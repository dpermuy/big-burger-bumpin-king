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
