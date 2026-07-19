# Phase 2K: Real Blocking Critical Sections — Big Bumpin' Recomp

## Context

Investigating the "physical memory allocation stall" reported after Phase 2J revealed
it isn't a memory bug at all. Evidence gathered via `lldb` thread-by-thread backtraces
during a live hang:

- The run doesn't actually go silent — `MmAllocatePhysicalMemoryEx` calls keep succeeding
  right up to the point the watchdog fires. Extending the watchdog from 10s to 120s produced
  a run that froze at the *exact same* address both times, proving this is a genuine stall,
  not merely slow progress.
- All-thread backtraces during the freeze showed 5 of 8 threads inside
  `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` (`host/kernel_impl.cpp:72-91`), all
  reached through the same `XApiThreadStartup` trampoline (`sub_820ABCE8`) via
  `sub_8212B210`. Sampling their PCs twice, 3 seconds apart, showed them actively churning
  (real CPU-bound activity, not blocked).
- The one thread that matters — the real game-logic thread running `_xstart`
  (`sub_82128110`) — was frozen at the *identical* instruction (a time-base read, PPC
  `mftb` compiled to ARM64 `mrs x9, CNTVCT_EL0`) across all samples spanning 6+ seconds.
  It was not running at all during that window.

Root cause: `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` provide no real mutual
exclusion. They lock the global `g_stateMutex` just long enough to bump a recursion
counter and release it immediately — `RtlEnterCriticalSection` always "succeeds"
instantly regardless of who else holds the section. Any guest code that spin-waits on a
critical section (checking some other piece of state each iteration, expecting to
eventually observe a change made by the section's real owner) gets no backpressure at
all and busy-loops at full CPU speed. With multiple real host threads (real threading
since Phase 2H) doing this concurrently, they saturate available CPU cores and starve the
actual game thread — a priority-inversion livelock, not a deadlock and not a memory bug.

## Goal

Make `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` real, blocking, recursive locks,
so a thread that needs to wait for a critical section actually blocks (freeing its CPU
core) instead of spinning.

## Design

Replace the bookkeeping-only `CriticalSectionState` (`recursionCount` +
`owningThread`, manually maintained) with a real `std::recursive_mutex` per critical
section address, owned via `std::unique_ptr` (the type isn't movable, so the map has to
hold pointers, not values):

```cpp
static std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> g_criticalSections;
```

- **`RtlInitializeCriticalSection`**: lock `g_stateMutex`, create the entry if absent
  (`std::make_unique<std::recursive_mutex>()`), release `g_stateMutex`. Idempotent — if
  called twice for the same address, the second call is a no-op (matches the existing
  lazy-init tolerance already implicit in the old `operator[]`-based code).
- **`RtlEnterCriticalSection`**: lock `g_stateMutex` briefly to find-or-create the
  section's `recursive_mutex` pointer (lazy init, in case `Enter` is reached before an
  explicit `Initialize` — matches current behavior), release `g_stateMutex`, then call
  the real `->lock()` **outside** `g_stateMutex`. This is the actual blocking wait: the OS
  parks the thread until the owner releases, using zero CPU while waiting.
- **`RtlLeaveCriticalSection`**: same lookup pattern (brief `g_stateMutex` hold to get the
  pointer, release), then `->unlock()`.
- Recursion (same thread entering multiple times) is handled natively by
  `std::recursive_mutex` — no manual counter needed.
- `g_stateMutex` is never held across the actual `lock()`/`unlock()` call, only across the
  map lookup/insert — so one thread blocking on a critical section can never stall another
  thread's unrelated `g_stateMutex` use (handle table, bump allocator, TLS all keep working
  normally while a thread waits).

### Explicitly not touched

- **`RtlTryEnterCriticalSection`** — still on `host/kernel_imports.txt`, never observed
  hit in any run so far. Not implemented here; no evidence it's needed.
- **`RtlDeleteCriticalSection`** — doesn't exist anywhere in the codebase (not in
  `kernel_imports.txt`, not implemented, not stubbed) — never observed called. Critical
  sections in this project live for the process's lifetime; no cleanup path needed.
- **`KeEnterCriticalRegion`/`KeLeaveCriticalRegion`** (Phase 2J no-ops) — different real
  semantics (APC delivery disable/enable, not mutual exclusion). Out of scope, no evidence
  they need to change.
- No change to `g_stateMutex`'s other uses (handle table, bump allocator, TLS, thread IDs)
  — only the critical-section implementation itself changes.

## Success Criteria

1. `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` implemented with real
   `std::recursive_mutex`-backed blocking semantics in `host/kernel_impl.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. Re-run the same repro (`private/default.xex`) that previously froze at
   `sub_82128110`'s time-base read for 120+ seconds. Report whatever actually happens —
   forward progress past that point, a new distinct failure, or (honestly, if it happens)
   the same freeze — rather than assuming success.
4. If the run makes new forward progress, capture the fresh log and a distinct stub-hit
   list, per the established phase pattern.

## Explicitly Out of Scope

- `RtlTryEnterCriticalSection` (unimplemented, unconfirmed usage).
- `RtlDeleteCriticalSection` (doesn't exist in this codebase).
- Any change to `KeEnterCriticalRegion`/`KeLeaveCriticalRegion`.
- Any change to `g_stateMutex`'s other consumers.
- Root-causing *what* the guest's spin-wait is actually waiting for (i.e., the specific
  higher-level condition in `sub_82128110`/`sub_820ABCE8`'s call chain) — this spec fixes
  the mechanism (blocking vs. spinning), not the guest-side logic driving it. If the game
  still doesn't progress after this fix, that's new evidence for a follow-up
  investigation, not a regression in this phase's scope.
