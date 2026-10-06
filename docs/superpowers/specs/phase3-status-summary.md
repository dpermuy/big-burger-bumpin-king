# Phase 3 status: why the game stops after loading

Summary through Finding 190. Full evidence and corrections are in
`phase3-past-loading-screen-investigation.txt`.

## What works

- Boot runs. The game's frame loop runs and presents frames.
- Non-indexed rectangle lists render with correct corners and viewport mapping
  (Findings 143-151).
- Indexed rectangle lists are implemented (`d805295`) but untested: no indexed
  rectangle draw has appeared in any capture (Findings 152-153).
- The host no longer aborts when stdout fails (`7caf080`, Finding 186).

## What is broken

The picture freezes. The game stops building new draws after the early phase
(Finding 189), and its main thread ends up in shutdown (Findings 165, 180).

## The chain, as established

1. The frame loop `sub_8244AFB0` runs while `field+136 == 0` (Findings 106, 165).
2. `field+136` is set to 1 by `sub_82451408` when a progress accumulator passes
   `field+76` (Finding 167). It is a one-way latch: set once, never cleared
   (Findings 174-176).
3. The accumulator is elapsed time in seconds (Finding 168).
4. `field+76` is a direct copy of a boot global at 0x8270F784. Our boot patch
   sets that global to 1 (Findings 118, 169, 170). Threshold 1 ends loading at
   about one second.
5. `sub_8244B040` is a one-shot: init, progress loop, teardown, return
   (Finding 173).
6. Teardown (`sub_824A3CC8`) clears the display word that gates ring
   submission (Finding 180). Submissions stop at about 5 s (Finding 179).
7. The worker thread (`sub_82127EE0`, created at boot, Finding 188) waits on
   event handle 0x1004, stored at global 0x82660018. No visible code signals it
   (Findings 183-185). The graphics interrupt path signals 0x1017, not 0x1004
   (Finding 187).
8. Forcing 0x1004 signaled at present 10 keeps submissions running for the whole
   window (3692 presents, 3470 write-pointer moves) but adds no new draws
   (Finding 189). Draw calls are all in the first third of the run.
9. With the forced signal, the main thread enters shutdown and waits in
   `sub_82461BE8` for the flag at `obj+816` bit 0 to clear (Finding 190). It
   is still set. Its writer is not found.

## Threshold results

Teardown happens at about the set value (Findings 171-172): 1 at about 32
presents, 3 at about 91. Values 10 and 30 teardown when the watchdog is raised.
Every tested value ends the same way, so the number is not the fix.

## Corrections to earlier findings

- Finding 158 said the game kept presenting the same content. Wrong. Guest
  presents stop at 33 (Finding 164). The repeated draws are the renderer
  holding the last frame.
- Finding 168's "seconds" unit is inferred from step size and code path, not
  a fully decoded constant.
- Finding 152's indexed rectangle path is untested.

## Open questions

1. Writer of `obj+816` bit 0 (object = global at -29808). This is the current
   blocker for shutdown (Finding 190).
2. What signals event 0x1004 in a real run. Probably a producer on a path this
   build does not reach (Findings 185, 188).
3. What builds the draw list in the in-game state. Draws stop after the early
   phase (Finding 189).
4. Whether a real console clears `field+136` anywhere (Findings 174-176).
5. The real loading threshold. The console sets the global at 0x8270F784; no
   copy exists in the game files or the XEX (Finding 170).

## Current state of the tree

- `host/main.cpp`: placeholder `kLoadingProgressConfigSlot = 1`, watchdog 10 s.
- `host/kernel_impl.cpp`: committed state. No temporary probes are committed.
- Indexed rectangle support committed, untested (`d805295`).
- Host print helper `host/host_print.h` committed (`7caf080`).
- Forced-signal probe and state probes were all reverted.

## Recommendation

1. Find the writer of `obj+816` bit 0, or the code that should clear it.
2. Find the producer of 0x1004. Check whether it is the same mechanism as the
   latch and the shutdown flag.
3. Decide whether to keep the placeholder at 1 or replace it with a researched
   value. Do not pick a number by sweeping.
