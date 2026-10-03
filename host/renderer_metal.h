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
// Renderer_RunEventLoop() stop. Dispatches the actual stop onto the main
// queue directly (not dependent on the display link still ticking, which
// can pause independent of window state), so it reliably terminates the
// run loop even if the window is minimized, occluded, or the display is
// asleep.
void Renderer_RequestShutdown();

// Thread-safe. Returns whether the user closed the window themselves
// (rather than the event loop stopping via Renderer_RequestShutdown()).
// Only meaningful after Renderer_RunEventLoop() has returned. Callers
// (main.cpp) use this to end the process immediately on a user close,
// rather than waiting for the PPC execution thread's own separate
// watchdog bound to also elapse.
bool Renderer_WasClosedByUser();

} // extern "C"
