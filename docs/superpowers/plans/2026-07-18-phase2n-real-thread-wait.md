# Phase 2N: Real NtWaitForSingleObjectEx Thread-Wait Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `NtWaitForSingleObjectEx` block on a handle's real `signaled` state instead
of always succeeding instantly, and make Thread handles actually become signaled when
their thread finishes — resolving the 3-million-iteration busy-poll found in the Phase 2M
investigation.

**Architecture:** Single-file change to `host/kernel_impl.cpp`. A new
`std::condition_variable g_handleSignalCv` shared by all handle types already tracked in
`g_handleTable`. Two new signaling points inside `ExCreateThread`'s lambda / a new
`thread_local` in `ExTerminateThread`, covering both real thread-exit paths this codebase
has observed. `NtWaitForSingleObjectEx` rewritten to actually wait on the condition
variable, with the same relative-timeout convention already established for
`KeWaitForSingleObject` (Phase 2I).

**Tech Stack:** C++17 `<condition_variable>` (new), existing `<mutex>`/`<chrono>`/
`<thread>`.

## Global Constraints

- `g_handleSignalCv` is one coarse condition variable for all handle-state changes,
  matching this file's existing single-coarse-lock posture (`host/kernel_impl.cpp:14-19`)
  — not per-handle condition variables.
- Only Thread-handle signaling and `NtWaitForSingleObjectEx` change. No Mutant auto-reset,
  no alertable-wait/APC handling, no absolute-timeout handling — all explicitly out of
  scope per the spec.

---

## Task 1: Add the Condition Variable and Thread-Completion Signaling

**Files:**
- Modify: `host/kernel_impl.cpp:1-13` (includes)
- Modify: `host/kernel_impl.cpp:151-158` (near `g_handleTable` declaration)
- Modify: `host/kernel_impl.cpp:509-575` (`ExCreateThread`)
- Modify: `host/kernel_impl.cpp:689-693` (`ExTerminateThread`)

**Interfaces:**
- Produces: `g_handleSignalCv` (condition variable) and `g_currentThreadHandle`
  (thread_local), both consumed by Task 2's `NtWaitForSingleObjectEx` rewrite.

- [ ] **Step 1: Add the `<condition_variable>` include**

In `host/kernel_impl.cpp`, add to the include block (alongside the existing `<chrono>`,
`<mutex>`, etc.):
```cpp
#include <condition_variable>
```

- [ ] **Step 2: Add the condition variable next to `g_handleTable`**

Find:
```cpp
static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels
```
Replace with:
```cpp
static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels
static std::condition_variable g_handleSignalCv;
```

- [ ] **Step 3: Add the thread_local current-handle tracker**

Add near the top of the file, after the `g_stateMutex` declaration:
```cpp
static thread_local uint32_t g_currentThreadHandle = 0; // this thread's own ExCreateThread handle, 0 if not a guest-spawned thread
```

- [ ] **Step 4: Set the thread_local at the top of `ExCreateThread`'s lambda, and signal on direct-entry return**

Find the lambda inside `ExCreateThread`:
```cpp
    std::thread hostThread([entryAddress, stackBase, kStackSize, startContext, startAddress, viaTrampoline, base]()
    {
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
    });
```
Replace with:
```cpp
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
```

- [ ] **Step 5: Signal in `ExTerminateThread`**

Find:
```cpp
PPC_FUNC(__imp__ExTerminateThread)
{
    fmt::println("[kernel] ExTerminateThread: exitCode=0x{:X} -- terminating this thread", ctx.r3.u64);
    pthread_exit(nullptr);
}
```
Replace with:
```cpp
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
```

- [ ] **Step 6: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2n_task1_build.log
grep -c "error:" /tmp/phase2n_task1_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 7: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Add thread-completion signaling for real NtWaitForSingleObjectEx"
```

---

## Task 2: Rewrite NtWaitForSingleObjectEx With Real Blocking Semantics

**Files:**
- Modify: `host/kernel_impl.cpp:214-221`

**Interfaces:**
- Consumes: `g_handleSignalCv`, `g_currentThreadHandle` (Task 1).
- Produces: nothing consumed by other tasks — leaf function.

- [ ] **Step 1: Replace the always-succeeds stub**

Find:
```cpp
PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    // Always succeeds immediately -- correct, not just convenient, under a
    // single-execution-context model: nothing else can run concurrently to
    // change an object's state while we'd otherwise block. Revisit once
    // ExCreateThread spawns real host threads.
    ctx.r3.u64 = 0;
}
```
Replace with:
```cpp
PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t timeoutPtr = (uint32_t)ctx.r6.u64;

    std::unique_lock<std::mutex> lock(g_stateMutex);
    auto it = g_handleTable.find(handle);
    if (it == g_handleTable.end())
    {
        fmt::println("[kernel] NtWaitForSingleObjectEx: unknown handle 0x{:X}", handle);
        ctx.r3.u64 = 0xC0000008; // STATUS_INVALID_HANDLE
        return;
    }

    if (timeoutPtr != 0)
    {
        int64_t timeoutValue = (int64_t)PPC_LOAD_U64(timeoutPtr);
        if (timeoutValue < 0)
        {
            // Same relative-timeout convention established for
            // KeWaitForSingleObject (Phase 2I): negative = relative 100ns ticks.
            auto durationMs = std::chrono::milliseconds(-timeoutValue / 10000);
            bool signaled = g_handleSignalCv.wait_for(lock, durationMs,
                [&] { return g_handleTable[handle].signaled; });
            ctx.r3.u64 = signaled ? 0 : 258; // STATUS_SUCCESS or STATUS_TIMEOUT
            return;
        }
        // Positive (absolute time) -- not observed yet, fall through to the
        // unbounded wait rather than guess at handling.
    }

    g_handleSignalCv.wait(lock, [&] { return g_handleTable[handle].signaled; });
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}
```

- [ ] **Step 2: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2n_task2_build.log
grep -c "error:" /tmp/phase2n_task2_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 3: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Give NtWaitForSingleObjectEx real blocking wait semantics"
```

---

## Task 3: Verify Against the Original Repro

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase2n_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2n-stub-hits.txt` (committed, only if new territory
  is reached — see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Tasks 1-2), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2n-stub-hits.txt` (conditional).

- [ ] **Step 1: Temporarily raise the watchdog to 25 seconds**

Modify `host/main.cpp`, changing:
```cpp
    auto status = future.wait_for(std::chrono::seconds(10));
```
to:
```cpp
    auto status = future.wait_for(std::chrono::seconds(25));
```

- [ ] **Step 2: Rebuild and run**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tail -5
./build/BigBumpinHost private/default.xex > private/phase2n_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check outcome and report honestly**

Run: `tail -20 private/phase2n_run.log; wc -l private/phase2n_run.log`

Report exactly what happened -- three possible honest outcomes, do not assume success:
- **New territory reached**: log shows activity past the old freeze point (the last
  `KfReleaseSpinLock` line from the Phase 2L run), or a clean return, or a new distinct
  failure. This is the expected outcome. Continue to Step 4.
- **Still stuck at the same point**: identical final state to the pre-fix run. The fix
  didn't resolve it as expected -- stop, report the exact log tail, do not proceed to
  Step 4/5's "new territory" assumptions.
- **Millions of log lines again** (a different busy-poll): also valid evidence of a
  different remaining bug -- report the line count and what's looping.

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

Do this regardless of Step 3's outcome.

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2n_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2n-stub-hits.txt; wc -l docs/superpowers/specs/phase2n-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2n-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2N verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body. This closes Phase 2N.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements spec sections 1 and 2 (condition variable,
  both thread-completion signaling paths). Task 2 implements spec section 3
  (real blocking `NtWaitForSingleObjectEx`). Task 3 covers verification against the
  actual Phase 2M repro, honestly reporting the outcome rather than assuming success.
- **Placeholder scan:** No TBD/TODO. Step 3's three outcomes are each given concrete,
  explicit handling.
- **Type/interface consistency:** `g_handleSignalCv` and `g_currentThreadHandle` are
  declared once in Task 1 and used identically in Task 2 -- same names, same types
  (`std::condition_variable`, `thread_local uint32_t`).
