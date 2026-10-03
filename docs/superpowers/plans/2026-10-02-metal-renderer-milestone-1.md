# Metal Renderer Milestone 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Open a real macOS window (behind a new `--window` flag) that a real Metal device presents a cleared debug color to, driven once per real `VdSwap` call from the running recompiled game, with zero change to the existing headless CLI testing workflow.

**Architecture:** All Metal/Cocoa-specific code lives in a new `host/renderer_metal.mm` (Objective-C++), exposing a plain C-linkage interface (`host/renderer_metal.h`) so the rest of the project stays plain C++. `main()` runs the real AppKit event loop on the main thread only when `--window` is passed; the existing PPC execution thread (`std::async`-launched `_xstart`) is untouched. A separate watchdog thread mirrors today's `future.wait_for` logic and signals the AppKit loop to stop via an atomic flag, since `NSApp stop:` must be requested but only takes effect once the run loop processes another event.

**Tech Stack:** C++17, Objective-C++ (`.mm`), Metal, MetalKit (`MTKView`), Cocoa (`NSApplication`/`NSWindow`), CMake.

**Spec:** `docs/superpowers/specs/2026-10-02-metal-renderer-milestone-1-design.md`

## Global Constraints

- Headless mode (no `--window`) must be byte-for-byte unchanged in behavior, output, and exit codes — this is the entire existing regression-testing methodology for this project (see `docs/superpowers/specs/phase3-past-loading-screen-investigation.txt`, a 104-finding investigation log that depends on it).
- No PM4 draw-call execution, vertex/index buffers, shaders, or textures in this milestone. Fixed debug clear color only.
- No `RenderBackend` abstraction or second graphics API. Metal only, for now.
- `host/gpu_trace.cpp` is not modified in this plan — PM4 parsing stays fully decoupled from the renderer.
- Window size is fixed at 1280x720, matching the existing `AVIVO_D1MODE_VIEWPORT_SIZE` value already seeded in `SetupMemoryImage` (`host/main.cpp`).
- Every task must be built and run for real before being considered done, matching this project's own established culture (every one of the 104 findings above was verified via a real build + real run + real log/lldb inspection before being committed). No task is "done" on the strength of code review alone.

## Review Focus

- **`--window` combined with positional args in any order** (e.g. `./BigBumpinHost --window`, `./BigBumpinHost custom.xex --window`, `./BigBumpinHost --window custom.xex`): a reasonable person expects the flag's position not to break `xexPath`/`isoPath` parsing. Covered in Task 2, Step 5b.
- **User manually closes the window before the watchdog fires**: a reasonable person expects clicking the window's close button to end the program, not leave a silently-vanished window with the process still running untouched. Covered in Task 2, Step 6.
- **No GPU device available** (e.g. a constrained CI environment): a reasonable person expects a clear error message and a non-zero exit, not a silent hang or a crash with no explanation. Covered in Task 2, Step 7.
- **Running `--window` without a real local GUI session** (SSH, headless CI): this is a real environment constraint, not a bug to fix — documented explicitly in Task 3's testing notes so nobody mistakes the expected failure mode for a defect.
- **The two hardcoded 1280x720 values** (this plan's new `Renderer_Init` call, and the pre-existing `AVIVO_D1MODE_VIEWPORT_SIZE` register seed) must stay in sync by eye for this milestone — flagged with a code comment in Task 2 rather than unified into a shared constant, since introducing a cross-file shared constant for one hardcoded milestone value is not worth the indirection yet (YAGNI).

---

### Task 1: Build plumbing — stub renderer compiles, links, and changes nothing

**Files:**
- Create: `host/renderer_metal.h`
- Create: `host/renderer_metal.mm` (stub implementation only — no real Cocoa/Metal calls yet)
- Modify: `CMakeLists.txt:24-27`

**Interfaces:**
- Produces: `Renderer_Init(int, int) -> bool`, `Renderer_RunEventLoop() -> void`, `Renderer_PostPresentSignal() -> void`, `Renderer_RequestShutdown() -> void`, `Renderer_IsActive() -> bool` — all `extern "C"`, all plain C-linkage, no Objective-C types in the header. These exact names and signatures are consumed by Task 2 and Task 3.

This task's only goal is proving the build system change works in isolation — Objective-C++ compilation, framework linking — before any real window/Metal logic exists to debug at the same time.

- [ ] **Step 1: Write the renderer interface header**

Create `host/renderer_metal.h`:

```cpp
#pragma once

// Metal/Cocoa renderer for Milestone 1 (window + clear-color present).
// Plain C-linkage interface -- no Objective-C types appear in this header,
// so callers (main.cpp, kernel_impl.cpp) don't need Objective-C++
// compilation themselves. All Objective-C/Metal/Cocoa code lives in
// renderer_metal.mm.
extern "C" {

// Creates the NSApplication, NSWindow (width x height), MTKView, and Metal
// device. Must be called on the main thread, before Renderer_RunEventLoop.
// Returns false on device/window creation failure (e.g. no GPU available).
bool Renderer_Init(int width, int height);

// Blocks the calling thread (must be the main thread) running the real
// AppKit event loop until Renderer_RequestShutdown() is called from any
// thread (or the user closes the window), then returns. Each display-link
// tick, clears the drawable to a fixed debug color and presents it.
void Renderer_RunEventLoop();

// Thread-safe, cheap. Records that a real frame was presented -- purely a
// counter bump for this milestone, no frame content crosses threads yet.
// Safe to call even if Renderer_Init() was never called.
void Renderer_PostPresentSignal();

// Thread-safe. Returns whether Renderer_Init() has successfully completed.
// Callers (VdSwap) use this to skip PostPresentSignal entirely in headless
// mode, for a true zero-cost no-op rather than relying on
// PostPresentSignal's own internals.
bool Renderer_IsActive();

// Thread-safe. Requests that the event loop started by
// Renderer_RunEventLoop() stop. It returns once AppKit processes the
// request (on the next display-link tick, or immediately if the window
// was already closed).
void Renderer_RequestShutdown();

} // extern "C"
```

- [ ] **Step 2: Write the stub implementation**

Create `host/renderer_metal.mm`:

```objc
#include "renderer_metal.h"

#include <atomic>

// TEMP stub for Task 1 -- proves the build/link wiring works in isolation.
// Task 2 replaces every function body here with the real Cocoa/Metal
// implementation; none of these signatures change.

namespace {
std::atomic<bool> g_initialized{false};
}

bool Renderer_Init(int width, int height) {
    (void)width;
    (void)height;
    g_initialized.store(true, std::memory_order_relaxed);
    return true;
}

void Renderer_RunEventLoop() {
    // Stub: returns immediately. Task 2 replaces this with [NSApp run].
}

void Renderer_PostPresentSignal() {
    // Stub: no-op.
}

bool Renderer_IsActive() {
    return g_initialized.load(std::memory_order_relaxed);
}

void Renderer_RequestShutdown() {
    // Stub: no-op.
}
```

- [ ] **Step 3: Wire the new file into the build**

Modify `CMakeLists.txt`. Find this block (currently lines 24-27):

```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/game_overrides.cpp)
    target_include_directories(BigBumpinHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc")
    target_link_libraries(BigBumpinHost PRIVATE BigBumpinPPC XenonUtils fmt::fmt)
endif()
```

Replace it with:

```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/game_overrides.cpp host/renderer_metal.mm)
    target_include_directories(BigBumpinHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc")
    target_link_libraries(BigBumpinHost PRIVATE BigBumpinPPC XenonUtils fmt::fmt "-framework Metal" "-framework MetalKit" "-framework Cocoa" "-framework QuartzCore")
    set_source_files_properties(host/renderer_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
```

(`-fobjc-arc` enables Automatic Reference Counting for this one file only, so Task 2's Objective-C object lifetimes don't need manual `retain`/`release`. The rest of the project stays plain C++ and is unaffected.)

- [ ] **Step 4: Reconfigure and build**

Run:
```bash
cd build && cmake .. && cmake --build . --target BigBumpinHost -j 8
```
Expected: clean build, no errors. A harmless pre-existing `-Wasm-operand-widths` warning from `ppc_context.h` is expected and not a regression (seen in every build throughout this project's history).

- [ ] **Step 5: Confirm headless behavior is completely unchanged**

Run:
```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
date "+%Y-%m-%d %H:%M:%S %Z" && ./build/BigBumpinHost > run.log 2>&1
date "+%Y-%m-%d %H:%M:%S %Z"
grep -c NtReadFile run.log
tail -3 run.log
```
Expected: 47 `NtReadFile` lines (the historical pre-Finding-96 baseline) or 153 (the post-Finding-96/98 baseline — either is fine as long as it matches whatever the baseline was immediately before this task started; the point is this task changes nothing about it), and the same `_xstart did not return within 10 seconds (watchdog timeout)...` message as every prior run in this project's history. Clean up:
```bash
rm -f run.log gpu_trace.log
```

- [ ] **Step 6: Commit**

```bash
git add host/renderer_metal.h host/renderer_metal.mm CMakeLists.txt
git commit -m "build: add stub Metal renderer target and Objective-C++ build wiring"
```

---

### Task 2: Real window, Metal device, and clear-color present loop

**Files:**
- Modify: `host/renderer_metal.mm` (replace all stub bodies with real implementations)
- Modify: `host/main.cpp` (add `--window` flag parsing and the watchdog/event-loop restructuring)

**Interfaces:**
- Consumes: `Renderer_Init`, `Renderer_RunEventLoop`, `Renderer_RequestShutdown`, `Renderer_IsActive` from Task 1 (signatures unchanged).
- Produces: a running `--window` mode in `main()` that later tasks (Task 3) can rely on continuing to work unmodified.

- [ ] **Step 1: Replace the stub with the real Cocoa/Metal implementation**

Replace the full contents of `host/renderer_metal.mm` with:

```objc
#include "renderer_metal.h"

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

#include <atomic>

namespace {
std::atomic<uint64_t> g_presentSignalCount{0};
std::atomic<bool> g_shutdownRequested{false};
std::atomic<bool> g_initialized{false};

// Shared by both the display-link draw callback (normal shutdown path) and
// the window-close delegate (user clicked the close button) -- both need
// to actually break [NSApp run] out of its wait, which only happens once
// another event is processed after -stop: is called.
void StopApplication() {
    [NSApp stop:nil];
    NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:wake atStart:YES];
}
} // namespace

@interface BigBumpinRendererDelegate : NSObject <MTKViewDelegate>
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@end

@implementation BigBumpinRendererDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // Fixed window size for this milestone -- nothing to recompute.
}

- (void)drawInMTKView:(MTKView *)view {
    if (g_shutdownRequested.load(std::memory_order_relaxed)) {
        StopApplication();
        return;
    }

    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!pass || !drawable) {
        return;
    }

    // Fixed debug clear color (cornflower blue, a standard graphics-
    // programming convention for "pipeline works, nothing drawn yet").
    // Milestone 1 does not execute any real PM4 draw content.
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.392, 0.584, 0.929, 1.0);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

@end

@interface BigBumpinWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation BigBumpinWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    // Real, expected behavior: closing the window ends the program's
    // rendering, same shutdown path the watchdog uses. The PPC execution
    // thread and its own watchdog bound are untouched by this -- the
    // process still exits on its existing watchdog timing, matching
    // headless mode's own existing bound-wait semantics (there is no
    // "cancel early" mechanism in headless mode either).
    g_shutdownRequested.store(true, std::memory_order_relaxed);
    StopApplication();
}
@end

namespace {
NSWindow *g_window = nil;
MTKView *g_view = nil;
BigBumpinRendererDelegate *g_renderDelegate = nil;
BigBumpinWindowDelegate *g_windowDelegate = nil;
}

bool Renderer_Init(int width, int height) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            return false;
        }

        NSRect frame = NSMakeRect(0, 0, width, height);
        g_window = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:(NSWindowStyleMaskTitled |
                                                           NSWindowStyleMaskClosable |
                                                           NSWindowStyleMaskMiniaturizable)
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        [g_window setTitle:@"Big Bumpin' (recompiled)"];
        [g_window center];

        g_windowDelegate = [[BigBumpinWindowDelegate alloc] init];
        g_window.delegate = g_windowDelegate;

        g_view = [[MTKView alloc] initWithFrame:frame device:device];
        g_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;

        g_renderDelegate = [[BigBumpinRendererDelegate alloc] init];
        g_renderDelegate.commandQueue = [device newCommandQueue];
        g_view.delegate = g_renderDelegate;

        [g_window setContentView:g_view];
        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        g_initialized.store(true, std::memory_order_relaxed);
        return true;
    }
}

void Renderer_RunEventLoop() {
    [NSApp run];
}

void Renderer_PostPresentSignal() {
    g_presentSignalCount.fetch_add(1, std::memory_order_relaxed);
}

bool Renderer_IsActive() {
    return g_initialized.load(std::memory_order_relaxed);
}

void Renderer_RequestShutdown() {
    g_shutdownRequested.store(true, std::memory_order_relaxed);
}
```

- [ ] **Step 2: Add `--window` flag parsing and the watchdog/event-loop split to main.cpp**

Add the new include near the top of `host/main.cpp` (alongside the existing `#include "xdvdfs.h"`):

```cpp
#include "renderer_metal.h"
```

Add these to the existing `#include <...>` block in `host/main.cpp` (alongside `#include <future>`):

```cpp
#include <atomic>
#include <string>
#include <vector>
```

Replace the start of `main()` (currently):

```cpp
int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    const char* isoPath = argc > 2 ? argv[2] : "Big Bumpin' (USA).iso";
    uint8_t* base = SetupMemoryImage(xexPath);
```

with:

```cpp
int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    // --window can appear anywhere on the command line; remaining
    // arguments stay positional (xexPath, isoPath) in their original
    // relative order.
    bool windowMode = false;
    std::vector<const char*> positionalArgs;
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--window")
        {
            windowMode = true;
        }
        else
        {
            positionalArgs.push_back(argv[i]);
        }
    }

    const char* xexPath = positionalArgs.size() > 0 ? positionalArgs[0] : "private/default.xex";
    const char* isoPath = positionalArgs.size() > 1 ? positionalArgs[1] : "Big Bumpin' (USA).iso";
    uint8_t* base = SetupMemoryImage(xexPath);

    if (windowMode)
    {
        // Fixed at 1280x720 for this milestone, matching the real
        // AVIVO_D1MODE_VIEWPORT_SIZE value already seeded in
        // SetupMemoryImage above -- keep these two in sync by eye; not
        // worth a shared constant for one hardcoded milestone value.
        constexpr int kWindowWidth = 1280;
        constexpr int kWindowHeight = 720;
        if (!Renderer_Init(kWindowWidth, kWindowHeight))
        {
            fmt::println("Failed to initialize Metal renderer (no GPU device available?) -- exiting.");
            std::_Exit(1);
        }
    }
```

Replace the existing watchdog block at the end of `main()` (currently):

```cpp
    auto future = std::async(std::launch::async, [&]() {
        _xstart(ctx, base);
    });

    auto status = future.wait_for(std::chrono::seconds(10));
    if (status == std::future_status::timeout)
    {
        fmt::println("_xstart did not return within 10 seconds (watchdog timeout) -- "
            "this is an expected, informative outcome for Phase 2A, not a crash.");
        std::_Exit(2);
    }

    fmt::println("_xstart returned normally.");
    return 0;
}
```

with:

```cpp
    auto future = std::async(std::launch::async, [&]() {
        _xstart(ctx, base);
    });

    if (!windowMode)
    {
        // Existing behavior, byte-for-byte unchanged.
        auto status = future.wait_for(std::chrono::seconds(10));
        if (status == std::future_status::timeout)
        {
            fmt::println("_xstart did not return within 10 seconds (watchdog timeout) -- "
                "this is an expected, informative outcome for Phase 2A, not a crash.");
            std::_Exit(2);
        }

        fmt::println("_xstart returned normally.");
        return 0;
    }

    // --window mode: the main thread must run the real AppKit event loop
    // instead of blocking directly on future.wait_for (windows only
    // function on the main thread, a hard Cocoa requirement). A separate
    // watchdog thread mirrors the exact same wait_for logic and timeout
    // message as headless mode, then signals the AppKit loop to stop.
    std::atomic<int> watchdogResult{-1}; // set to 0 (returned normally) or 2 (timeout) below
    std::thread watchdogThread([&]() {
        auto status = future.wait_for(std::chrono::seconds(10));
        if (status == std::future_status::timeout)
        {
            fmt::println("_xstart did not return within 10 seconds (watchdog timeout) -- "
                "this is an expected, informative outcome for Phase 2A, not a crash.");
            watchdogResult.store(2);
        }
        else
        {
            fmt::println("_xstart returned normally.");
            watchdogResult.store(0);
        }
        Renderer_RequestShutdown();
    });

    Renderer_RunEventLoop(); // blocks main thread until shutdown is requested
    watchdogThread.join();

    if (watchdogResult.load() == 2)
    {
        std::_Exit(2);
    }
    return 0;
}
```

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
```
Expected: clean build, no new warnings beyond the pre-existing `-Wasm-operand-widths` one.

- [ ] **Step 4: Confirm headless mode is still completely unchanged**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
./build/BigBumpinHost > run.log 2>&1
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
```
Expected: identical `NtReadFile` count and tail message to Task 1's Step 5 result — this task must not have changed headless behavior at all.

- [ ] **Step 5: Run with `--window` and confirm a real window opens**

```bash
./build/BigBumpinHost --window
```
Expected: a real window titled "Big Bumpin' (recompiled)" opens, showing a solid cornflower-blue fill, and the process exits on its own at the ~10 second watchdog boundary (window closes automatically, process returns to the shell). Confirm no leftover process:
```bash
pgrep -f BigBumpinHost
```
Expected: no output (process has fully exited).

- [ ] **Step 5b: Confirm `--window` works regardless of its position relative to positional args**

```bash
./build/BigBumpinHost --window
```
Expected: same window-opens-and-exits-cleanly behavior as Step 5 (this repeats Step 5's exact command as the baseline case for comparison with the two below).

```bash
./build/BigBumpinHost private/default.xex --window
```
Expected: same window behavior, and confirm via the log that it loaded `private/default.xex` (the explicit positional arg), not the default:
```bash
./build/BigBumpinHost private/default.xex --window > run.log 2>&1
grep "Guest memory image ready" run.log
rm -f run.log gpu_trace.log
```

```bash
./build/BigBumpinHost --window private/default.xex
```
Expected: identical behavior to the previous command — `--window`'s position relative to the positional argument must not change which file loads or whether the window opens.

- [ ] **Step 6: Confirm manually closing the window ends the program promptly**

```bash
./build/BigBumpinHost --window &
```
Wait 1-2 seconds for the window to appear, then manually click its close button. Expected: the window disappears immediately (not waiting for the full watchdog bound). The background process itself may take up to the remaining watchdog time to fully exit (matching headless mode's own existing bound-wait semantics — there is no "cancel the PPC thread early" mechanism in this project, in either mode). Confirm it does eventually exit:
```bash
wait
pgrep -f BigBumpinHost
```
Expected: no output (process has exited, no zombie left behind).

- [ ] **Step 7: Confirm the no-GPU-device error path is reachable and correct**

This step is a code-review confirmation rather than a forced-failure test (there is no practical way to make `MTLCreateSystemDefaultDevice()` fail on a real Mac for a live test). Re-read `Renderer_Init`'s `if (!device) { return false; }` path and `main.cpp`'s `if (!Renderer_Init(...)) { fmt::println(...); std::_Exit(1); }` call site together, and confirm: the error message is printed before exit, and the exit code (1) is distinct from the existing headless watchdog-timeout exit code (2), so a caller/script can tell the two failure modes apart.

- [ ] **Step 8: Commit**

```bash
git add host/renderer_metal.mm host/main.cpp
git commit -m "feat: real Metal window with clear-color present behind --window flag"
```

---

### Task 3: Wire VdSwap to the renderer and validate under a long live run

**Files:**
- Modify: `host/kernel_impl.cpp` (add the present-signal call inside `__imp__VdSwap`)

**Interfaces:**
- Consumes: `Renderer_IsActive() -> bool`, `Renderer_PostPresentSignal() -> void` from Task 1/2 (unchanged signatures).

- [ ] **Step 1: Add the present-signal call to VdSwap**

Add near the top of `host/kernel_impl.cpp`'s includes (alongside the existing `#include "gpu_trace.h"`):

```cpp
#include "renderer_metal.h"
```

In `PPC_FUNC(__imp__VdSwap)`, find the existing real in-flight-swap-counter reset near the end of the function:

```cpp
    constexpr uint32_t kGpuManagerSelf = 0xA0009900; // empirically stable this whole
                                                      // investigation (Findings 20-55) --
                                                      // allocated very early, before the
                                                      // large game-heap request (Finding
                                                      // 51) that shifts later addresses.
    PPC_STORE_U32(kGpuManagerSelf + 10868, 0);
}
```

Add the present-signal call right after the existing store, still inside the function:

```cpp
    constexpr uint32_t kGpuManagerSelf = 0xA0009900; // empirically stable this whole
                                                      // investigation (Findings 20-55) --
                                                      // allocated very early, before the
                                                      // large game-heap request (Finding
                                                      // 51) that shifts later addresses.
    PPC_STORE_U32(kGpuManagerSelf + 10868, 0);

    // Milestone 1 (Metal renderer): record that a real frame was
    // presented. Renderer_IsActive() is false in headless mode (the
    // default, and this project's entire existing regression-testing
    // methodology), making this a true zero-cost no-op there -- no
    // renderer-module code runs at all unless --window was passed.
    if (Renderer_IsActive())
    {
        Renderer_PostPresentSignal();
    }
}
```

- [ ] **Step 2: Build**

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
```
Expected: clean build.

- [ ] **Step 3: Confirm headless mode is still completely unchanged**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
./build/BigBumpinHost > run.log 2>&1
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
```
Expected: identical to Task 2's Step 4 result.

- [ ] **Step 4: Confirm `--window` still works with the default short watchdog**

```bash
./build/BigBumpinHost --window
```
Expected: same as Task 2's Step 5 — window opens with the clear color, process exits cleanly on its own.

- [ ] **Step 5: Extended live validation — confirm stability under a long run**

Temporarily raise the watchdog bound to exercise this under the same kind of long-run conditions this project's own investigation used throughout (Findings 90-104 of `docs/superpowers/specs/phase3-past-loading-screen-investigation.txt`). In `host/main.cpp`, change both `std::chrono::seconds(10)` watchdog calls (the headless one and the `--window` one) to `std::chrono::seconds(120)`, with a `// TEMP long-run validation, revert after` comment, matching this project's own established convention for temporary watchdog extensions. Rebuild:

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
```

Run:
```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
date "+%Y-%m-%d %H:%M:%S %Z" && ./build/BigBumpinHost --window > run.log 2>&1
date "+%Y-%m-%d %H:%M:%S %Z"
```

While this runs, visually confirm (or, if running non-interactively, confirm via `pgrep -f BigBumpinHost` staying present for the full duration) that the window stays open showing a stable, non-flickering, non-corrupted cornflower-blue fill for the full 120 seconds, then exits cleanly on its own. Check the log for any unexpected crashes or errors:
```bash
tail -5 run.log
rm -f run.log gpu_trace.log
```
Expected: the same `_xstart did not return within 10 seconds...` message text (the message string itself is not parameterized by the actual watchdog duration — this is pre-existing, expected behavior, not a bug introduced by this task), no crash, no corruption.

- [ ] **Step 6: Revert the temporary watchdog extension**

In `host/main.cpp`, change both `std::chrono::seconds(120)` calls back to `std::chrono::seconds(10)` and remove the `// TEMP long-run validation, revert after` comments. Rebuild and confirm the diff against the committed state is empty for this specific change:

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
git diff host/main.cpp
```
Expected: no output (file matches what Task 2 committed).

- [ ] **Step 7: Final default regression check**

```bash
./build/BigBumpinHost > run.log 2>&1
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
```
Expected: identical to Step 3's result in this task.

- [ ] **Step 8: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "feat: VdSwap posts a present-signal to the Metal renderer when --window is active"
```
