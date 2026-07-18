# Phase 2G: IRQL, Spinlocks, and Object References — Big Bumpin' Recomp

## Context

Phase 2F ([spec](2026-07-18-phase2f-thread-delay-tls-and-input-design.md)) implemented 5
of 6 hit imports and made real progress (23 → 48 log lines), reaching thread-creation and
IRQL/spinlock sequences before a watchdog timeout — a genuinely different outcome from
prior crashes.

Reading the log tail closely: lines 31-42 show a clean, legitimate spinlock sequence
(distinct lock addresses, real kernel work, not a spin), but after the last logged call
the run goes completely silent for the remaining watchdog budget. Since every stub in this
project logs unconditionally on every call, silence means execution moved into guest code
that never calls back into any kernel import — a raw memory-polling loop, not a kernel-
level wait this project could shortcut the way `NtWaitForSingleObjectEx` was. The most
likely cause: the main thread is spinning on a flag that a spawned worker thread (from
`ExCreateThread`, called 3 times in this run) would normally set — a flag that never gets
set because `ExCreateThread` still fakes success without actually running the requested
thread function.

Real thread spawning is a much larger architectural jump than any phase so far (a host
thread per guest thread, thread-safety for all existing shared state, per-thread TLS) and
is being deliberately split into its own future phase (2H) with its own investigation and
design cycle, rather than bundled here. This spec covers only the 11 *other* imports
`docs/superpowers/specs/phase2f-stub-hits.txt` recorded — all low-risk, matching
established patterns from prior phases. Implementing them won't get the run past the
suspected threading-related hang, but it's still real, evidence-grounded progress and
keeps the big architectural jump properly isolated.

## Goal

Implement the 11 non-`ExCreateThread` imports Phase 2F's run hit.

## Design

- **`KeRaiseIrqlToDpcLevel()` / `KfLowerIrql(newIrql)`** — no real interrupt/IRQL masking
  needed under this project's single-execution-context model.
  `KeRaiseIrqlToDpcLevel` returns `0` (a plausible old-IRQL value); `KfLowerIrql` ignores
  its argument entirely.
- **`KeAcquireSpinLockAtRaisedIrql(lockPtr)` / `KeReleaseSpinLockFromRaisedIrql(lockPtr)`**
  — pure no-ops. Same reasoning already applied to critical sections (Phase 2B): nothing
  can ever contend for a lock under single-execution-context, so acquire trivially
  "succeeds" and release does nothing.
- **`ObReferenceObjectByHandle(r3=handle, r4=desiredAccessOrType, r5=objectOutPtr)`** —
  this one needs real behavior, not a no-op. The log shows genuine data flow: the value
  this call writes to `*r5` (observed `0x900FF850`) is read back and reused as the "thread
  object" argument by the very next `KeSetBasePriorityThread`, `KeResumeThread`, and
  `ObDereferenceObject` calls. Write a fixed, consistent sentinel value
  (`0xDEAD0001`) to `*objectOutPtr` and return `STATUS_SUCCESS`. `handle=0` observed is
  consistent with a pseudo-handle convention (e.g. "current thread") rather than a real
  handle-table lookup — this implementation doesn't attempt to distinguish real vs.
  pseudo-handles, since nothing downstream needs that distinction yet.
- **`ObDereferenceObject(objectPtr)`** — no-op. No real reference counting exists; nothing
  observed depends on it being tracked.
- **`KeSetBasePriorityThread(threadObject, priority)`** — no-op. No real thread scheduling
  exists.
- **`KeResumeThread(threadObject)`** — returns `0` (previous suspend count — the most
  common/safe assumption for a thread object that was never really suspended).
- **`KeInitializeSemaphore(semaphorePtr, count, limit)`** — no-op. Nothing in the observed
  log waits on or releases this specific semaphore afterward; no state needs tracking yet.
- **`ExRegisterTitleTerminateNotification(...)`** — no-op. No evidence the registered
  notification ever needs to fire.
- **`XAudioRegisterRenderDriverClient(...)`** — returns `STATUS_SUCCESS` only, no real
  audio driver semantics attempted. Real audio is its own future phase — this just
  unblocks the boot sequence far enough to see what comes next, not a claim that audio
  works.

## Success Criteria

Phase 2G is complete when:

1. All 11 functions are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run is captured — expected to still end in the same watchdog timeout as Phase
   2F (these 11 functions aren't on the suspected hang path), but this must be confirmed
   by actually running it, not assumed. If the run instead progresses further or fails
   differently, that's valuable evidence the threading hypothesis needs revisiting before
   Phase 2H.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- `ExCreateThread` real thread spawning — its own future phase (2H), given the scale of
  the jump (host threads, thread-safety for existing shared state, per-thread TLS).
- Real audio driver semantics for `XAudioRegisterRenderDriverClient` — future audio-
  specific phase.
