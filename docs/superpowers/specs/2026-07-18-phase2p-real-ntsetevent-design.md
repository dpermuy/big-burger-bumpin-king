# Phase 2P: Real NtSetEvent — Big Bumpin' Recomp

## Context

Phase 2N gave `NtWaitForSingleObjectEx` real blocking semantics tied to `g_handleTable`'s
`signaled` field, and confirmed the specific busy-poll bug it targeted is fixed. Phase 2O's
investigation into the resulting (different, deeper) stall found: 5+ worker threads all
genuinely block on `NtWaitForSingleObjectEx(handle=0x1003)` — an Event-type handle
(confirmed via `g_handleTable`'s tracked `type` field) that is never signaled anywhere in
the run.

Confirmed via direct runtime evidence (temporary logging, reverted):
- `NtSetEvent` (`host/kernel_stubs.cpp:341-345`) is called exactly once in the whole run,
  targeting handle `0x1009` -- a different handle than the one 5+ threads are stuck
  waiting on (`0x1003`). It is still a fully generic auto-generated stub: it logs and
  returns success without touching `g_handleTable` at all.
- `KeSetEvent` (`host/kernel_impl.cpp:680-685`, already a no-op from Phase 2J) was ruled
  out as the mechanism for signaling `0x1003`: its `r3` argument, observed at runtime, is
  a raw guest memory address (`0xFFFFFFFF82673A78`), not a `g_handleTable` handle --
  `KeSetEvent` and `NtCreateEvent`/`NtWaitForSingleObjectEx` operate on architecturally
  separate object namespaces (direct kernel-object pointers vs. this project's own
  synthetic handle-table integers). `KeSetEvent` cannot be the fix for a handle-table-based
  wait regardless of further changes.

This is the same shape as every fix in this arc: a "no-op / always succeeds" stub that
silently drops real signal state, exposed now that waits are genuinely blocking (Phase
2N). `NtSetEvent` needs the same treatment `NtReleaseMutant` already got (Phase 2C/2F) --
mark the handle signaled and wake waiters.

## Goal

Give `NtSetEvent` real behavior: mark the target handle's `signaled` state and wake any
threads blocked waiting on it, symmetric with the existing `NtReleaseMutant` pattern.

## Design

Move `NtSetEvent` out of `host/kernel_stubs.cpp` (delete it from
`host/kernel_imports.txt`, regenerate) and implement it for real in
`host/kernel_impl.cpp`, directly mirroring `NtReleaseMutant`
(`host/kernel_impl.cpp:195-215`):

```cpp
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
```

`r3` = `EventHandle`, `r4` = optional `PLONG PreviousState` out-pointer (real NT
signature: `NtSetEvent(HANDLE EventHandle, PLONG PreviousState OPTIONAL)`). `r5`/`r6`,
observed as non-zero in the one confirmed call (`r5=0x1, r6=0x0`), aren't part of the real
signature -- treated as leftover register content, same "don't invent meaning for
unevidenced registers" posture used throughout this project (e.g. Phase 2E's
`MmAllocatePhysicalMemoryEx`).

## Success Criteria

1. `NtSetEvent` implemented for real in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. Re-run `private/default.xex` with an adequate watchdog and report honestly what
   happens: whether handle `0x1009`'s waiter(s) unblock, whether that cascades into any
   further progress (possibly including handle `0x1003`, possibly not -- no assumption
   either way), or whether the stall persists unchanged. All three are valid, reportable
   outcomes.
4. If new territory is reached, capture the fresh run log and a distinct stub-hit list,
   per the established phase pattern.

## Explicitly Out of Scope

- Any fix for handle `0x1003` specifically beyond what naturally falls out of fixing
  `NtSetEvent` -- no evidence yet of what's actually supposed to signal it. If it's still
  unsignaled after this phase, that's a new, separate investigation, not a regression.
- `KeSetEvent`'s raw-pointer-based object namespace -- confirmed architecturally distinct
  from this fix, no evidence yet that anything depends on it doing more than its current
  no-op.
- `NtClearEvent` / any other event-family function still on `host/kernel_imports.txt` --
  not observed hit yet.
