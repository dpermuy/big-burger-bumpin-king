#include "renderer_metal.h"
#include "gpu_trace.h"

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <cstdio>
#include <vector>

namespace {
const char* kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
};

vertex float4 vertex_main(VertexIn in [[stage_in]]) {
    return float4(in.position, 1.0);
}

fragment float4 fragment_main() {
    return float4(1.0, 1.0, 1.0, 1.0);
}
)";

id<MTLRenderPipelineState> g_drawPipelineState = nil;
std::atomic<uint64_t> g_presentSignalCount{0};
std::atomic<uint64_t> g_drawnFrameCount{0};
std::atomic<bool> g_shutdownRequested{false};
std::atomic<bool> g_closedByUser{false};
std::atomic<bool> g_initialized{false};

// Shared by the display-link draw callback (normal shutdown path), the
// window-close delegate (user clicked the close button), and
// Renderer_RequestShutdown's own dispatch_async (see below) -- all three
// need to actually break [NSApp run] out of its wait, which only happens
// once another event is processed after -stop: is called.
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

    std::vector<DrawCommand> drawCommands = g_gpuTracer.DrawList().TakeReady();
    [encoder setRenderPipelineState:g_drawPipelineState];
    for (const DrawCommand &cmd : drawCommands) {
        if (cmd.vertexData.empty()) {
            continue;
        }
        id<MTLBuffer> vertexBuffer = [self.commandQueue.device newBufferWithBytes:cmd.vertexData.data()
                                                                            length:cmd.vertexData.size()
                                                                           options:MTLResourceStorageModeShared];
        if (!vertexBuffer) {
            fprintf(stderr, "[renderer] failed to create vertex buffer (%zu bytes), skipping draw\n", cmd.vertexData.size());
            continue;
        }
        [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];

        MTLPrimitiveType metalPrimType;
        switch (cmd.primitiveType) {
            case DrawPrimitiveType::Point: metalPrimType = MTLPrimitiveTypePoint; break;
            case DrawPrimitiveType::Line: metalPrimType = MTLPrimitiveTypeLine; break;
            case DrawPrimitiveType::LineStrip: metalPrimType = MTLPrimitiveTypeLineStrip; break;
            case DrawPrimitiveType::Triangle: metalPrimType = MTLPrimitiveTypeTriangle; break;
            case DrawPrimitiveType::TriangleStrip: metalPrimType = MTLPrimitiveTypeTriangleStrip; break;
        }

        if (cmd.indexData.empty()) {
            [encoder drawPrimitives:metalPrimType vertexStart:0 vertexCount:cmd.vertexCount];
        } else {
            id<MTLBuffer> indexBuffer = [self.commandQueue.device newBufferWithBytes:cmd.indexData.data()
                                                                               length:cmd.indexData.size()
                                                                              options:MTLResourceStorageModeShared];
            if (!indexBuffer) {
                fprintf(stderr, "[renderer] failed to create index buffer (%zu bytes), skipping draw\n", cmd.indexData.size());
                continue;
            }
            MTLIndexType indexType = cmd.indexIs32Bit ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
            [encoder drawIndexedPrimitives:metalPrimType
                                 indexCount:cmd.indexCount
                                  indexType:indexType
                                indexBuffer:indexBuffer
                          indexBufferOffset:0];
        }
    }

    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];

    g_drawnFrameCount.fetch_add(1, std::memory_order_relaxed);
}

@end

@interface BigBumpinWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation BigBumpinWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    // Real, expected behavior: closing the window ends the program
    // immediately (see Renderer_WasClosedByUser() and its caller in
    // main.cpp), not just its rendering -- a reasonable person who closes
    // the window does not expect the process to keep running invisibly
    // for up to the remaining watchdog bound.
    g_closedByUser.store(true, std::memory_order_relaxed);
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
        // Programmatically-created NSWindows default to YES here, which
        // under ARC can leave this strong global dangling once the window
        // closes (the window releases itself on close, ARC's own strong
        // reference then points at deallocated memory). Keep ARC as the
        // sole owner instead.
        g_window.releasedWhenClosed = NO;

        g_windowDelegate = [[BigBumpinWindowDelegate alloc] init];
        g_window.delegate = g_windowDelegate;

        g_view = [[MTKView alloc] initWithFrame:frame device:device];
        g_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;

        g_renderDelegate = [[BigBumpinRendererDelegate alloc] init];
        g_renderDelegate.commandQueue = [device newCommandQueue];
        g_view.delegate = g_renderDelegate;

        NSError *libraryError = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:@(kShaderSource)
                                                        options:nil
                                                          error:&libraryError];
        if (!library) {
            fprintf(stderr, "[renderer] failed to compile placeholder shader: %s\n",
                libraryError.localizedDescription.UTF8String);
            return false;
        }

        MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
        vertexDescriptor.attributes[0].format = MTLVertexFormatFloat3;
        vertexDescriptor.attributes[0].offset = 0;
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.layouts[0].stride = 12;

        MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = [library newFunctionWithName:@"vertex_main"];
        pipelineDescriptor.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
        pipelineDescriptor.vertexDescriptor = vertexDescriptor;
        pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        NSError *pipelineError = nil;
        g_drawPipelineState = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&pipelineError];
        if (!g_drawPipelineState) {
            fprintf(stderr, "[renderer] failed to create placeholder pipeline state: %s\n",
                pipelineError.localizedDescription.UTF8String);
            return false;
        }

        [g_window setContentView:g_view];
        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        g_initialized.store(true, std::memory_order_relaxed);
        return true;
    }
}

void Renderer_RunEventLoop() {
    [NSApp run];
    // By the time [NSApp run] returns, no further drawInMTKView: calls can
    // race this read -- the only paths that make it return are the
    // shutdown ones above, all of which stop real drawing first. These
    // counts are the real, non-visual evidence (for environments with no
    // attached display to look at) that the render loop actually ran and
    // that VdSwap's present-signal actually reached it, rather than just
    // "the process didn't crash".
    fprintf(stderr, "[renderer] event loop stopped -- drawn frames: %llu, present signals received: %llu\n",
        (unsigned long long)g_drawnFrameCount.load(std::memory_order_relaxed),
        (unsigned long long)g_presentSignalCount.load(std::memory_order_relaxed));
}

void Renderer_PostPresentSignal() {
    g_presentSignalCount.fetch_add(1, std::memory_order_relaxed);
}

bool Renderer_IsActive() {
    return g_initialized.load(std::memory_order_relaxed);
}

void Renderer_RequestShutdown() {
    g_shutdownRequested.store(true, std::memory_order_relaxed);
    // Call StopApplication() on the main queue directly, rather than
    // relying solely on the next drawInMTKView: tick noticing the flag
    // above. MTKView's display-link callbacks can pause (window
    // minimized, fully occluded, display asleep) independent of whether
    // the window is still open -- the main queue is serviced by the
    // AppKit run loop regardless, so this guarantees the watchdog's own
    // "terminate the run loop" requirement is met even in those states.
    dispatch_async(dispatch_get_main_queue(), ^{
        StopApplication();
    });
}

bool Renderer_WasClosedByUser() {
    return g_closedByUser.load(std::memory_order_relaxed);
}
