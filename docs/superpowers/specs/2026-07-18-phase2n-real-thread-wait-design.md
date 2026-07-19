# Phase 2N: Real NtWaitForSingleObjectEx Thread-Wait Semantics — Big Bumpin' Recomp

## Context

Phase 2L's `KeQueryPerformanceFrequency` fix let execution reach new territory, but a
fresh stall appeared shortly after: a single host thread was measured calling
`sub_820B9A58` → `NtWaitForSingleObjectEx(handle=0x1006)` over 3 million times in under
25 real seconds (Phase 2M investigation, confirmed via entry/exit call-count
instrumentation). Handle `0x1006` is a thread handle returned by an earlier
`ExCreateThread` call — this is a wait-for-thread-completion ("join") pattern.

`NtWaitForSingleObjectEx` (`host/kernel_impl.cpp:214-221`) always returns success
immediately, by design, dating from before real threading existed (its own comment says
so: "correct... under a single-execution-context model... Revisit once ExCreateThread
spawns real host threads" — Phase 2H already did, this was never revisited). The caller
sees an immediate "success," rechecks the thread's actual status through some other means,
finds it hasn't really finished, and loops — burning full CPU with zero real blocking.

This is the same root-cause shape as the Phase 2K critical-section fix and the Phase 2L
`KeQueryPerformanceFrequency` fix: a stub that "always succeeds" instead of reflecting
real object state, now exposed because real concurrent threading (Phase 2H) makes the gap
observable.

The existing `g_handleTable` (`host/kernel_impl.cpp:151-158`) already tracks a `signaled`
bool per handle, used by `NtCreateMutant`/`NtCreateEvent`/`NtReleaseMutant`. Nothing
currently sets a **Thread**-type handle's `signaled` field when the corresponding thread
actually finishes — that piece doesn't exist yet.

## Goal

Give `NtWaitForSingleObjectEx` real blocking semantics based on the handle's actual
`signaled` state, and make Thread handles actually become signaled when their thread
finishes, so a wait-for-thread-completion loop genuinely blocks instead of spinning.

## Design

### 1. A condition variable for handle-state changes

Add `std::condition_variable g_handleSignalCv;` alongside the existing `g_handleTable`
and `g_stateMutex`. One coarse condition variable notified on every handle-state change,
matching this file's existing "single coarse lock, correctness over throughput" posture
(`host/kernel_impl.cpp:14-19`) — not worth fine-grained per-handle condition variables at
this stage.

### 2. Marking Thread handles signaled on completion

Xbox 360 threads created via `ExCreateThread` exit through one of two different paths in
this codebase's own generated code, and both need to mark the handle signaled:

- **Trampoline path** (`XApiThreadStartup`, `sub_820ABCE8` — the majority case observed so
  far): this generated code always calls `ExTerminateThread` as its last action,
  regardless of whether the guest thread function returned normally or terminated early
  (confirmed in Phase 2H/2J investigation by reading `sub_820ABCE8` directly).
  `ExTerminateThread` (`host/kernel_impl.cpp:689-693`) then calls `pthread_exit`, which
  never returns — so any code after the `PPC_LOOKUP_FUNC` call inside `ExCreateThread`'s
  lambda (`host/kernel_impl.cpp:545-560`) never executes for this path. `ExTerminateThread`
  itself must therefore mark the handle signaled before exiting.

  This requires `ExTerminateThread` to know which handle belongs to the calling thread.
  Add `static thread_local uint32_t g_currentThreadHandle = 0;`, set at the very top of
  `ExCreateThread`'s lambda (before invoking guest code), so `ExTerminateThread` can look
  it up.

- **Direct-entry path** (no trampoline, `viaTrampoline == false`): guest code is called
  directly and, per real observed behavior, simply returns without ever calling
  `ExTerminateThread`. The lambda itself must mark the handle signaled immediately after
  `(PPC_LOOKUP_FUNC(base, entryAddress))(threadCtx, base)` returns.

  Both signaling points are harmless to hit redundantly (marking an already-signaled
  handle again is a no-op), so no coordination between them is needed.

### 3. Real NtWaitForSingleObjectEx

Confirmed real calling convention via `sub_820AB238`'s call site (Phase 2M investigation):
`r3` = Handle, `r4` = WaitMode (ignored — no distinct kernel/user-mode wait behavior to
emulate), `r5` = Alertable (ignored — see Out of Scope), `r6` = `PLARGE_INTEGER Timeout`.

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
            // Same relative-timeout convention established for KeWaitForSingleObject
            // (Phase 2I): negative = relative 100ns ticks.
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

This uniformly serves every handle type already tracked in `g_handleTable` (Mutant,
Event, Generic, Thread) — no type-specific branching needed, since they all already
funnel through the same `signaled` field.

## Explicitly Out of Scope

- **Mutant auto-reset-on-successful-wait semantics.** Real Xbox 360 mutant handles
  re-lock (clear `signaled`) for the waiting thread on successful acquire. No evidence
  yet of contested-mutant usage — this investigation's evidence is specifically a
  thread-join wait. Not implementing unverified behavior; revisit if a future run shows a
  mutant being waited on by multiple threads expecting exclusive hand-off.
- **Alertable waits / APC delivery (`r5`, and `STATUS_ALERTED`/257 handling).** This
  project doesn't emulate APCs (matching the existing `KeEnterCriticalRegion`/
  `KeLeaveCriticalRegion` no-op precedent from Phase 2J). No evidence any observed wait
  loop actually depends on receiving 257.
- **Positive (absolute-time) `Timeout` values.** Same precedent as Phase 2I's
  `KeWaitForSingleObject` — not observed yet, falls through to unbounded wait rather than
  guessing at semantics.
- **`KeWaitForMultipleObjects`** or any other wait-family function — still generic stubs,
  no evidence yet they're hit.
