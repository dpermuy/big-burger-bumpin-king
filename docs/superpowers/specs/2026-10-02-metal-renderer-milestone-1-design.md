# Metal Renderer, Milestone 1: Window + Clear-Color Present

## Context

This project is a static Xbox 360 recompilation (XenonRecomp-based) of the
game "Big Bumpin'". A 104-finding investigation
(`docs/superpowers/specs/phase3-past-loading-screen-investigation.txt`)
traced and fixed the CPU-side execution stalls that were preventing the
recompiled game from running a healthy main loop. That investigation's own
closing finding (Finding 104) concluded that the original "loading screen
never progresses" symptom is resolved to the full extent observable without
a real renderer — this project currently has no rendering output at all.

`host/gpu_trace.cpp`'s `GpuCommandTracer` parses and logs the real Xenos/R500
PM4 command stream (confirmed against Xenia's own real opcode semantics,
Findings 35-37 of the investigation above) and executes two synchronization
opcodes (`PM4_EVENT_WRITE_SHD`, `PM4_INTERRUPT`) for correctness, but never
draws anything. `host/kernel_impl.cpp`'s `__imp__VdSwap` (the real per-frame
present call) has an explicit no-op comment: no renderer exists yet.

This spec covers the first real milestone toward changing that: a real
window, a real Metal device, and a real swapchain that `VdSwap` actually
presents to — clearing to a fixed debug color each frame. No PM4 draw-call
execution yet; that is explicitly out of scope and will be a later,
separate milestone built on top of this one.

## Goals

- A real `--window` CLI flag opens a real macOS window and shows a cleared
  color, updated once per real `VdSwap` call from the running game.
- The existing headless CLI workflow (`./build/BigBumpinHost` with no
  flags, redirected to a log, bounded by the existing watchdog) is
  completely unaffected — this is the backbone of this project's entire
  established regression-testing methodology, and must keep working
  exactly as it does today.
- The PM4 parsing/semantics layer (`gpu_trace.cpp`) stays backend-agnostic.
  All Metal- and Cocoa-specific code lives in new, separate files. This is
  not "build for future portability" — Metal itself does not port, and no
  second backend is being built now — it is keeping already-separate
  concerns (PM4 semantics vs. a specific graphics API) from tangling,
  which is just good file organization and happens to make a future
  backend a new file instead of a rewrite, if one is ever wanted.

## Non-goals

- Executing real PM4 draw calls (`PM4_DRAW_INDX_2` etc.), real vertex/index
  buffers, real shaders, or real texture sampling. These depend on this
  milestone's infrastructure existing first and are separate future work.
- Any Windows/Linux backend, or a `RenderBackend` abstraction interface.
  Explicitly deferred until there is a second real backend to validate an
  abstraction against.
- Matching real frame pacing/vsync timing precisely. This milestone proves
  the present mechanism works; real pacing is a later concern once actual
  rendering work exists to pace.

## Architecture

**Threading model.** AppKit requires windows and their run loop to live on
the main thread — this is a hard platform constraint, not a design choice.
The existing PPC execution thread (launched via `std::async` in
`main.cpp`, running `_xstart`) is untouched and keeps running exactly as it
does today, including the existing watchdog (`future.wait_for`). When
`--window` is passed, `main()` instead builds a minimal `NSApplication` +
`NSWindow` + `MTKView` and runs the real AppKit event loop on the main
thread, with the PPC execution thread still running concurrently in the
background exactly as in headless mode.

`__imp__VdSwap` (running on the PPC thread) cannot safely touch
`NSWindow`/`MTKView` state directly from a background thread. It instead
posts a lightweight "a frame was presented" signal into a thread-safe
queue/counter. `MTKView`'s own per-frame render callback (invoked by
AppKit on the main thread, driven by the display link) checks for a new
signal, encodes a trivial clear-color command buffer, and presents the
drawable. If no new signal has arrived since the last callback (the game
hasn't called `VdSwap` yet), the callback simply re-presents the last
cleared frame rather than blocking.

**File organization.**
- `host/renderer_metal.h` / `host/renderer_metal.mm` (new, Objective-C++):
  all Metal/Cocoa-specific code — device creation, the `NSWindow`/`MTKView`
  setup, the thread-safe present-signal queue, and the render callback that
  clears and presents. Exposes a small, plain C++-callable interface (no
  Objective-C types in the header) so `kernel_impl.cpp` can call into it
  without needing Objective-C++ compilation itself.
- `host/gpu_trace.cpp`: unchanged in this milestone. No dependency on the
  new renderer files.
- `host/kernel_impl.cpp`: `__imp__VdSwap` gains one new call into the
  renderer's "present signal" function, guarded by whether the renderer
  was actually initialized (headless mode: this is always a no-op, zero
  behavior change).
- `host/main.cpp`: gains `--window` flag parsing and, when set, calls into
  the renderer's init/run-event-loop entry point on the main thread instead
  of returning immediately after launching the PPC thread.

**Build system.** `CMakeLists.txt` adds `host/renderer_metal.mm` to
`BigBumpinHost`'s sources, sets it to compile as Objective-C++, and links
the `Metal`, `MetalKit`, `Cocoa`, and `QuartzCore` frameworks. These
changes are additive only — no existing target, source list, or link
behavior changes for the parts of the build that don't touch the new file.

**Window parameters.** 1280x720, matching the real resolution already
seeded into the GPU register block's `AVIVO_D1MODE_VIEWPORT_SIZE` value in
`SetupMemoryImage` (`host/main.cpp`). Fixed debug clear color (e.g.
cornflower blue, a standard graphics-programming convention for "nothing
drawn yet but the pipeline works").

## Data flow

1. `main()` parses `--window`. Headless (no flag): behavior is byte-for-byte
   identical to today.
2. With `--window`: `main()` initializes the Metal renderer (device,
   `MTKView`, window) on the main thread, launches the existing PPC
   execution thread exactly as today, then hands control to
   `NSApp run` (blocking the main thread in the real AppKit event loop,
   same as any normal macOS app).
3. The PPC thread runs `_xstart` as today. When game code calls real
   `VdSwap`, `__imp__VdSwap` does its existing real work (GPU interrupt
   dispatch, in-flight-swap counter reset, etc., all unchanged) and
   additionally posts a present-signal.
4. `MTKView`'s render callback (main thread, driven by the system display
   link) fires continuously regardless of game state. Each firing: check
   the present-signal; encode a command buffer that clears the drawable to
   the fixed debug color; present.
5. The existing watchdog (`future.wait_for` + `std::_Exit`) still governs
   how long the PPC thread is allowed to run. In `--window` mode, hitting
   the watchdog needs to also terminate the AppKit run loop cleanly (e.g.
   `NSApp stop` from a watchdog-expiry callback) rather than leaving a
   window open after the host process's own logic has decided to exit.

## Error handling

- Metal device creation failure (no GPU, e.g. some CI environments): log a
  clear error and exit non-zero. Do not silently fall back to headless —
  the user explicitly asked for a window.
- `--window` combined with the default short watchdog: no special handling
  needed, the window will simply open and close quickly, matching
  headless mode's existing short-run semantics. Longer interactive viewing
  needs an explicit extended watchdog, exactly as every other extended
  test run this project has already done throughout the investigation
  above (a temporary, reverted `main.cpp` change, not new infrastructure).

## Testing

- Headless regression check (no `--window`): run the existing default
  10-second smoke test, confirm `NtReadFile` counts and log shape are
  byte-for-byte unchanged from before this change — proves zero
  regression to the established testing methodology.
- `--window` smoke test: launch with `--window` and a short watchdog,
  confirm a real window opens showing the cleared debug color, and that
  the process exits cleanly at the watchdog boundary without leaving a
  zombie window or AppKit process behind.
- `--window` with an extended watchdog: confirm the clear color stays
  stable (no flicker/corruption) while the PPC thread continues running
  real game logic underneath, for at least as long as this project's
  existing extended live-validation runs (60-600s, matching the patience
  tests already used throughout the investigation).

## Open questions for the implementation plan

- Exact present-signal data structure (a simple atomic counter is likely
  sufficient for this milestone, since no actual frame content is being
  handed across threads yet — just a "something happened" signal).
- Exact watchdog-to-AppKit-shutdown wiring (likely a small callback or
  atomic flag the display link callback checks, given `NSApp stop` must
  itself be called from the main thread).
