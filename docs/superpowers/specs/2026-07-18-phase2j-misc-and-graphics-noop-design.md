# Phase 2J: Misc Utilities and Graphics No-Ops — Big Bumpin' Recomp

## Context

Phase 2I ([spec](2026-07-18-phase2i-real-wait-timeout-design.md)) fixed the
`KeWaitForSingleObject` spin. The resulting run reached deep into real Xbox 360 video
subsystem initialization — `VdInitializeEngines` through `VdPersistDisplay`, the standard
XDK video boot order — entirely on generic do-nothing stubs, without a single failure.
It was still making forward progress (not stuck) when the watchdog fired.
`docs/superpowers/specs/phase2i-stub-hits.txt` lists 32 remaining imports: 11 non-graphics
utilities and 21 graphics/display (`Vd*` plus `XGetVideoMode`) calls.

The key finding driving this spec: the entire graphics boot sequence observed so far has
tolerated no-op stubs without issue. This means the actual GPU command-buffer parsing and
Metal/Vulkan rendering translation — the real, large "Phase 3" undertaking flagged since
the start of this project — has not yet been *required* by anything. This phase implements
the remaining 32 imports as no-ops/plausible-success returns to find out whether the game
reaches a genuinely stable running state (the "black window" milestone) without any real
rendering, rather than starting real GPU translation work speculatively.

## Goal

Implement all 32 remaining imports. For the 11 non-graphics utilities, give real behavior
where it's well-defined and low-risk (following established per-function precedent from
prior phases); no-op where there's no evidence real behavior is needed. For the 21
graphics/display calls, implement as no-ops/plausible-success returns — explicitly not
attempting real GPU command execution or display output.

## Design

### Non-graphics utilities (11)

- **`RtlFillMemoryUlong(Destination, Length, Pattern)`** — real, well-defined behavior:
  fill `Length` bytes at `Destination` with the repeating 32-bit `Pattern`. A standard,
  stable RTL utility — implemented for real, not stubbed.
- **`MmGetPhysicalAddress(VirtualAddress)`** — identity mapping: return the same address
  unchanged. This project has no real physical/virtual memory distinction.
- **`MmSetAddressProtect`** — no-op, returns success. No real page-protection enforcement
  exists or is needed yet.
- **`KeSetEvent`** — no-op, returns `0` (previous signal state). Same "no real dispatcher-
  object state tracked" reasoning already applied to `KeResetEvent`'s existing stub
  (Phase 2A-era, unchanged — not hit in the run this spec is based on, so not touched
  here).
- **`KeEnterCriticalRegion` / `KeLeaveCriticalRegion`** — no-ops. Real semantics
  disable/enable APC delivery; this project doesn't emulate APCs, so there's nothing to
  disable or re-enable.
- **`KiApcNormalRoutineNop`** — no-op by design; the name states its own real behavior.
- **`XexGetModuleHandle(ModuleName, out HModule)`** — the observed call passes
  `ModuleName = NULL` (meaning "the current module," the only case seen). Real Xbox 360
  convention: a module's "handle" is its own base load address. Returns `PPC_IMAGE_BASE`
  (a fact already established and used throughout this project, not a guess).
- **`XexGetModuleSection`** — no section-lookup infrastructure exists. Rather than
  fabricate section data, honestly report "not found" (`STATUS_NOT_FOUND`), matching the
  no-hard-disk precedent (Phase 2D): an honest failure the caller's own fallback logic can
  handle.
- **`_vsnprintf` / `DbgPrint`** — no-ops, return `0`. Pure debug-output helpers with no
  bearing on game logic; real PPC varargs formatting is nontrivial and has zero
  correctness value to implement here.
- **`ExTerminateThread(ExitCode)`** — genuinely terminates the calling host thread via
  `pthread_exit`. This one needs real behavior, not a no-op: the generated code for
  `XApiThreadStartup` (`sub_820ABCE8`, read directly in Phase 2H's crash investigation)
  calls this as its final action after running the user thread function. In the one
  observed call site it happens to be the last statement before a natural return anyway,
  but `ExTerminateThread` can be called from arbitrary nesting depth at other call sites,
  where "just returning" would only unwind one level instead of actually ending the
  thread — `pthread_exit` is correct regardless of call depth, a plain no-op stub isn't.
- **`KeSetAffinityThread`** — no-op, returns success (writing `0` to the previous-affinity
  out-pointer if present). Processor affinity is ignored, matching `ExCreateThread`'s
  existing treatment of `CreationFlags`' affinity bits (Phase 2H) — the OS scheduler
  handles real placement on a modern multi-core host.

### Graphics/display (21: `VdCallGraphicsNotificationRoutines`, `VdEnableRingBufferRPtrWriteBack`, `VdGetCurrentDisplayGamma`, `VdGetCurrentDisplayInformation`, `VdGetSystemCommandBuffer`, `VdInitializeEngines`, `VdInitializeRingBuffer`, `VdInitializeScalerCommandBuffer`, `VdIsHSIOTrainingSucceeded`, `VdPersistDisplay`, `VdQueryVideoMode`, `VdRetrainEDRAM`, `VdRetrainEDRAMWorker`, `VdSetDisplayMode`, `VdSetGraphicsInterruptCallback`, `VdSetSystemCommandBufferGpuIdentifierAddress`, `VdShutdownEngines`, `VdSwap`, `XGetVideoMode`)

All implemented as no-ops or minimal plausible-success returns — no real GPU command
buffer parsing, no real display mode data, no real rendering. Two exceptions where a
specific value is clearly warranted rather than arbitrary:

- **`VdIsHSIOTrainingSucceeded`** returns `1` (true) — this is a hardware EDRAM-training
  status check; reporting success avoids the game treating (nonexistent) hardware training
  as having failed.
- **`VdSetGraphicsInterruptCallback(CallbackAddress, Context)`** — no-op for now, but
  documented distinctly: this registers the callback a real GPU interrupt handler (vblank,
  etc.) would invoke. Nothing yet requires us to actually call it back; if future frame-
  pacing work needs real vsync timing, this is where that callback address comes from.

Every other function in this group returns success (or is genuinely `void` per its real
signature) without writing any structured output data — the same conservative posture
Phase 2E used for `MmQueryStatistics`: several of these (`VdQueryVideoMode`,
`XGetVideoMode`, `VdGetCurrentDisplayGamma`, `VdGetCurrentDisplayInformation`,
`VdGetSystemCommandBuffer`) would need real, currently-unverified struct layouts to fill
in meaningfully, and there's no evidence yet the game reads back and depends on specific
field values from any of them — the entire sequence up through `VdSwap` and
`VdPersistDisplay` already completed once purely on "success, no data written" generic
stubs, immediately before this phase existed.

## Success Criteria

Phase 2J is complete when:

1. All 32 functions are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run is captured, specifically checking whether the game reaches a stable,
   non-spinning, non-crashing running state — the "black window" milestone — rather than
   just progressing to a new failure point. Both outcomes (stable idle loop, or a new
   specific failure) are valuable; report whichever actually happens.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- Real GPU command-buffer parsing (the ring buffer `VdInitializeRingBuffer` sets up) or
  any actual Metal/Vulkan rendering translation — the real, large "Phase 3" work, not
  started speculatively before confirming the game reaches a stable state without it.
- Real struct-layout-correct data for `VdQueryVideoMode`, `XGetVideoMode`,
  `VdGetCurrentDisplayGamma`, `VdGetCurrentDisplayInformation`, `VdGetSystemCommandBuffer`
  — no confirmed layouts, no evidence yet the game needs correct field values.
- Real vsync/frame-pacing via `VdSetGraphicsInterruptCallback`'s registered callback.
