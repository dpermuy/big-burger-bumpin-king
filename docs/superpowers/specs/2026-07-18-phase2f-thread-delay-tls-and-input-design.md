# Phase 2F: Thread Delay, TLS Read, and Input Enumeration — Big Bumpin' Recomp

## Context

Phase 2E ([spec](2026-07-18-phase2e-memory-stats-and-physical-alloc-design.md))
implemented `MmQueryStatistics`/`MmAllocatePhysicalMemoryEx` conservatively, which was
enough to progress past the prior SIGTRAP entirely into new territory: controller/input
enumeration and TLS reads. A fresh run left 6 imports hit
(`docs/superpowers/specs/phase2e-stub-hits.txt`): `ExCreateThread`,
`KeDelayExecutionThread`, `KeTlsGetValue`, `XamInputGetCapabilities`,
`XamInputGetState`, `XamNotifyCreateListener`.

Unlike some prior phases, this group didn't need fresh empirical investigation — every
function maps cleanly onto either an already-established pattern from earlier phases or a
real, well-documented Xbox 360 behavior:

- `KeTlsGetValue` is the read counterpart to `KeTlsAlloc`/`KeTlsSetValue`, already
  implemented in Phases 2B/2C.
- `XamInputGetCapabilities`/`XamInputGetState` being called across `r3=0,1,2,3` (observed
  in Phase 2E's log) is the game polling all 4 possible controller ports — a real Xbox 360
  game must gracefully handle "no controller connected" on any of them, since that's an
  ordinary, fully-supported hardware state, not an edge case.
- `XamNotifyCreateListener` fits the same handle-returning shape as the sync objects from
  Phase 2C.

## Goal

Implement all 6 hit imports, continuing to defer `ExCreateThread`'s real thread-spawning
semantics (still not shown to be the actual blocker — both observed calls in Phase 2E's
run are followed by the main thread continuing normally).

## Design

- **`ExCreateThread`** — stays untouched (still the Phase 2A-era do-nothing stub).
- **`KeDelayExecutionThread(r3=waitMode, r4=alertable, r5=intervalPtr)`** — return success
  immediately without actually sleeping. This isn't laziness: `BigBumpinHost` runs
  `_xstart` on a thread with a 10-second watchdog (Phase 2A); a real sleep here would burn
  that budget on what is, under our current single-execution-context model, a wait that
  can never accomplish anything different by actually waiting (nothing else runs
  concurrently to change state in the meantime) — the same reasoning already applied to
  `NtWaitForSingleObjectEx` in Phase 2C.
- **`KeTlsGetValue(r3=index)`** — read from the existing `g_tlsSlots` array (Phase 2B/2C),
  return the stored value, or `0` if the index is out of range.
- **`XamInputGetCapabilities(r3=userIndex, ...)` / `XamInputGetState(r3=userIndex, ...)`**
  — return `ERROR_DEVICE_NOT_CONNECTED` (`0x48F`, a standard documented Win32 error code)
  for every user index. No real controller passthrough exists yet; this is the accurate
  response for that state, not an approximation of a different one.
- **`XamNotifyCreateListener`** — allocate a handle from the existing handle table (Phase
  2C's `g_handleTable`/`g_nextHandle`), returned directly in `ctx.r3` (matching the
  `Xam*`/`Mm*` family's direct-return convention rather than `Nt*`'s status-plus-output-
  pointer convention). No new `HandleObjectType` needed — nothing yet reads back or
  interprets this handle's type, so it's tracked generically for now.

## Success Criteria

Phase 2F is complete when:

1. All 6 functions are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than Phase 2E's run or fails at a new,
   demonstrably different point.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- `ExCreateThread` real thread spawning — still deferred, still not shown to be the
  actual blocker.
- Real controller/input passthrough (e.g. via a host gamepad API) — no evidence yet that
  the game does anything beyond graceful no-controller handling; revisit if that changes.
