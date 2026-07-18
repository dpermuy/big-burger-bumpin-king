# Phase 2H: Real Thread Spawning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `ExCreateThread` spawn a real host thread executing the requested guest entry point, with thread-safety retrofitted into every piece of shared state built up since Phase 2B, so a fresh `BigBumpinHost` run either progresses past Phase 2G's watchdog timeout or fails at a new, informative point.

**Architecture:** Two sequential changes to `host/kernel_impl.cpp`: first retrofit a single coarse `g_stateMutex` around all existing shared global state (and convert TLS storage to `thread_local`) so it's safe under real concurrency, verified to still build and behave identically single-threaded; then add the real `ExCreateThread` implementation on top of that now-safe foundation. `CREATE_SUSPENDED` is not honored (see spec's design rationale — no confirmed evidence a real per-thread resume call exists) and `KeResumeThread` is unchanged from Phase 2G.

**Tech Stack:** C++17 (`<mutex>`, `<thread>`), same toolchain as prior phases.

## Global Constraints

- `CREATE_SUSPENDED` (`ExCreateThread`'s `r9` bit 0) is NOT honored — spawned threads always run immediately. `KeResumeThread` stays exactly as Phase 2G left it (a no-op).
- Real register mapping for `ExCreateThread` (verified by instrumented capture, not assumed): `r3`=Handle out-pointer, `r4`=StackSize (ignored — always allocates a fixed 256 KiB/`0x40000` stack), `r5`=ThreadId out-pointer, `r6`=XApiThreadStartup, `r7`=StartAddress (the real entry point — **not** `r6`, correcting Phase 2A's original assumption), `r8`=StartContext, `r9`=CreationFlags (only bit 0 inspected, and even that's ignored per above — processor-affinity high bits ignored entirely).
- Entry point resolution: call `XApiThreadStartup` (`r6`) if non-zero, otherwise call `StartAddress` (`r7`) directly.
- Spawned threads are `.detach()`ed immediately — never stored in a long-lived container, since a live, never-joined `std::thread` calls `std::terminate()` at process exit if still running.
- `g_stateMutex` is a single coarse lock protecting `g_criticalSections`, `g_tlsNextSlot` (not `g_tlsSlots` — see below), `g_bumpAllocatorNext`, `g_handleTable`, and `g_nextHandle`.
- `g_tlsSlots` changes from `static uint64_t g_tlsSlots[64]` to `static thread_local uint64_t g_tlsSlots[64]` — real per-thread storage via the language, no mutex needed for it specifically (no cross-thread access is possible once it's `thread_local`).

---

## Task 1: Thread-Safety Retrofit

**Files:**
- Modify: `host/kernel_impl.cpp`

**Interfaces:**
- Produces: `g_stateMutex`, usable by Task 2's `ExCreateThread`.
- Consumes: nothing new — this task only adds locking around state every function in this file already declared in Phases 2B-2G.

- [ ] **Step 1: Add includes and the mutex declaration**

Modify `host/kernel_impl.cpp`, changing:
```cpp
#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
```
to:
```cpp
#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <thread>
#include <unordered_map>

// Protects every piece of shared state below (handle table, critical
// sections, bump allocator) now that ExCreateThread (Task 2) spawns real
// host threads. Single coarse lock -- correctness over throughput at this
// stage; fine-grained locking would introduce lock-ordering bugs this
// project has no way to test for yet.
static std::mutex g_stateMutex;
```

- [ ] **Step 2: Lock the critical-section functions**

Modify `host/kernel_impl.cpp`, changing:
```cpp
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
```
to:
```cpp
PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    g_criticalSections[csAddr] = CriticalSectionState{};
}

PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount++;
    state.owningThread = 1; // real threads exist now, but no per-thread ID scheme yet -- still a placeholder, not a real thread identifier
}

PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount--;
    if (state.recursionCount <= 0)
    {
        state.owningThread = 0;
    }
}
```

- [ ] **Step 3: Make TLS storage per-thread and lock the slot allocator**

Modify `host/kernel_impl.cpp`, changing:
```cpp
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
```
to:
```cpp
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
```

`KeTlsSetValue` and `KeTlsGetValue` need no changes — `g_tlsSlots` being `thread_local` means no cross-thread access is possible, so no lock is needed for either.

- [ ] **Step 4: Lock the bump allocator in both its consumers**

Modify `host/kernel_impl.cpp`, changing:
```cpp
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
```
to:
```cpp
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
```

And changing:
```cpp
PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx)
{
    // r4 (observed 0x19000000, ~400 MB) is implausibly large for a single
    // physical allocation on 512 MB hardware -- treated as stale register
    // content, not a real size argument. Always allocates a fixed 64 KiB
    // block from the same bump allocator NtAllocateVirtualMemory (Phase 2B)
    // uses, rather than guessing which register (if any) is trustworthy.
    constexpr uint32_t kAllocGranularity = 0x10000;

    uint32_t allocatedAddress = g_bumpAllocatorNext;
    g_bumpAllocatorNext += kAllocGranularity;

    fmt::println("[kernel] MmAllocatePhysicalMemoryEx: {} bytes -> 0x{:X}", kAllocGranularity, allocatedAddress);

    ctx.r3.u64 = allocatedAddress; // Mm* functions return the address directly, not a status code
}
```
to:
```cpp
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
```

- [ ] **Step 5: Lock the handle-table functions**

Modify `host/kernel_impl.cpp`, changing:
```cpp
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
```
to:
```cpp
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
```

(`NtWaitForSingleObjectEx` is unchanged in this step — it doesn't touch `g_handleTable` at all, listed here only to show it's still adjacent in the file. Its "revisit once ExCreateThread spawns real host threads" comment is addressed by Task 2 existing at all, not by a change to this specific function — no evidence yet that any wait needs to become a real blocking wait.)

- [ ] **Step 6: Lock the last handle-table consumer**

Modify `host/kernel_impl.cpp`, changing:
```cpp
PPC_FUNC(__imp__XamNotifyCreateListener)
{
    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Generic, false };
    ctx.r3.u64 = handle;
}
```
to:
```cpp
PPC_FUNC(__imp__XamNotifyCreateListener)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Generic, false };
    ctx.r3.u64 = handle;
}
```

- [ ] **Step 7: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2h_task1_build.log
grep -c "error:" /tmp/phase2h_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`.

- [ ] **Step 8: Run and confirm no behavior change**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | diff - private/phase2g_run.log && echo IDENTICAL`
Expected: `IDENTICAL` — this retrofit is purely mechanical (locking access to state, changing TLS storage class), no logic changed, so single-threaded output must be byte-for-byte identical to Phase 2G's run. If it's not, something in this step introduced an unintended behavior change — stop and investigate before proceeding to Task 2, don't layer new thread-spawning logic on top of an unverified foundation.

- [ ] **Step 9: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Retrofit thread-safety: coarse mutex + thread_local TLS"
```

---

## Task 2: Real ExCreateThread

**Files:**
- Modify: `host/kernel_impl.cpp`

**Interfaces:**
- Consumes: `g_stateMutex`, `g_bumpAllocatorNext`, `g_handleTable`, `g_nextHandle` (Task 1), `PPC_LOOKUP_FUNC` (`thirdparty/XenonRecomp/XenonUtils/ppc_context.h`, verified in Phase 2A's plan).
- Produces: nothing consumed by other tasks — this closes out the kernel-import side of Phase 2H.

- [ ] **Step 1: Add the Thread handle type**

Modify `host/kernel_impl.cpp`, changing:
```cpp
enum class HandleObjectType { Mutant, Event, Generic };
```
to:
```cpp
enum class HandleObjectType { Mutant, Event, Generic, Thread };
```

- [ ] **Step 2: Replace the ExCreateThread stub removal — first regenerate kernel_stubs.cpp**

Edit `host/kernel_imports.txt`, delete the line `ExCreateThread` (the only remaining line — this file will be empty after this edit, which is correct: 209 of 210 imports are now implemented).

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 0 stub functions to host/kernel_stubs.cpp`

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `0`

- [ ] **Step 3: Implement real ExCreateThread**

Append to `host/kernel_impl.cpp`:

```cpp
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

    std::thread hostThread([entryAddress, stackBase, kStackSize, startContext, base]()
    {
        PPCContext threadCtx{};
        threadCtx.r1.u64 = stackBase + kStackSize - 0x10;
        threadCtx.r3.u64 = startContext;

        PPC_LOOKUP_FUNC(base, entryAddress)(threadCtx, base);
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
```

- [ ] **Step 4: Remove ExCreateThread from CMakeLists.txt's source list if referenced directly**

No action needed — `host/kernel_impl.cpp` is already a fixed source in the `BigBumpinHost` target (added in Phase 2A); this step only exists to confirm there's nothing else to wire up. Skip to Step 5.

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2h_task2_build.log
grep -c "error:" /tmp/phase2h_task2_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for — 0 in `kernel_stubs.cpp`, all 210 now in `kernel_impl.cpp`.

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement real ExCreateThread: spawns and detaches a real host thread"
```

---

## Task 3: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2h_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2h-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 2), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2h-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2h_run.log`
Expected: given all 210 imports are now implemented, this run cannot produce any more
`[stub]`-tagged lines by definition. It should progress further than Phase 2G's watchdog
timeout, or terminate differently (a crash from a genuine concurrency bug is plausible and
would itself be valuable, specific information, not a phase failure to hide) — record
whatever actually happens.

- [ ] **Step 2: Compare against Phase 2G's run**

Run: `diff private/phase2g_run.log private/phase2h_run.log`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2h_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2h-stub-hits.txt; wc -l docs/superpowers/specs/phase2h-stub-hits.txt`
Expected: `0` — by construction, no stubs remain (every kernel import is now implemented
in `host/kernel_impl.cpp`). If this is non-zero, something is wrong with the accounting
(e.g. a name mismatch between the removed import and its implementation) — investigate
before committing.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2h-stub-hits.txt
git commit -m "Record Phase 2H bring-up run: all kernel imports now implemented"
```

This closes Phase 2H — and the kernel-import bring-up effort that began in Phase 2A.
Whatever the run's new terminal outcome is becomes the starting point for the next
investigation (likely graphics device creation, given the boot sequence has now cleared
every kernel-level obstacle).

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's "Thread safety" section exactly (same mutex scope, same `thread_local` TLS change). Task 2 implements `ExCreateThread` per the spec's corrected register mapping and entry-resolution logic, including the simplified no-registry/detach-immediately approach from the spec's final revision. `CREATE_SUSPENDED` is correctly NOT implemented, matching the spec's explicit scope cut. Success criteria 1–2 map to Task 1/2's build and behavior-preservation checks. Criteria 3–4 map to Task 3.
- **Placeholder scan:** No TBD/TODO. Both mid-design corrections (dropping speculative `CREATE_SUSPENDED` handling, dropping the `GuestThread`/`g_threads` registry in favor of detach) are reflected consistently in both the spec and this plan, not left as a stale mismatch between the two documents.
- **Type/interface consistency:** `g_stateMutex` declared once (Task 1 Step 1), used identically by every subsequent lock site in both tasks. `HandleObjectType::Thread` added once (Task 2 Step 1), used once (Task 2 Step 3). The lambda in `ExCreateThread` captures exactly the variables it uses (`entryAddress`, `stackBase`, `kStackSize`, `startContext`, `base`) — verified against the body, not assumed.
