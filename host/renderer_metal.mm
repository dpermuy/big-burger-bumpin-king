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
