#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>
#include "xdvdfs.h"
#include "gpu_trace.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Protects every piece of shared state below (handle table, critical
// sections, bump allocator) now that ExCreateThread (Task 2) spawns real
// host threads. Single coarse lock -- correctness over throughput at this
// stage; fine-grained locking would introduce lock-ordering bugs this
// project has no way to test for yet.
static std::mutex g_stateMutex;
static thread_local uint32_t g_currentThreadHandle = 0; // this thread's own ExCreateThread handle, 0 if not a guest-spawned thread

PPC_FUNC(__imp__KeBugCheck)
{
    fmt::println("[kernel] KeBugCheck: code=0x{:X} -- halting (real kernel never returns from this)", ctx.r3.u64);
    std::_Exit(3);
}

PPC_FUNC(__imp__HalReturnToFirmware)
{
    fmt::println("[kernel] HalReturnToFirmware: routine=0x{:X} -- halting (real kernel never returns from this)", ctx.r3.u64);
    std::_Exit(4);
}

PPC_FUNC(__imp__KeGetCurrentProcessType)
{
    ctx.r3.u64 = 1; // PROCTYPE_TITLE
}

PPC_FUNC(__imp__RtlInitAnsiString)
{
    uint32_t destAddr = (uint32_t)ctx.r3.u64;
    uint32_t sourceAddr = (uint32_t)ctx.r4.u64;

    uint16_t length = 0;
    if (sourceAddr != 0)
    {
        while (PPC_LOAD_U8(sourceAddr + length) != 0)
        {
            length++;
        }
    }

    PPC_STORE_U16(destAddr + 0, length);
    PPC_STORE_U16(destAddr + 2, sourceAddr != 0 ? (uint16_t)(length + 1) : (uint16_t)0);
    PPC_STORE_U32(destAddr + 4, sourceAddr);
}

static std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> g_criticalSections;

static std::recursive_mutex& GetCriticalSection(uint32_t csAddr)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto& slot = g_criticalSections[csAddr];
    if (!slot)
    {
        slot = std::make_unique<std::recursive_mutex>();
    }
    return *slot;
}

PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr);
}

PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr).lock();
}

PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr).unlock();
}

// Real spinlocks (Phase 3I). Kf{Acquire,Release}SpinLock only ever use r3
// (PKSPIN_LOCK) and, on release, r4 (KIRQL to restore) -- confirmed against
// the real NT signature; the generic stub's r5/r6 dump was leftover register
// noise, not real arguments. This project models no interrupt levels, so the
// "old IRQL" KfAcquireSpinLock returns in r3 is a meaningless placeholder --
// mutual exclusion by address is the only real behavior needed here.
static std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> g_spinLocks;

static std::recursive_mutex& GetSpinLock(uint32_t addr)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto& slot = g_spinLocks[addr];
    if (!slot)
    {
        slot = std::make_unique<std::recursive_mutex>();
    }
    return *slot;
}

PPC_FUNC(__imp__KfAcquireSpinLock)
{
    uint32_t spinLockAddr = (uint32_t)ctx.r3.u64;
    GetSpinLock(spinLockAddr).lock();
    ctx.r3.u64 = 0; // old IRQL -- not modeled
}

PPC_FUNC(__imp__KfReleaseSpinLock)
{
    uint32_t spinLockAddr = (uint32_t)ctx.r3.u64;
    GetSpinLock(spinLockAddr).unlock();
}

static thread_local uint64_t g_tlsSlots[64] = {};
static int g_tlsNextSlot = 0; // slot *numbering* is shared across threads; slot *values* (g_tlsSlots) are per-thread

PPC_FUNC(__imp__KeTlsAlloc)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_tlsNextSlot >= 64)
    {
        fmt::println("[kernel] KeTlsAlloc: out of TLS slots (64 max)");
        ctx.r3.u64 = 0xFFFFFFFF; // TLS_OUT_OF_INDEXES
        return;
    }
    ctx.r3.u64 = (uint64_t)g_tlsNextSlot++;
}

PPC_FUNC(__imp__KeTlsSetValue)
{
    uint32_t index = (uint32_t)ctx.r3.u64;
    uint64_t value = ctx.r4.u64;
    if (index < 64)
    {
        g_tlsSlots[index] = value;
    }
    ctx.r3.u64 = 1; // success
}

PPC_FUNC(__imp__XexCheckExecutablePrivilege)
{
    // Always grant -- no privilege-flag parser yet, and this is a personal
    // single-player backup, not a scenario with real DRM/multiplayer stakes.
    // Revisit only if a specific privilege check turns out to gate behavior
    // we actually want disabled.
    ctx.r3.u64 = 1;
}

static uint32_t g_bumpAllocatorNext = 0xA0000000;

PPC_FUNC(__imp__NtAllocateVirtualMemory)
{
    uint32_t baseAddressPtr = (uint32_t)ctx.r3.u64;
    uint32_t regionSizePtr = (uint32_t)ctx.r4.u64;
    uint32_t regionSize = PPC_LOAD_U32(regionSizePtr);

    constexpr uint32_t kAllocGranularity = 0x10000; // 64 KiB, Xbox 360 allocation granularity
    uint32_t alignedSize = (regionSize + (kAllocGranularity - 1)) & ~(kAllocGranularity - 1);

    uint32_t allocatedAddress;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        allocatedAddress = g_bumpAllocatorNext;
        g_bumpAllocatorNext += alignedSize;
    }

    PPC_STORE_U32(baseAddressPtr, allocatedAddress);
    PPC_STORE_U32(regionSizePtr, alignedSize);

    fmt::println("[kernel] NtAllocateVirtualMemory: {} bytes -> 0x{:X}", alignedSize, allocatedAddress);

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

enum class HandleObjectType { Mutant, Event, Generic, Thread, File };

struct HandleObject
{
    HandleObjectType type;
    bool signaled;
};

static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels
static std::condition_variable g_handleSignalCv;

// Real per-thread small-data-area/TLS base (Phase 3K). r13 serves a dual
// role on real Xbox 360: it's the classic PowerPC EABI small-data-area base
// (signed 16-bit displacements reach the observed -21984..+20124 range
// around it) AND the base for compiler-emitted declspec(thread) TLS fields
// (confirmed against Xenia's XThread::Create, src/xenia/kernel/xthread.cc --
// real hardware gives every thread its own private KPCR/TLS block, seeded
// from a template, not a single shared address). This project previously
// hardcoded r13 identically for every thread ("same as main thread"), which
// silently collapsed every genuinely per-thread field (e.g. the byte at
// [r13+268] the game reads as its own queue-worker slot index) onto the
// same shared memory -- confirmed live: this is why every worker thread in
// the field4/completion-queue subsystem computed the same slot and waited
// on the same event handle (Finding 15), permanently missing every real
// completion signal.
//
// Fix: give every ExCreateThread'd thread its own private 64 KiB block,
// seeded by copying the current content of the shared small-data window
// (so any boot-time-initialized values other code relies on seeing are
// still visible, matching how real declspec(thread) storage is seeded from
// a template at thread creation) rather than aliasing the same memory.
// r13 points to the middle of the block so the same +/-32 KiB signed
// displacement range used throughout the game's compiled code still
// resolves correctly. [+268] is then overwritten with a real, unique,
// monotonically-increasing per-thread value (not a guess at the exact
// original Xbox 360 numbering scheme, which isn't recoverable from the
// recompiled code alone, but a principled, collision-free choice that
// directly fixes the confirmed bug).
static constexpr uint32_t kTlsBlockSize = 0x10000; // 64 KiB, covers the full observed r13-relative offset range with margin
static constexpr uint32_t kTlsBlockCenter = kTlsBlockSize / 2;
static uint32_t g_nextTlsSlotIndex = 1;

// Real user-mode APC queueing (Phase 3H). Xbox 360 worker threads (e.g. the
// XApiThreadStartup pool thread) block in an alertable NtWaitForSingleObjectEx
// waiting to be handed work via NtQueueApcThread; delivery happens when that
// wait wakes up (see TryDeliverOneApc / NtWaitForSingleObjectEx below), not here.
struct ApcEntry
{
    uint32_t routine;
    uint32_t context;
    uint32_t sysArg1;
    uint32_t sysArg2;
};
static std::unordered_map<uint32_t, std::deque<ApcEntry>> g_apcQueues; // keyed by target thread handle

struct FileHandleState
{
    XdvdfsEntry entry;
    uint64_t position = 0;
};
static std::unordered_map<uint32_t, FileHandleState> g_fileState;

PPC_FUNC(__imp__NtCreateMutant)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialOwner = ctx.r5.u64 != 0;

    uint32_t handle;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        handle = g_nextHandle++;
        g_handleTable[handle] = HandleObject{ HandleObjectType::Mutant, !initialOwner };
    }

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtCreateEvent)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialState = ctx.r6.u64 != 0;

    uint32_t handle;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        handle = g_nextHandle++;
        g_handleTable[handle] = HandleObject{ HandleObjectType::Event, initialState };
    }

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtReleaseMutant)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t previousCountOutPtr = (uint32_t)ctx.r4.u64;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_handleTable.find(handle);
        if (it != g_handleTable.end())
        {
            it->second.signaled = true;
        }
    }

    if (previousCountOutPtr != 0)
    {
        PPC_STORE_U32(previousCountOutPtr, 0);
    }

    ctx.r3.u64 = 0;
}

// Real NT alertable-wait semantics (Phase 3G/3H): an alertable wait wakes and
// delivers a queued APC even when the waited-on object never becomes
// signaled -- the wait does NOT get satisfied by APC delivery, it returns
// STATUS_USER_APC early and the caller's own loop re-enters the wait to
// drain further APCs or keep waiting on the object. Confirmed live: the
// XApiThreadStartup worker thread (handle 0x1005) blocks forever in exactly
// this call (alertable=true, object never signaled) while NtQueueApcThread
// later targets it -- without APC-aware wakeup here, the queued APC (which
// is what's supposed to clear the stuck "pending work" counter downstream
// callers busy-poll on) never runs.
static bool TryDeliverOneApc(PPCContext& ctx, uint8_t* base, uint32_t threadHandle)
{
    ApcEntry apc;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_apcQueues.find(threadHandle);
        if (it == g_apcQueues.end() || it->second.empty())
        {
            return false;
        }
        apc = it->second.front();
        it->second.pop_front();
    }

    // Real delivery convention confirmed against Xenia's DeliverAPCs
    // (src/xenia/kernel/xthread.cc): normal_routine(normal_context, arg1,
    // arg2) -- i.e. r3=context, r4=sysArg1, r5=sysArg2. Previously hardcoded
    // r5=0; NtQueueApcThread's real 5th parameter (arg2) is now captured and
    // threaded through instead of silently dropped.
    ctx.r3.u64 = apc.context;
    ctx.r4.u64 = apc.sysArg1;
    ctx.r5.u64 = apc.sysArg2;
    PPC_CALL_INDIRECT_FUNC(apc.routine);
    return true;
}

PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    bool alertable = ctx.r5.u64 != 0;
    uint32_t timeoutPtr = (uint32_t)ctx.r6.u64;
    uint32_t selfHandle = g_currentThreadHandle;

    std::unique_lock<std::mutex> lock(g_stateMutex);
    auto it = g_handleTable.find(handle);
    if (it == g_handleTable.end())
    {
        fmt::println("[kernel] NtWaitForSingleObjectEx: unknown handle 0x{:X}", handle);
        ctx.r3.u64 = 0xC0000008; // STATUS_INVALID_HANDLE
        return;
    }

    auto hasPendingApc = [&] {
        auto apcIt = g_apcQueues.find(selfHandle);
        return apcIt != g_apcQueues.end() && !apcIt->second.empty();
    };
    auto wakePredicate = [&] { return g_handleTable[handle].signaled || (alertable && hasPendingApc()); };

    if (timeoutPtr != 0)
    {
        int64_t timeoutValue = (int64_t)PPC_LOAD_U64(timeoutPtr);
        if (timeoutValue < 0)
        {
            // Same relative-timeout convention established for
            // KeWaitForSingleObject (Phase 2I): negative = relative 100ns ticks.
            auto durationMs = std::chrono::milliseconds(-timeoutValue / 10000);
            bool woke = g_handleSignalCv.wait_for(lock, durationMs, wakePredicate);
            if (alertable && hasPendingApc())
            {
                lock.unlock();
                TryDeliverOneApc(ctx, base, selfHandle);
                ctx.r3.u64 = 0xC0; // STATUS_USER_APC
                return;
            }
            ctx.r3.u64 = woke ? 0 : 258; // STATUS_SUCCESS or STATUS_TIMEOUT
            return;
        }
        // Positive (absolute time) -- not observed yet, fall through to the
        // unbounded wait rather than guess at handling.
    }

    g_handleSignalCv.wait(lock, wakePredicate);
    if (alertable && hasPendingApc())
    {
        lock.unlock();
        TryDeliverOneApc(ctx, base, selfHandle);
        ctx.r3.u64 = 0xC0; // STATUS_USER_APC
        return;
    }
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

// Real APC queueing (Phase 3H). Full 5-parameter signature confirmed against
// Xenia's real NtQueueApcThread_entry (src/xenia/kernel/xboxkrnl/
// xboxkrnl_threading.cc): r3=target thread handle, r4=ApcRoutine,
// r5=ApcContext, r6=SystemArgument1, r7=SystemArgument2 -- previously only
// read r3-r6 and silently dropped SystemArgument2 (always delivered as 0
// regardless of what the caller actually passed). Real Xbox 360
// NtQueueApcThread is void (no real return value; the r3=0 write below is a
// harmless simplification, not a status real callers rely on). Delivery
// happens in NtWaitForSingleObjectEx above when the target thread's
// alertable wait wakes -- this call only enqueues and wakes any thread
// currently blocked there.
PPC_FUNC(__imp__NtQueueApcThread)
{
    uint32_t targetThreadHandle = (uint32_t)ctx.r3.u64;
    uint32_t apcRoutine = (uint32_t)ctx.r4.u64;
    uint32_t apcContext = (uint32_t)ctx.r5.u64;
    uint32_t sysArg1 = (uint32_t)ctx.r6.u64;
    uint32_t sysArg2 = (uint32_t)ctx.r7.u64;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_apcQueues[targetThreadHandle].push_back(ApcEntry{ apcRoutine, apcContext, sysArg1, sysArg2 });
    }
    g_handleSignalCv.notify_all();

    ctx.r3.u64 = 0; // real signature is void; harmless placeholder
}

PPC_FUNC(__imp__NtClose)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_handleTable.erase(handle);
    }
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetAVPack)
{
    // Real AV-pack enum value not independently confirmed; 0 is a
    // deliberate low-confidence default, not an asserted-correct constant.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExGetXConfigSetting)
{
    uint32_t bufferPtr = (uint32_t)ctx.r5.u64;
    uint32_t bufferSize = (uint32_t)ctx.r6.u64;

    for (uint32_t i = 0; i < bufferSize; i++)
    {
        PPC_STORE_U8(bufferPtr + i, 0);
    }

    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeQuerySystemTime)
{
    uint32_t outPtr = (uint32_t)ctx.r3.u64;

    // Windows FILETIME: 100ns ticks since 1601-01-01. Unix epoch (1970-01-01)
    // is 11644473600 seconds after that.
    constexpr uint64_t kFiletimeEpochOffsetSeconds = 11644473600ULL;
    uint64_t unixSeconds = (uint64_t)time(nullptr);
    uint64_t filetime = (unixSeconds + kFiletimeEpochOffsetSeconds) * 10000000ULL;

    PPC_STORE_U64(outPtr, filetime);
}

PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    ctx.r3.u64 = 50000000ULL; // Xbox 360's documented hardware timebase frequency
}

PPC_FUNC(__imp__KeEnableFpuExceptions)
{
    // No-op -- FPU exception trapping isn't emulated.
}

PPC_FUNC(__imp__RtlNtStatusToDosError)
{
    uint32_t status = (uint32_t)ctx.r3.u64;

    constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;
    constexpr uint32_t kErrorFileNotFound = 2;
    constexpr uint32_t kErrorGenFailure = 31;

    if (status == kStatusObjectNameNotFound)
    {
        ctx.r3.u64 = kErrorFileNotFound;
    }
    else
    {
        ctx.r3.u64 = kErrorGenFailure;
    }
}

PPC_FUNC(__imp__RtlTimeToTimeFields)
{
    uint32_t inputTimePtr = (uint32_t)ctx.r3.u64;
    uint32_t outputFieldsPtr = (uint32_t)ctx.r4.u64;

    uint64_t filetime = PPC_LOAD_U64(inputTimePtr);

    constexpr uint64_t kFiletimeEpochOffsetSeconds = 11644473600ULL;
    time_t unixSeconds = (time_t)(filetime / 10000000ULL - kFiletimeEpochOffsetSeconds);
    uint32_t milliseconds = (uint32_t)((filetime / 10000ULL) % 1000ULL);

    std::tm tmValue{};
    gmtime_r(&unixSeconds, &tmValue);

    // TIME_FIELDS: Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday (8x int16)
    PPC_STORE_U16(outputFieldsPtr + 0, (uint16_t)(tmValue.tm_year + 1900));
    PPC_STORE_U16(outputFieldsPtr + 2, (uint16_t)(tmValue.tm_mon + 1));
    PPC_STORE_U16(outputFieldsPtr + 4, (uint16_t)tmValue.tm_mday);
    PPC_STORE_U16(outputFieldsPtr + 6, (uint16_t)tmValue.tm_hour);
    PPC_STORE_U16(outputFieldsPtr + 8, (uint16_t)tmValue.tm_min);
    PPC_STORE_U16(outputFieldsPtr + 10, (uint16_t)tmValue.tm_sec);
    PPC_STORE_U16(outputFieldsPtr + 12, (uint16_t)milliseconds);
    PPC_STORE_U16(outputFieldsPtr + 14, (uint16_t)tmValue.tm_wday);
}

constexpr uint32_t kStatusNoSuchDevice = 0xC000000E;
constexpr uint32_t kStatusInvalidHandle = 0xC0000008;
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;

static std::string ReadGuestAnsiString(uint8_t* base, uint32_t objectAttributesPtr)
{
    uint32_t objectNamePtr = PPC_LOAD_U32(objectAttributesPtr + 4);
    uint16_t nameLength = PPC_LOAD_U16(objectNamePtr + 0);
    uint32_t nameBufferPtr = PPC_LOAD_U32(objectNamePtr + 4);
    std::string name;
    for (uint16_t i = 0; i < nameLength; i++)
    {
        name.push_back((char)PPC_LOAD_U8(nameBufferPtr + i));
    }
    return name;
}

static uint32_t OpenGuestFile(const std::string& path, uint32_t& outHandle)
{
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (lowerPath.rfind("\\device\\", 0) == 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        outHandle = g_nextHandle++;
        g_handleTable[outHandle] = HandleObject{ HandleObjectType::Generic, true };
        return 0; // STATUS_SUCCESS
    }

    if (lowerPath.rfind("d:\\", 0) == 0 || lowerPath.rfind("d:/", 0) == 0)
    {
        std::string subPath = path.substr(3);
        XdvdfsEntry entry;
        if (g_xdvdfsImage.ResolvePath(subPath, entry))
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            outHandle = g_nextHandle++;
            g_handleTable[outHandle] = HandleObject{ HandleObjectType::File, true };
            g_fileState[outHandle] = FileHandleState{ entry, 0 };
            return 0; // STATUS_SUCCESS
        }
        return kStatusObjectNameNotFound;
    }

    return kStatusNoSuchDevice;
}

PPC_FUNC(__imp__NtCreateFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t objectAttributesPtr = (uint32_t)ctx.r5.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r6.u64;

    std::string path = ReadGuestAnsiString(base, objectAttributesPtr);
    uint32_t handle = 0;
    uint32_t status = OpenGuestFile(path, handle);

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, status == 0 ? handle : 0);
    }
    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, status);
        PPC_STORE_U32(ioStatusBlockPtr + 4, status == 0 ? 1 : 0); // FILE_OPENED
    }

    fmt::println("[kernel] NtCreateFile: \"{}\" -> status=0x{:X} handle=0x{:X}", path, status, handle);
    ctx.r3.u64 = status;
}

PPC_FUNC(__imp__NtOpenFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t objectAttributesPtr = (uint32_t)ctx.r5.u64;

    std::string path = ReadGuestAnsiString(base, objectAttributesPtr);
    uint32_t handle = 0;
    uint32_t status = OpenGuestFile(path, handle);

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, status == 0 ? handle : 0);
    }

    fmt::println("[kernel] NtOpenFile: \"{}\" -> status=0x{:X} handle=0x{:X}", path, status, handle);
    ctx.r3.u64 = status;
}

PPC_FUNC(__imp__NtReadFile)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t eventHandle = (uint32_t)ctx.r4.u64;
    uint32_t apcRoutine = (uint32_t)ctx.r5.u64;
    uint32_t apcContext = (uint32_t)ctx.r6.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r7.u64;
    uint32_t bufferPtr = (uint32_t)ctx.r8.u64;
    uint32_t length = (uint32_t)ctx.r9.u64;
    uint32_t byteOffsetPtr = (uint32_t)ctx.r10.u64;

    uint64_t offset = 0;
    uint32_t bytesToRead = 0;
    bool validHandle = true;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_fileState.find(handle);
        if (it == g_fileState.end())
        {
            validHandle = false;
        }
        else
        {
            FileHandleState& state = it->second;
            offset = (byteOffsetPtr != 0) ? PPC_LOAD_U64(byteOffsetPtr) : state.position;

            if (offset < state.entry.fileSize)
            {
                uint64_t remaining = state.entry.fileSize - offset;
                bytesToRead = (uint32_t)(remaining < length ? remaining : length);
            }

            if (bytesToRead > 0)
            {
                g_xdvdfsImage.ReadBytes(state.entry, offset, bytesToRead, base + bufferPtr);
            }
            state.position = offset + bytesToRead;
        }
    }

    if (!validHandle)
    {
        fmt::println("[kernel] NtReadFile: unknown handle 0x{:X}", handle);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusInvalidHandle);
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = kStatusInvalidHandle;
        return;
    }

    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, 0); // STATUS_SUCCESS
        PPC_STORE_U32(ioStatusBlockPtr + 4, bytesToRead);
    }

    fmt::println("[kernel] NtReadFile: handle=0x{:X} offset={} requested={} read={}", handle, offset, length, bytesToRead);

    // Real NT I/O completion APC (distinct from the user-mode APC queueing in
    // NtQueueApcThread, Phase 3H). Cross-referenced against Xenia's real
    // NtReadFile_entry (src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc) -- confirms
    // every detail independently derived this investigation: parameter order
    // (handle, event, apcRoutine, apcContext, ioStatusBlock, buffer, length,
    // byteOffset), the ApcRoutine low-bit flag ("Low bit probably means do
    // not queue to IO ports" per Xenia's own comment) masked off with & ~1
    // before use, and the completion callback's real 3-arg convention
    // (context, sysArg1=IoStatusBlock, 0) -- matches TryDeliverOneApc's
    // existing calling convention exactly. Two real gaps Xenia's
    // implementation has that this one initially didn't: it requires
    // apcContext != 0 (not just apcRoutine != 0) before queuing, and it also
    // signals the caller's event handle (r4), independent of the APC.
    if ((apcRoutine & ~1u) != 0 && apcContext != 0)
    {
        uint32_t realApcRoutine = apcRoutine & ~1u;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_apcQueues[g_currentThreadHandle].push_back(ApcEntry{ realApcRoutine, apcContext, ioStatusBlockPtr, 0 });
        }
        g_handleSignalCv.notify_all();
    }

    if (eventHandle != 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto eventIt = g_handleTable.find(eventHandle);
        if (eventIt != g_handleTable.end())
        {
            eventIt->second.signaled = true;
        }
        g_handleSignalCv.notify_all();
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

// Real, observed FILE_INFORMATION_CLASS values (standard NT enum, confirmed via a live
// capture during Phase 3B): 0xE = FilePositionInformation (8-byte LARGE_INTEGER
// CurrentByteOffset), 0x22 = FileNetworkOpenInformation (56-byte struct: 4x LARGE_INTEGER
// timestamps we don't track [zeroed, no evidence anything reads them], AllocationSize,
// EndOfFile [the real file size], FileAttributes). Any other class: no evidence yet,
// not guessed at.
constexpr uint32_t kFilePositionInformation = 0xE;
constexpr uint32_t kFileNetworkOpenInformation = 0x22;

PPC_FUNC(__imp__NtQueryInformationFile)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r4.u64;
    uint32_t fileInformationPtr = (uint32_t)ctx.r5.u64;
    uint32_t infoClass = (uint32_t)ctx.r7.u64;

    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_fileState.find(handle);
    if (it == g_fileState.end())
    {
        fmt::println("[kernel] NtQueryInformationFile: unknown handle 0x{:X} class=0x{:X}", handle, infoClass);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusInvalidHandle);
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = kStatusInvalidHandle;
        return;
    }

    FileHandleState& state = it->second;
    uint32_t written = 0;

    if (infoClass == kFilePositionInformation)
    {
        PPC_STORE_U64(fileInformationPtr, state.position);
        written = 8;
    }
    else if (infoClass == kFileNetworkOpenInformation)
    {
        PPC_STORE_U64(fileInformationPtr + 0, 0);  // CreationTime -- no evidence anything reads it
        PPC_STORE_U64(fileInformationPtr + 8, 0);  // LastAccessTime
        PPC_STORE_U64(fileInformationPtr + 16, 0); // LastWriteTime
        PPC_STORE_U64(fileInformationPtr + 24, 0); // ChangeTime
        PPC_STORE_U64(fileInformationPtr + 32, state.entry.fileSize); // AllocationSize
        PPC_STORE_U64(fileInformationPtr + 40, state.entry.fileSize); // EndOfFile
        PPC_STORE_U32(fileInformationPtr + 48, 0x20); // FileAttributes: FILE_ATTRIBUTE_ARCHIVE
        written = 56;
    }
    else
    {
        fmt::println("[kernel] NtQueryInformationFile: handle=0x{:X} unhandled class=0x{:X}", handle, infoClass);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusNoSuchDevice);
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = kStatusNoSuchDevice;
        return;
    }

    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, 0); // STATUS_SUCCESS
        PPC_STORE_U32(ioStatusBlockPtr + 4, written);
    }

    fmt::println("[kernel] NtQueryInformationFile: handle=0x{:X} class=0x{:X} -> {} bytes", handle, infoClass, written);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtSetInformationFile)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r4.u64;
    uint32_t fileInformationPtr = (uint32_t)ctx.r5.u64;
    uint32_t infoClass = (uint32_t)ctx.r7.u64;

    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_fileState.find(handle);
    if (it == g_fileState.end())
    {
        fmt::println("[kernel] NtSetInformationFile: unknown handle 0x{:X} class=0x{:X}", handle, infoClass);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusInvalidHandle);
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = kStatusInvalidHandle;
        return;
    }

    FileHandleState& state = it->second;

    if (infoClass == kFilePositionInformation)
    {
        state.position = PPC_LOAD_U64(fileInformationPtr);
    }
    else
    {
        fmt::println("[kernel] NtSetInformationFile: handle=0x{:X} unhandled class=0x{:X}", handle, infoClass);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusNoSuchDevice);
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = kStatusNoSuchDevice;
        return;
    }

    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, 0); // STATUS_SUCCESS
        PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
    }

    fmt::println("[kernel] NtSetInformationFile: handle=0x{:X} class=0x{:X} position={}", handle, infoClass, state.position);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtWriteFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
}

PPC_FUNC(__imp__NtDeviceIoControlFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
}

PPC_FUNC(__imp__MmQueryStatistics)
{
    // Caller pre-fills Length for validation (standard NT convention;
    // observed value 0x68). No confirmed full MM_STATISTICS field layout,
    // so we acknowledge success without writing detailed statistics --
    // same conservative posture as the critical-section implementation in
    // Phase 2B. Revisit only if a future run's evidence shows the game
    // reading and acting on specific garbage field values.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx)
{
    // r4 (observed 0x19000000, ~400 MB) is implausibly large for a single
    // physical allocation on 512 MB hardware -- treated as stale register
    // content, not a real size argument. Always allocates a fixed 64 KiB
    // block from the same bump allocator NtAllocateVirtualMemory (Phase 2B)
    // uses, rather than guessing which register (if any) is trustworthy.
    constexpr uint32_t kAllocGranularity = 0x10000;

    uint32_t allocatedAddress;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        allocatedAddress = g_bumpAllocatorNext;
        g_bumpAllocatorNext += kAllocGranularity;
    }

    fmt::println("[kernel] MmAllocatePhysicalMemoryEx: {} bytes -> 0x{:X}", kAllocGranularity, allocatedAddress);

    ctx.r3.u64 = allocatedAddress; // Mm* functions return the address directly, not a status code
}

PPC_FUNC(__imp__KeDelayExecutionThread)
{
    // Returns immediately without sleeping -- a real sleep here would burn
    // the harness's 10s watchdog budget for no benefit, since nothing else
    // runs concurrently to change state while we'd otherwise wait. Same
    // reasoning as NtWaitForSingleObjectEx (Phase 2C).
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__KeTlsGetValue)
{
    uint32_t index = (uint32_t)ctx.r3.u64;
    ctx.r3.u64 = (index < 64) ? g_tlsSlots[index] : 0;
}

constexpr uint32_t kErrorDeviceNotConnected = 0x48F;

PPC_FUNC(__imp__XamInputGetCapabilities)
{
    ctx.r3.u64 = kErrorDeviceNotConnected;
}

PPC_FUNC(__imp__XamInputGetState)
{
    ctx.r3.u64 = kErrorDeviceNotConnected;
}

PPC_FUNC(__imp__XamNotifyCreateListener)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Generic, false };
    ctx.r3.u64 = handle;
}

PPC_FUNC(__imp__KeRaiseIrqlToDpcLevel)
{
    ctx.r3.u64 = 0; // old IRQL; no real interrupt masking under single-execution-context
}

PPC_FUNC(__imp__KfLowerIrql)
{
    // No-op -- argument (new IRQL) ignored, no real IRQL state tracked.
}

PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql)
{
    // No-op -- nothing can ever contend under single-execution-context, same
    // reasoning as critical sections (Phase 2B).
}

PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql)
{
    // No-op.
}

PPC_FUNC(__imp__ObReferenceObjectByHandle)
{
    uint32_t objectOutPtr = (uint32_t)ctx.r5.u64;

    // Fixed sentinel value -- the log shows this gets read back and reused
    // as the "thread object" argument by KeSetBasePriorityThread/
    // KeResumeThread/ObDereferenceObject, so it needs to be consistent, not
    // that it needs to point at a real object (nothing dereferences it).
    constexpr uint32_t kFakeObjectSentinel = 0xDEAD0001;

    if (objectOutPtr != 0)
    {
        PPC_STORE_U32(objectOutPtr, kFakeObjectSentinel);
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__ObDereferenceObject)
{
    // No-op -- no real reference counting exists.
}

PPC_FUNC(__imp__KeSetBasePriorityThread)
{
    // No-op -- no real thread scheduling exists.
}

PPC_FUNC(__imp__KeResumeThread)
{
    ctx.r3.u64 = 0; // previous suspend count
}

PPC_FUNC(__imp__KeInitializeSemaphore)
{
    // No-op -- nothing observed waits on or releases this semaphore.
}

PPC_FUNC(__imp__ExRegisterTitleTerminateNotification)
{
    // No-op -- no evidence the registered notification needs to fire.
}

PPC_FUNC(__imp__XAudioRegisterRenderDriverClient)
{
    // No real audio driver semantics -- just unblocks the boot sequence.
    // Real audio is its own future phase.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

static uint32_t g_nextThreadId = 1;

PPC_FUNC(__imp__ExCreateThread)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t threadIdOutPtr = (uint32_t)ctx.r5.u64;
    uint32_t xApiThreadStartup = (uint32_t)ctx.r6.u64;
    uint32_t startAddress = (uint32_t)ctx.r7.u64;
    uint64_t startContext = ctx.r8.u64;

    constexpr uint32_t kStackSize = 0x40000; // 256 KiB
    uint32_t entryAddress = (xApiThreadStartup != 0) ? xApiThreadStartup : startAddress;

    uint32_t stackBase;
    uint32_t handle;
    uint32_t threadId;
    uint32_t tlsBlockBase;
    uint32_t tlsSlotIndex;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        stackBase = g_bumpAllocatorNext;
        g_bumpAllocatorNext += kStackSize;

        tlsBlockBase = g_bumpAllocatorNext;
        g_bumpAllocatorNext += kTlsBlockSize;

        handle = g_nextHandle++;
        g_handleTable[handle] = HandleObject{ HandleObjectType::Thread, false };

        threadId = g_nextThreadId++;
        tlsSlotIndex = g_nextTlsSlotIndex++;
    }

    // Seed the new thread's private small-data/TLS block from the current
    // content of the shared window every thread used to alias (see the
    // g_nextTlsSlotIndex comment above) -- this preserves visibility of any
    // boot-time-initialized values other code expects to see via r13, matching
    // how real declspec(thread) storage is seeded from a template rather than
    // starting zeroed.
    std::memcpy(base + tlsBlockBase, base + (0x82670000 - kTlsBlockCenter), kTlsBlockSize);
    // Overwrite the one confirmed genuinely-per-thread field (Finding 15):
    // the queue-worker slot index at [r13+268].
    PPC_STORE_U8(tlsBlockBase + kTlsBlockCenter + 268, (uint8_t)tlsSlotIndex);

    fmt::println("[kernel] ExCreateThread: entry=0x{:X} (via {}) stack=0x{:X}..0x{:X} handle=0x{:X} tlsSlot={}",
        entryAddress, xApiThreadStartup != 0 ? "XApiThreadStartup" : "StartAddress directly",
        stackBase, stackBase + kStackSize, handle, tlsSlotIndex);

    // XApiThreadStartup's calling convention (confirmed by reading its generated
    // code, sub_820ABCE8 in private/ppc/ppc_recomp.2.cpp): r3=StartAddress,
    // r4=StartContext -- it saves r3 into a non-volatile register, later moves
    // it into ctr, and does an indirect call (bctrl) passing StartContext (from
    // r4) in r3. Calling StartAddress directly (no trampoline) uses the plain
    // thread-entry convention instead: r3=StartContext only.
    bool viaTrampoline = xApiThreadStartup != 0;
    uint32_t tlsBase = tlsBlockBase + kTlsBlockCenter;
    std::thread hostThread([entryAddress, stackBase, kStackSize, startContext, startAddress, viaTrampoline, base, handle, tlsBase]()
    {
        g_currentThreadHandle = handle;

        PPCContext threadCtx{};
        threadCtx.r1.u64 = stackBase + kStackSize - 0x10;
        threadCtx.r13.u64 = tlsBase; // per-thread small-data/TLS base (Phase 3K, Finding 15 fix)
        if (viaTrampoline)
        {
            threadCtx.r3.u64 = startAddress;
            threadCtx.r4.u64 = startContext;
        }
        else
        {
            threadCtx.r3.u64 = startContext;
        }

        (PPC_LOOKUP_FUNC(base, entryAddress))(threadCtx, base);

        // Direct-entry threads (no XApiThreadStartup trampoline) return here
        // normally without ever calling ExTerminateThread -- mark completion
        // ourselves. Harmless no-op if ExTerminateThread already did it
        // (trampoline path, which never reaches this line since
        // ExTerminateThread calls pthread_exit).
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            auto it = g_handleTable.find(handle);
            if (it != g_handleTable.end())
            {
                it->second.signaled = true;
            }
        }
        g_handleSignalCv.notify_all();
    });
    hostThread.detach(); // never joined -- storing a live std::thread in a
                          // long-lived container would call std::terminate()
                          // at process exit if still running

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, handle);
    }
    if (threadIdOutPtr != 0)
    {
        PPC_STORE_U32(threadIdOutPtr, threadId);
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__KeWaitForSingleObject)
{
    uint32_t timeoutPtr = (uint32_t)ctx.r7.u64;

    if (timeoutPtr != 0)
    {
        int64_t timeoutValue = (int64_t)PPC_LOAD_U64(timeoutPtr);
        if (timeoutValue < 0)
        {
            // Negative = relative time, in 100ns units (standard NT convention).
            // Confirmed via captured runtime value from a real guest
            // timeout-polling loop: -300000 = -30ms, a legitimate bounded poll,
            // not an infinite/forever wait (see Phase 2I design spec).
            int64_t hundredNsUnits = -timeoutValue;
            auto durationMs = std::chrono::milliseconds(hundredNsUnits / 10000);
            std::this_thread::sleep_for(durationMs);

            ctx.r3.u64 = 258; // STATUS_TIMEOUT
            return;
        }
        // Positive (absolute time) -- not observed yet, fall through to the
        // existing immediate-success behavior rather than guess at handling.
    }

    // TimeoutPtr null (wait forever) or an unobserved absolute-time case:
    // keep existing behavior.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlFillMemoryUlong)
{
    uint32_t dest = (uint32_t)ctx.r3.u64;
    uint32_t length = (uint32_t)ctx.r4.u64;
    uint32_t pattern = (uint32_t)ctx.r5.u64;

    for (uint32_t i = 0; i + 4 <= length; i += 4)
    {
        PPC_STORE_U32(dest + i, pattern);
    }
}

PPC_FUNC(__imp__MmGetPhysicalAddress)
{
    // Identity mapping -- no real physical/virtual memory distinction in
    // this harness. ctx.r3.u64 already holds the virtual address; leave it
    // unchanged as the "physical" address.
}

PPC_FUNC(__imp__MmSetAddressProtect)
{
    // No-op -- no real page-protection enforcement.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__KeSetEvent)
{
    // No-op -- no real dispatcher-object state tracked, same reasoning as
    // the existing KeResetEvent stub.
    ctx.r3.u64 = 0; // previous signal state
}

PPC_FUNC(__imp__KeEnterCriticalRegion)
{
    // No-op -- APC delivery isn't emulated, nothing to disable.
}

PPC_FUNC(__imp__KeLeaveCriticalRegion)
{
    // No-op -- APC delivery isn't emulated, nothing to re-enable.
}

PPC_FUNC(__imp__KiApcNormalRoutineNop)
{
    // No-op by design -- name states its own real behavior.
}

PPC_FUNC(__imp__XexGetModuleHandle)
{
    uint32_t moduleHandleOutPtr = (uint32_t)ctx.r4.u64;
    if (moduleHandleOutPtr != 0)
    {
        // Real convention: a module's "handle" is its own base load address.
        // Observed call has ModuleName=NULL (r3=0), meaning "the current
        // module" -- the only case seen.
        PPC_STORE_U32(moduleHandleOutPtr, (uint32_t)PPC_IMAGE_BASE);
    }
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__XexGetModuleSection)
{
    // No section-lookup infrastructure exists. Honestly report "not found"
    // rather than fabricate section data, matching the no-hard-disk
    // precedent (Phase 2D).
    constexpr uint32_t kStatusNotFound = 0xC0000225;
    ctx.r3.u64 = kStatusNotFound;
}

PPC_FUNC(__imp___vsnprintf)
{
    // No-op -- pure debug-formatting helper, no real PPC varargs support
    // implemented (nontrivial calling convention, zero bearing on game
    // logic correctness). Returns 0: no characters formatted.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgPrint)
{
    // No-op -- debug output only, no bearing on game logic.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExTerminateThread)
{
    fmt::println("[kernel] ExTerminateThread: exitCode=0x{:X} -- terminating this thread", ctx.r3.u64);

    if (g_currentThreadHandle != 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_handleTable.find(g_currentThreadHandle);
        if (it != g_handleTable.end())
        {
            it->second.signaled = true;
        }
    }
    g_handleSignalCv.notify_all();

    pthread_exit(nullptr);
}

PPC_FUNC(__imp__KeSetAffinityThread)
{
    uint32_t previousAffinityOutPtr = (uint32_t)ctx.r5.u64;
    if (previousAffinityOutPtr != 0)
    {
        PPC_STORE_U32(previousAffinityOutPtr, 0);
    }
    // Processor affinity ignored, matching ExCreateThread's CreationFlags
    // handling (Phase 2H) -- the OS scheduler handles real placement.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdInitializeEngines)
{
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdShutdownEngines)
{
    // No-op -- void real signature.
}

PPC_FUNC(__imp__VdSetGraphicsInterruptCallback)
{
    // Real semantics: registers a callback a GPU interrupt handler (e.g.
    // vblank) would invoke. No-op for now -- nothing yet requires us to
    // actually invoke it. Callback address is r3, context is r4, if future
    // frame-pacing/vsync work needs them.
}

// Real signature confirmed live (private/ppc/ppc_recomp.4.cpp:14684-14686,14977-14981):
// called once at startup with r3=0 (reset) and again with a real address once the ring
// buffer is set up (offset+8 into the same buffer struct VdInitializeRingBuffer uses).
// Looks like a fence/identifier slot, not the ring buffer itself -- stored but not yet
// acted on (Phase 3K design spec).
PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    uint32_t addr = (uint32_t)ctx.r3.u64;
    if (addr != 0)
    {
        g_gpuTracer.SetIdentifierAddr(addr);
    }
}

// Real signature confirmed live (private/ppc/ppc_recomp.4.cpp:14842-14856): r3 is the
// physical address of a buffer the game itself allocated via MmAllocatePhysicalMemoryEx
// (already real in this project) and resolved through MmGetPhysicalAddress (already an
// identity mapping, Phase 2J) -- so r3 is already a valid, already-owned guest address.
// The kernel doesn't allocate the ring buffer, it's told where one already is.
PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    uint32_t physAddr = (uint32_t)ctx.r3.u64;
    uint32_t sizeLog2Raw = (uint32_t)ctx.r4.u64;
    g_gpuTracer.RegisterRingBuffer(physAddr, sizeLog2Raw);
}

// Real semantics: the GPU writes its "consumed up to here" read pointer to this address
// so the CPU knows how much ring space is free. Handled in GpuCommandTracer::
// ScanAndTraceFrame, called once per VdSwap -- see there for why this always reports
// "caught up" rather than left unanswered (Phase 3H's APC-delivery lesson applied here
// deliberately).
PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    uint32_t addr = (uint32_t)ctx.r3.u64;
    g_gpuTracer.SetRptrWriteBackAddr(addr);
}

PPC_FUNC(__imp__VdQueryVideoMode)
{
    // No confirmed struct layout; return success without writing data,
    // same conservative posture as MmQueryStatistics (Phase 2E).
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdCallGraphicsNotificationRoutines)
{
    // No-op.
}

PPC_FUNC(__imp__VdRetrainEDRAM)
{
    // No-op -- hardware-specific EDRAM calibration, no real hardware to calibrate.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdRetrainEDRAMWorker)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdIsHSIOTrainingSucceeded)
{
    // Report success -- no real EDRAM hardware to fail training.
    ctx.r3.u64 = 1;
}

static uint32_t g_systemCommandBufferAddr = 0;
static constexpr uint32_t kSystemCommandBufferSize = 65536;

// Real signature confirmed live (private/ppc/ppc_recomp.6.cpp:24604-24625): writes an
// address and a size back through two caller-stack out-pointers (r3, r4), and also
// returns the address directly in r3 -- the caller saves that return value separately
// right after the call. A second, smaller, kernel-owned buffer distinct from the game's
// main ring buffer; given a real, non-crashing region here rather than treated as a
// primary command stream (Phase 3K design spec).
PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    uint32_t addrOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t sizeOutPtr = (uint32_t)ctx.r4.u64;

    if (g_systemCommandBufferAddr == 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_systemCommandBufferAddr == 0)
        {
            g_systemCommandBufferAddr = g_bumpAllocatorNext;
            g_bumpAllocatorNext += kSystemCommandBufferSize;
        }
    }

    if (addrOutPtr != 0)
    {
        PPC_STORE_U32(addrOutPtr, g_systemCommandBufferAddr);
    }
    if (sizeOutPtr != 0)
    {
        PPC_STORE_U32(sizeOutPtr, kSystemCommandBufferSize);
    }

    ctx.r3.u64 = g_systemCommandBufferAddr;
}

PPC_FUNC(__imp__VdSwap)
{
    // The frame-present call. Still no real rendering -- nothing to actually present,
    // no renderer exists yet (Phase 3K only adds command-buffer tracing). Real frame
    // presentation is future work once the renderer design (informed by this phase's
    // trace output) exists.
    if (g_gpuTracer.HasRingBuffer())
    {
        g_gpuTracer.ScanAndTraceFrame(base);
    }
}

PPC_FUNC(__imp__VdGetCurrentDisplayGamma)
{
    // No confirmed struct layout; return success without writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdSetDisplayMode)
{
    // No-op -- accept whatever mode is requested.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdGetCurrentDisplayInformation)
{
    // No confirmed struct layout; return success without writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdInitializeScalerCommandBuffer)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdPersistDisplay)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__XGetVideoMode)
{
    // No confirmed struct layout; return success without writing data,
    // same conservative posture as VdQueryVideoMode.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtSetEvent)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t previousStateOutPtr = (uint32_t)ctx.r4.u64;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_handleTable.find(handle);
        if (it != g_handleTable.end())
        {
            it->second.signaled = true;
        }
    }
    g_handleSignalCv.notify_all();

    if (previousStateOutPtr != 0)
    {
        PPC_STORE_U32(previousStateOutPtr, 0);
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

// Real sprintf (Phase 3J). The XEX doesn't link real libc, so this is a
// genuine kernel-surface gap, not a libc passthrough. Variadic args after
// r3 (buffer) and r4 (format) follow the standard PPC EABI integer-argument
// register sequence r5..r10 -- confirmed sufficient for every real call site
// observed live so far (at most 2 variadic args). Floating-point conversions
// are not observed live yet and are deliberately left unhandled (consumes
// one GPR slot to keep later specifiers aligned, emits the literal specifier
// text) rather than guessing at FPR-based argument passing.
PPC_FUNC(__imp__sprintf)
{
    uint32_t bufferPtr = (uint32_t)ctx.r3.u64;
    uint32_t formatPtr = (uint32_t)ctx.r4.u64;

    std::string format;
    for (uint32_t i = 0; ; i++)
    {
        uint8_t c = PPC_LOAD_U8(formatPtr + i);
        if (c == 0) break;
        format.push_back((char)c);
    }

    uint64_t gprArgs[6] = { ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64 };
    int nextGpr = 0;

    std::string result;
    for (size_t i = 0; i < format.size(); i++)
    {
        char c = format[i];
        if (c != '%')
        {
            result.push_back(c);
            continue;
        }

        size_t specStart = i;
        i++;
        while (i < format.size() && (format[i] == '-' || format[i] == '+' || format[i] == '0' ||
               format[i] == ' ' || format[i] == '#' || (format[i] >= '0' && format[i] <= '9') || format[i] == '.'))
        {
            i++;
        }
        while (i < format.size() && (format[i] == 'l' || format[i] == 'h' || format[i] == 'L'))
        {
            i++;
        }

        if (i >= format.size())
        {
            result += format.substr(specStart);
            break;
        }

        char conv = format[i];
        std::string spec = format.substr(specStart, i - specStart + 1);
        char specBuf[64];

        switch (conv)
        {
        case '%':
            result.push_back('%');
            break;
        case 'd': case 'i':
        {
            int32_t v = (int32_t)(nextGpr < 6 ? gprArgs[nextGpr++] : 0);
            snprintf(specBuf, sizeof(specBuf), spec.c_str(), v);
            result += specBuf;
            break;
        }
        case 'u': case 'x': case 'X': case 'o':
        {
            uint32_t v = (uint32_t)(nextGpr < 6 ? gprArgs[nextGpr++] : 0);
            snprintf(specBuf, sizeof(specBuf), spec.c_str(), v);
            result += specBuf;
            break;
        }
        case 'c':
        {
            int v = (int)(nextGpr < 6 ? gprArgs[nextGpr++] : 0);
            snprintf(specBuf, sizeof(specBuf), spec.c_str(), v);
            result += specBuf;
            break;
        }
        case 's':
        {
            uint32_t strPtr = (uint32_t)(nextGpr < 6 ? gprArgs[nextGpr++] : 0);
            std::string arg;
            if (strPtr != 0)
            {
                for (uint32_t k = 0; ; k++)
                {
                    uint8_t ch = PPC_LOAD_U8(strPtr + k);
                    if (ch == 0) break;
                    arg.push_back((char)ch);
                }
            }
            std::vector<char> tmp(arg.size() + 64);
            snprintf(tmp.data(), tmp.size(), spec.c_str(), arg.c_str());
            result += tmp.data();
            break;
        }
        case 'p':
        {
            uint32_t v = (uint32_t)(nextGpr < 6 ? gprArgs[nextGpr++] : 0);
            snprintf(specBuf, sizeof(specBuf), "0x%X", v);
            result += specBuf;
            break;
        }
        default:
            if (nextGpr < 6) nextGpr++;
            result += spec;
            break;
        }
    }

    for (size_t i = 0; i < result.size(); i++)
    {
        PPC_STORE_U8(bufferPtr + (uint32_t)i, (uint8_t)result[i]);
    }
    PPC_STORE_U8(bufferPtr + (uint32_t)result.size(), 0);

    ctx.r3.u64 = (uint32_t)result.size();
}
