# Phase 2H: Real Thread Spawning — Big Bumpin' Recomp

## Context

Phase 2G ([spec](2026-07-18-phase2g-irql-spinlocks-and-object-refs-design.md)) implemented
11 low-risk imports and cleanly confirmed a hypothesis carried since Phase 2A:
`ExCreateThread` is the sole remaining stubbed import, and the game's main thread is
provably blocked spinning on state a spawned worker thread would normally set. Everything
else in the boot sequence up to this point succeeds.

Given how consequential getting this wrong would be, `ExCreateThread`'s actual argument
registers were captured directly (`r3`-`r10`, not just the `r3`-`r6` every prior phase
logged) across all 4 observed call sites before designing anything. This surfaced an
important correction: Phase 2A's original assumption that `r6` was the thread's start
address was wrong. The real picture, cross-checked against which register uniquely varies
per call (a start address must differ for every thread; a shared trampoline need not):

```
r3 = Handle (out-pointer)                        -- stack address every call
r4 = StackSize (0 observed in all 4 calls)        -- 0 = use a default
r5 = ThreadId (out-pointer, null on 2 of 4 calls)
r6 = XApiThreadStartup (0x820ABCE8 on 2 calls, 0 on the other 2)
r7 = StartAddress                                  -- HIGH CONFIDENCE: unique on every call
r8 = StartContext (0 on all 4 observed calls)
r9 = CreationFlags (0, 0, 0x10000001, 0x20000001 -- bit0 = CREATE_SUSPENDED on 2 of 4;
                     high bits look like processor-affinity hints)
```

This matches the real (XDK-documented) Xbox 360 `ExCreateThread` signature, trimmed to 7
parameters rather than desktop NT's larger `CreateThread` surface.

## Goal

Implement real thread spawning: `ExCreateThread` actually runs a host thread executing the
requested guest code, with its own stack and register state, `CREATE_SUSPENDED` support,
and thread-safety for all shared state built up across Phases 2B-2G.

## Design

### Thread state

```cpp
struct GuestThread
{
    std::thread hostThread;
    bool suspended = false;
    std::condition_variable resumeCv;
    std::mutex resumeMutex;
};

static std::unordered_map<uint32_t, GuestThread> g_threads;
```

Tracked via a new `HandleObjectType::Thread` value in the existing handle table (Phase
2C), so `NtClose`/other handle-generic code continues to work uniformly.

### ExCreateThread

1. Allocate a 256 KiB (`0x40000`) stack from the existing bump allocator
   (`g_bumpAllocatorNext`, Phase 2B) — generous for a worker thread, small enough not to
   burn address space quickly. Real `StackSize` (`r4`) is ignored when `0` (observed in
   all 4 calls); if a future run shows a nonzero value, round it up to this same
   granularity instead of trusting it exactly, consistent with how
   `MmAllocatePhysicalMemoryEx` (Phase 2E) already treats uncertain size arguments.
2. Build a fresh `PPCContext` for the new thread: `r1` = top of the newly allocated stack,
   `r3` = `StartContext` (`r8`), all other registers zeroed.
3. Determine the real entry point: if `XApiThreadStartup` (`r6`) is non-zero, that's the
   function the host thread calls — real Xbox 360 hardware behavior is for this trampoline
   to internally invoke `StartAddress`/`StartContext` itself, and since it's real
   recompiled guest code, letting it do that (rather than us reimplementing its internal
   logic) is both simpler and more accurate. If `r6` is `0` (observed on 2 of 4 calls),
   call `StartAddress` (`r7`) directly instead.
4. If `CreationFlags` (`r9`) bit 0 is set (`CREATE_SUSPENDED`), the spawned host thread
   blocks on `resumeCv` before calling the entry point. Otherwise it proceeds immediately.
5. Allocate a handle (`HandleObjectType::Thread`), write it to `*HandleOutPtr` (`r3`, the
   function's r3 argument — not to be confused with the new thread's own `PPCContext.r3`).
   If `ThreadIdOutPtr` (`r5`) is non-null, write a simple incrementing thread ID.
6. Processor-affinity bits (`CreationFlags` high bits) are ignored entirely — meaningless
   on a modern multi-core host; the OS scheduler handles real placement.

### KeResumeThread (revised from Phase 2G's no-op)

Phase 2G's `KeResumeThread` implementation just returned `0` unconditionally, explicitly
noting "no real thread scheduling exists" at the time. Now it does: look up the thread
object (via the sentinel/handle convention `ObReferenceObjectByHandle` established in
Phase 2G), and if it corresponds to a tracked `GuestThread` currently suspended, set
`suspended = false` and notify `resumeCv`. Still returns `0` (previous suspend count) —
real per-thread suspend-count tracking beyond a single suspended/not-suspended bit isn't
warranted by any evidence so far (no nested suspend/resume calls observed).

### Thread safety

A single coarse global mutex (`g_stateMutex`) protects every piece of shared state
introduced since Phase 2B: `g_handleTable`, `g_criticalSections`, `g_bumpAllocatorNext`,
`g_nextHandle`, and the new `g_threads`. One lock, not fine-grained per-structure locking —
correctness matters far more than throughput at this stage, and fine-grained locking
introduces lock-ordering bugs this project has no way to test for yet.

`g_tlsSlots` (Phase 2B/2C/2F) changes from `static uint64_t g_tlsSlots[64]` to
`static thread_local uint64_t g_tlsSlots[64]` — real per-thread storage, provided by the
language itself rather than hand-rolled. The slot-number allocator (`g_tlsNextSlot`)
stays a single shared counter (slot *numbering* is global even though slot *values* are
per-thread) and moves under `g_stateMutex` protection along with everything else.

### Process lifecycle

`main()`'s watchdog continues to apply only to the `_xstart` call itself (Phase 2A),
unchanged. Spawned worker threads run independently; if `_xstart` returns, times out, or
the process otherwise exits while worker threads are still running, they're abruptly
terminated with the process — acceptable for this bring-up harness, not worth building
graceful shutdown/join logic for without evidence it matters.

## Success Criteria

Phase 2H is complete when:

1. `ExCreateThread` spawns a real host thread executing the requested guest entry point,
   `KeResumeThread` actually resumes `CREATE_SUSPENDED` threads, and all shared state is
   protected by `g_stateMutex`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than Phase 2G's watchdog timeout or fails at a
   new, demonstrably different point (a crash from a genuine concurrency bug is plausible
   and would itself be valuable, specific information — not a phase failure to hide).
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- Fine-grained locking / lock-free data structures — premature without evidence of
  contention or a specific correctness problem the coarse mutex causes.
- Real per-thread suspend-count nesting beyond a single suspended/not-suspended bit.
- Graceful process shutdown/thread joining.
- Any of the still-untouched 163 kernel imports beyond what a fresh run's evidence
  surfaces next.
