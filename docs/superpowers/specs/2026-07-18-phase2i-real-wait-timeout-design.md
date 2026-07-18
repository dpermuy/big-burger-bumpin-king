# Phase 2I: Real Wait Timeout Semantics — Big Bumpin' Recomp

## Context

Fixing `ExCreateThread`'s calling convention bug (found and fixed immediately after Phase
2H closed — see the `2ed66d1` commit) unblocked dramatically more execution: the game
reaches real graphics subsystem calls for the first time in this project
(`VdInitializeEngines`, `VdSwap`, `VdSetDisplayMode`, `XGetVideoMode`, and 31 other newly-
hit imports, recorded in `docs/superpowers/specs/phase2h-postfix-stub-hits.txt`). But the
run now hits the watchdog timeout instead of a crash: `KeWaitForSingleObject`/
`KeResetEvent` spin roughly 2.86 million times on the same event address before the 10
second watchdog fires.

Investigated with the same discipline as every prior register-mapping/behavior question in
this project — not guessed. Two pieces of evidence, both from reading real generated code
and captured runtime values, not inference:

1. **The calling loop's logic** (`sub_820C8D28` in `private/ppc/ppc_recomp.6.cpp`,
   identified via the link register captured at the `KeWaitForSingleObject`/`KeResetEvent`
   call sites): the loop calls `KeWaitForSingleObject`, then checks
   `if (r3 == 258) goto <timeout path>` — 258 is the real `STATUS_TIMEOUT` NTSTATUS value.
   This is a genuine, intentional "wait with timeout, do periodic work either way" pattern
   — not a bug in the game's own code.
2. **The actual timeout value**: the 5th argument (`r7`, a pointer to a 64-bit 100ns-tick
   count — matching the real `KeWaitForSingleObject(Object, WaitReason, WaitMode,
   Alertable, Timeout)` signature) was captured directly: `-300000`, i.e. -30,000,000ns =
   **-30ms** (negative = relative time, the standard NT convention). This is a legitimate,
   bounded 30ms poll — not an infinite wait, and not a bug in the request itself.

Every prior phase's "wait resolves instantly" implementations (`NtWaitForSingleObjectEx`,
Phase 2C; the current `KeWaitForSingleObject` stub) were justified specifically because
nothing could run concurrently to change state during a wait — true before Phase 2H, false
now. `KeWaitForSingleObject` always returning immediate "success" means the calling loop
never takes its designed timeout branch, finds its exit condition still unmet (nothing in
the system sets it), and loops back immediately — at unthrottled CPU speed instead of the
intended ~33Hz poll rate.

## Goal

Give `KeWaitForSingleObject` real timeout semantics: honor a finite relative timeout by
actually waiting approximately that long, then reporting `STATUS_TIMEOUT`, so the game's
own polling loops run at their designed pace instead of spinning.

## Design

`KeWaitForSingleObject(r3=Object, r4=WaitReason, r5=WaitMode, r6=Alertable, r7=TimeoutPtr)`:

- If `TimeoutPtr` (`r7`) is non-null, read the 64-bit value it points to.
  - If **negative** (relative timeout — the only case observed so far): convert the
    100ns-tick magnitude to milliseconds and `std::this_thread::sleep_for` that duration,
    then return `258` (`STATUS_TIMEOUT`) in `ctx.r3`.
  - If **positive** (absolute time) or the pointer is **null** (wait forever): keep the
    existing "succeed immediately" behavior. Neither case has been observed yet: building
    real handling for them now would be speculative in exactly the way this project has
    consistently avoided (see e.g. Phase 2B's critical-section design, Phase 2E's
    conservative `MmQueryStatistics`). Revisit if a future run's evidence shows either
    case actually occurring.
- No change to the object's real signaled state — this project still has no general
  mechanism to know whether a given dispatcher object (most of which, like this one, are
  raw embedded structures the game manages itself, not handles from `g_handleTable`) is
  genuinely signaled. The fix is about *pacing*, not about correctly modeling every
  object's real synchronization state.
- Scoped to `KeWaitForSingleObject` only. `NtWaitForSingleObjectEx` (Phase 2C) and other
  "always instant success" wait-family functions keep their existing behavior — they were
  justified by their own, different evidence (handle-based synchronization where nothing
  else could ever change state) and no evidence yet shows them causing an equivalent spin.

## Success Criteria

Phase 2I is complete when:

1. `KeWaitForSingleObject` is updated in `host/kernel_impl.cpp` (moved out of
   `host/kernel_stubs.cpp`, following the established per-phase pattern) with the timeout
   handling above.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than the post-2H-fix run or fails at a new,
   demonstrably different point, without the multi-million-line spin.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- Absolute-timeout and wait-forever (`TimeoutPtr == NULL`) handling for
  `KeWaitForSingleObject` — no evidence either occurs yet.
- Real signaled-state tracking for embedded (non-handle) dispatcher objects.
- Any change to `NtWaitForSingleObjectEx` or other wait-family functions — no evidence any
  of them need to change.
- `KeWaitForMultipleObjects` — a different function, no evidence yet it's hit or behaves
  the same way.
