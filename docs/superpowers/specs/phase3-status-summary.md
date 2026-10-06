# Phase 3 status: why the game stops after loading

Summary of the investigation through Finding 174. Details, evidence, and
corrections are in `phase3-past-loading-screen-investigation.txt`.

## What works

- The game boots, runs its frame loop, and presents frames (guest VdSwap).
- Rectangle lists (non-indexed) render with the expected corners and viewport
  mapping (Findings 143-151). Indexed rectangle lists are implemented but
  untested: no indexed rectangle draw has appeared in any capture (Findings
  152-153).
- The loading-complete flag (canonical `field+136`) reaches 1 at present 30.

## What is broken

The game stops at about frame 32. Its main thread takes the shutdown path,
tears down, and returns, and then the watchdog fires after 10 s.

## The chain, as established

1. The frame loop `sub_8244AFB0` runs frames while `field+136 == 0`
   (Finding 106, Finding 165).
2. `field+136` is set to 1 by `sub_82451408` when a progress accumulator
   passes `field+76` (Finding 167).
3. The accumulator is elapsed time in seconds (Finding 168).
4. `field+76` is a direct copy of a boot-time global at guest 0x8270F784. Our
   boot patch sets that global to 1 (Finding 118, Finding 169, Finding 170).
   A threshold of 1 ends loading at about one second.
5. `field+136` is a one-way latch. It is set once and never cleared (Finding 174).
6. `sub_8244B040` is a one-shot: init, progress loop, teardown, return. It
   has no next-phase call, and its only caller does not loop (Finding 173).

Threshold sweep (Findings 171-172): teardown happens at about the set value.
Thresholds 1 and 3 teardown at about 32 and 91 presents. Thresholds 10 and 30
also teardown when the watchdog is raised. Every tested value ends in the
same teardown, just later.

## Corrections to earlier findings

- Finding 158 said the game kept presenting the same content. That was wrong.
  Guest presents stop at 33 (Finding 164). The repeated draws are the
  renderer holding the last frame.
- Finding 168's "seconds" unit is the best reading from step size and code
  path, not a fully decoded constant. Treat as high but not full confidence.
- Finding 152's indexed rectangle path is untested.

## Open questions

- Does a real console clear `field+136` somewhere this build never reaches,
  or does the real game only set it on quit? Not visible in this run.
- What is the real loading threshold? The console would have set the global
  at 0x8270F784. No copy exists in the game files or the XEX (Finding 170).
- Which caller should loop over phases after `sub_8244B040` returns? None
  found.

## Current state of the tree

- `host/main.cpp` committed value: placeholder `kLoadingProgressConfigSlot = 1`,
  watchdog 10 s. Test values were reverted.
- No temporary probes are committed.
- Indexed rectangle support is committed but untested (`d805295`).

## Recommendation

1. Find the code that should clear `field+136` or start the next phase. It
   may be in a path this build never runs (e.g. a scene change or a quit
   check). Check for writers of the canonical object from other functions.
2. Do not pick a threshold by sweeping. A sweep only shows that the value
   moves the teardown time. A value should come from the game's data or be
   measured from a real loading time.
3. Keep the indexed rectangle path flagged as untested until a capture
   shows an indexed rectangle draw.
