# Phase 2D: File I/O as "No Hard Disk Present" — Big Bumpin' Recomp

## Context

Phase 2C ([spec](2026-07-18-phase2c-handle-table-and-simple-stubs-design.md)) implemented
12 of 22 imports Phase 2B's run hit. A fresh run confirmed zero regressions and left 10
imports still stubbed: `docs/superpowers/specs/phase2c-stub-hits.txt` — file I/O (5),
crypto (2), memory statistics/physical allocation (2), and real thread spawning (1).

Before designing file I/O, the actual guest paths being requested were captured directly
(same investigate-then-implement discipline as `NtAllocateVirtualMemory` in Phase 2B): the
generic stub for `NtCreateFile` was temporarily instrumented to dereference its
`ObjectAttributes` argument and print the raw bytes. The result was not a game-content
path — it was `\Device\Harddisk0\WindowsPartition...` and a second, related
`\Device\Harddisk0\Pa[rtition]...` string. These are NT device-enumeration paths: the game
is probing whether a hard disk is attached, not reading a specific asset file from the
disc. No evidence yet exists of a call requesting real disc/game-content data — execution
hasn't reached that point (it terminates in the same SIGTRAP as Phase 2B/2C, on the
still-deferred crypto/memory-stats/thread-spawn path).

This changes the shape of the problem from "build a virtual filesystem serving extracted
ISO content" to "correctly report that no hard disk is present" — a real, common, fully
supported Xbox 360 hardware configuration (retail consoles routinely shipped and ran games
without a hard drive attached), not an edge case the game would mishandle.

A secondary finding from the same investigation: the current generic stub template
returns success without writing to any output pointer. For a handle-returning function
like `NtCreateFile`, this means callers receive an untouched (garbage/stale) handle value
while being told the call succeeded — directly observed in Phase 2B/2C's logs, where the
`NtReadFile` immediately following a stubbed `NtCreateFile` call was invoked with
`r3=0xFFFFFFFF` (a leftover stack value, not a real handle). This phase's implementations
fix that specifically for these 5 functions by writing real (if failure-indicating)
output values.

## Goal

Implement the 5 file I/O imports (`NtCreateFile`, `NtOpenFile`, `NtReadFile`,
`NtWriteFile`, `NtDeviceIoControlFile`) to report "no hard disk / no such device" using
real NTSTATUS codes, rather than either the old always-succeeds-with-garbage stub or a
speculative virtual filesystem with no evidence backing its need.

## Design

- **`NtCreateFile(r3=handleOutPtr, r6=ioStatusBlockPtr)`** — write `0` to `*handleOutPtr`
  (fixing the garbage-handle bug). If `ioStatusBlockPtr` is non-null, write
  `IoStatusBlock.Status = STATUS_NO_SUCH_DEVICE` (`0xC000000E`, a standard documented
  NTSTATUS value) and `IoStatusBlock.Information = 0` (the `IO_STATUS_BLOCK` structure's
  first 4 bytes are the status, next 4 are an information field — a small, stable, widely
  documented NT structure). Return `STATUS_NO_SUCH_DEVICE` in `ctx.r3`.

  Confidence note: `r6` being the `IoStatusBlock` pointer fits both the real NT parameter
  ordering and the observed stack-address shape of the value, but — unlike
  `NtAllocateVirtualMemory`'s mapping in Phase 2B — this wasn't independently confirmed by
  dereferencing and cross-checking content (there's no self-describing content in an
  output-only parameter to verify against, the way `RegionSize`'s `0x100000` value
  confirmed itself). If this turns out wrong, the consequence is a wasted/misdirected
  4-8 byte write into caller stack memory on what is already a failure path — low blast
  radius, but not zero. Worth keeping in mind if unrelated stack corruption ever shows up
  in a future run's crash.

- **`NtOpenFile(r3=handleOutPtr)`** — write `0` to `*handleOutPtr`, return
  `STATUS_NO_SUCH_DEVICE`. The register mapping here is lower-confidence than
  `NtCreateFile`'s (inferred purely from positional/shape similarity, no independent
  string dereference performed for this specific call site), so the implementation stays
  simpler — no `IoStatusBlock` write attempted.

- **`NtReadFile` / `NtWriteFile` / `NtDeviceIoControlFile`** — return
  `STATUS_INVALID_HANDLE` (`0xC0000008`) unconditionally. Once `NtCreateFile`/`NtOpenFile`
  can never hand back a real handle, any handle reaching these functions is by definition
  invalid — this is the correct real-NT response, not an approximation.

## Success Criteria

Phase 2D is complete when:

1. All 5 functions are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than Phase 2C's run or fails at a new,
   demonstrably different point. Given crypto (`XeCryptSha`,
   `XeKeysConsolePrivateKeySign`) in the existing log is downstream of these exact file
   calls, a real behavior change here is plausible (the crypto calls may no longer be
   reached at all if the game's own no-HDD fallback skips them, or may be reached with
   different — still stub, still logged — arguments if the fallback path calls them
   anyway).
4. The new run's log is saved and a fresh distinct-stub-hit list extracted, the same
   pattern as every prior phase.

## Explicitly Out of Scope

- Real filesystem passthrough serving `private/extracted/` ISO content — no evidence yet
  that any call requests real disc/game-content data. Revisit if a future run's captured
  path evidence shows one.
- Crypto (`XeCryptSha`, `XeKeysConsolePrivateKeySign`) — own future phase, and now
  possibly moot for the specific calls already observed if the no-HDD fallback skips
  them; still needed if new crypto calls appear elsewhere.
- Memory statistics/physical allocation, real thread spawning — unchanged from Phase 2C's
  deferral, still their own future phases.
