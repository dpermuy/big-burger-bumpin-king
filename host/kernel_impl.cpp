#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>

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

enum class HandleObjectType { Mutant, Event, Generic, Thread };

struct HandleObject
{
    HandleObjectType type;
    bool signaled;
};

static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels
static std::condition_variable g_handleSignalCv;

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

PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    // Always succeeds immediately -- correct, not just convenient, under a
    // single-execution-context model: nothing else can run concurrently to
    // change an object's state while we'd otherwise block. Revisit once
    // ExCreateThread spawns real host threads.
    ctx.r3.u64 = 0;
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

PPC_FUNC(__imp__NtCreateFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r6.u64;

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, 0);
    }
    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusNoSuchDevice);
        PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
    }

    fmt::println("[kernel] NtCreateFile: reporting STATUS_NO_SUCH_DEVICE (no virtual hard disk backing)");
    ctx.r3.u64 = kStatusNoSuchDevice;
}

PPC_FUNC(__imp__NtOpenFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, 0);
    }

    fmt::println("[kernel] NtOpenFile: reporting STATUS_NO_SUCH_DEVICE (no virtual hard disk backing)");
    ctx.r3.u64 = kStatusNoSuchDevice;
}

PPC_FUNC(__imp__NtReadFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
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
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        stackBase = g_bumpAllocatorNext;
        g_bumpAllocatorNext += kStackSize;

        handle = g_nextHandle++;
        g_handleTable[handle] = HandleObject{ HandleObjectType::Thread, false };

        threadId = g_nextThreadId++;
    }

    fmt::println("[kernel] ExCreateThread: entry=0x{:X} (via {}) stack=0x{:X}..0x{:X} handle=0x{:X}",
        entryAddress, xApiThreadStartup != 0 ? "XApiThreadStartup" : "StartAddress directly",
        stackBase, stackBase + kStackSize, handle);

    // XApiThreadStartup's calling convention (confirmed by reading its generated
    // code, sub_820ABCE8 in private/ppc/ppc_recomp.2.cpp): r3=StartAddress,
    // r4=StartContext -- it saves r3 into a non-volatile register, later moves
    // it into ctr, and does an indirect call (bctrl) passing StartContext (from
    // r4) in r3. Calling StartAddress directly (no trampoline) uses the plain
    // thread-entry convention instead: r3=StartContext only.
    bool viaTrampoline = xApiThreadStartup != 0;
    std::thread hostThread([entryAddress, stackBase, kStackSize, startContext, startAddress, viaTrampoline, base, handle]()
    {
        g_currentThreadHandle = handle;

        PPCContext threadCtx{};
        threadCtx.r1.u64 = stackBase + kStackSize - 0x10;
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

PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    // No-op.
}

PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    // No-op -- not parsing real GPU commands yet.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    // No-op.
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

PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    // No confirmed struct/pointer-pair layout; return success without
    // writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdSwap)
{
    // The frame-present call. No-op -- nothing to actually present, no
    // renderer exists yet.
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
