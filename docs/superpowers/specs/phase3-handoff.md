# Handoff: Phase 3 (big-burger-bumpin-king), continue the stall investigation

Paste this into a new session started in the repo
(`/Users/dylan/Documents/GitHub/big-burger-bumpin-king`), or ask it to read this file.

## Read first

1. `docs/superpowers/specs/phase3-status-summary.md` (current state, chain, open questions)
2. `docs/superpowers/specs/phase3-past-loading-screen-investigation.txt`, Findings 165-190 (latest evidence)

## Where things are

- Branch `master`, last pushed commit `7ec1083` (Finding 190), plus the status
  update commit. The tree is clean apart from untracked files.
- Build: `cmake --build build --target BigBumpinHost`. Run: `./build/BigBumpinHost --window`.
- `private/` is git-ignored: generated PPC code lives there. Temporary edits
  there must be reverted by hand (keep a backup in
  `/Users/dylan/.claude/jobs/011990e5/tmp/` before editing).
- `timeout` is not installed on this Mac: use background launch and `pkill`.
- lldb conditional breakpoints with `*(unsigned int*)$x0` do not fire here.
  Use unconditional breakpoints with `memory read` and `bt`.

## The next question

Find the writer of the flag at `obj+816` bit 0 (object = global at -29808, lis
-32143 offset -29808, guest 0x82710000 - 29808 = 0x82708B90 for the global
slot). `sub_82461BE8` (shutdown drain) waits while it is set (Finding 190).
Also, find the producer of event handle 0x1004 (stored at guest 0x82660018,
created by `sub_8212A710` via `sub_820A9750`). The worker `sub_82127EE0`
waits on it (Findings 183-188).

## Method that worked

- Watchpoint (lldb, `watchpoint set expression -s 4 -- 0x7000000000ULL + <guest>`)
  set at a known entry point, then `continue` and `bt`. Host address = guest +
  0x7000000000.
- Breakpoint on `__imp__<name>` with `bt` to find callers.
- Forced test: change one behavior in `host/kernel_impl.cpp`, rebuild, run,
  compare presents / write-pointer moves / draw counts in `gpu_trace.log`,
  then revert with `git checkout`.

## Do not

- Commit temporary probes or test values. The placeholder in `host/main.cpp`
  stays at 1 until a researched value is found.
- Pick a loading threshold by sweeping. Sweeps only move the teardown time.
- Trust the 10-second watchdog as a stop signal. It is a harness limit.
