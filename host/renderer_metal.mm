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
