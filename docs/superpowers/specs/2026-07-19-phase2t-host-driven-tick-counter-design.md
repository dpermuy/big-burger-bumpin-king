# Phase 2T: Host-Driven Kernel Tick Counter — Big Bumpin' Recomp

## Context

Phase 2R fixed `r13` (confirmed via cross-reference) but found the fix alone insufficient:
the pointer slot it unlocks at guest address `0x82670100` (`r13 + 256`) is itself always
zero. Phase 2S confirmed why: that address falls inside the `.data` section, correctly
loaded from the XEX -- and the *raw XEX file* genuinely contains zero there. No game code
anywhere writes it either (confirmed by exhaustive search across all generated
`private/ppc/*.cpp`).

This is real Xbox 360 kernel/HAL-populated data: on actual hardware, a periodic timer
interrupt updates a kernel-shared tick structure, and the kernel's own boot/loader code
(entirely outside any title's own executable) points each process's local slot at it.
This project has never emulated any part of that boot sequence -- we jump straight from
loading the XEX to calling its entry point (`_xstart`).

`sub_820B9A58` (the function whose busy-poll originally surfaced this whole investigation
chain, Phase 2Q) reads this chain as: `ptr = *(r13+256)`, then `tick = *(ptr+88)`,
comparing `tick`'s change since a recorded timestamp against a threshold of `5000`. The
`5000` threshold, interpreted as milliseconds (the most natural reading, matching the
common `GetTickCount()`-style millisecond-tick convention and giving a plausible ~5-second
pacing interval), is the only unit assumption this design makes -- flagged explicitly, not
silently baked in.

## Goal

Make the tick value at `[ptr+88]` genuinely advance in real time, so `sub_820B9A58`'s
pacing check (and any other consumer of the same structure) can actually succeed instead
of polling a permanently-frozen zero forever.

## Design

### Guest-memory tick structure

Reserve a small, fixed guest address for a minimal host-owned structure --
`0x7FFF0000`. Chosen because it falls well outside every region already in active use:
the loaded XEX image (`0x82000600`-`0x8271A5FC`, confirmed via Phase 2S's section dump),
the stack region (`0x90000000`+), and the kernel bump allocator (`0xA0000000`+, `host/kernel_impl.cpp`).

Only one field is implemented -- the `+88` tick offset `sub_820B9A58` actually reads.
Per this project's established conservative posture (e.g. Phase 2E's `MmQueryStatistics`,
Phase 2B's critical-section state): don't fabricate other fields with no evidence they're
read. The rest of the reserved region stays zeroed.

### Populating the pointer slot

In `host/main.cpp`'s `SetupMemoryImage`, after sections are loaded and before returning
`base`:
```cpp
constexpr uint32_t kKernelTickStructAddr = 0x7FFF0000;
constexpr uint32_t kKernelTickPointerSlot = 0x82670100; // r13(0x82670000) + 256, confirmed Phase 2Q/2R
PPC_STORE_U32(kKernelTickPointerSlot, kKernelTickStructAddr);
```

### Updating the tick value in real time

A single detached background `std::thread`, started once in `main()` after
`SetupMemoryImage` returns, sleeping and updating the tick field:

```cpp
std::thread tickThread([base]()
{
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        PPC_STORE_U32(0x7FFF0000 + 88, (uint32_t)elapsedMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
});
tickThread.detach();
```

One millisecond granularity matches the assumed millisecond-tick unit above and is far
finer than anything currently polling it (a `5000`-tick/~5-second threshold), so
resolution isn't a concern. Detached, matching the existing precedent for
`ExCreateThread`-spawned threads (`host/kernel_impl.cpp`) -- never joined, runs harmlessly
for the process's lifetime, no `std::terminate()` risk since it's never stored in a
container that outlives `main`.

No mutex needed: this is a single writer (the tick thread), and readers (`PPC_LOAD_U32`
from guest code) tolerate torn reads of a monotonically-increasing counter the same way
real hardware would -- worst case a reader sees a slightly-stale value for one iteration,
never a value that goes backward or corrupts unrelated data, since it's a single aligned
32-bit store.

## Success Criteria

1. Guest address `0x82670100` holds `0x7FFF0000` immediately after image setup, before
   `_xstart` runs.
2. The tick value at `0x7FFF0088` visibly advances over real time once the tick thread
   starts.
3. `BigBumpinHost` builds clean (zero compiler errors).
4. Re-run `private/default.xex` with an adequate watchdog and report honestly what
   happens: whether `sub_820B9A58`'s busy-poll (confirmed via Phase 2Q at 1.1B+
   iterations/30s) stops, whether execution reaches new territory, or whether a different
   symptom appears. Given the chain of dependencies already found in this investigation,
   do not assume this single fix resolves everything downstream.
5. If new territory is reached, capture the fresh run log and a distinct stub-hit list,
   per the established phase pattern.

## Explicitly Out of Scope

- Any tick-structure field beyond `+88` -- no evidence yet any other offset is read.
- Real millisecond-unit verification against actual Xbox 360 hardware documentation --
  the `5000`-threshold interpretation is a reasoned assumption, flagged as such, not a
  confirmed fact. If a future run's evidence contradicts it (e.g., the pacing loop
  completes suspiciously fast or slow relative to what 5 real seconds would suggest),
  that's grounds to revisit, not a blocker now.
- Any other still-uninitialized `r13`-relative slot beyond the one this investigation
  chain has actually traced (`+256`) -- no evidence any other slot is hit yet.
- General "boot sequence emulation" beyond this one specific structure -- scoped
  narrowly to what's confirmed needed by the actual stall under investigation.
