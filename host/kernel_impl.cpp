#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

#include <cstdint>
#include <cstdlib>
#include <unordered_map>

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

struct CriticalSectionState
{
    int32_t recursionCount = 0;
    uint32_t owningThread = 0;
};

static std::unordered_map<uint32_t, CriticalSectionState> g_criticalSections;

PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    g_criticalSections[csAddr] = CriticalSectionState{};
}

PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount++;
    state.owningThread = 1; // single logical execution context; no real guest thread ID yet
}

PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount--;
    if (state.recursionCount <= 0)
    {
        state.owningThread = 0;
    }
}

static uint64_t g_tlsSlots[64] = {};
static int g_tlsNextSlot = 0;

PPC_FUNC(__imp__KeTlsAlloc)
{
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

    uint32_t allocatedAddress = g_bumpAllocatorNext;
    g_bumpAllocatorNext += alignedSize;

    PPC_STORE_U32(baseAddressPtr, allocatedAddress);
    PPC_STORE_U32(regionSizePtr, alignedSize);

    fmt::println("[kernel] NtAllocateVirtualMemory: {} bytes -> 0x{:X}", alignedSize, allocatedAddress);

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

enum class HandleObjectType { Mutant, Event };

struct HandleObject
{
    HandleObjectType type;
    bool signaled;
};

static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels

PPC_FUNC(__imp__NtCreateMutant)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialOwner = ctx.r5.u64 != 0;

    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Mutant, !initialOwner };

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtCreateEvent)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialState = ctx.r6.u64 != 0;

    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Event, initialState };

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtReleaseMutant)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t previousCountOutPtr = (uint32_t)ctx.r4.u64;

    auto it = g_handleTable.find(handle);
    if (it != g_handleTable.end())
    {
        it->second.signaled = true;
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
    g_handleTable.erase(handle);
    ctx.r3.u64 = 0;
}
