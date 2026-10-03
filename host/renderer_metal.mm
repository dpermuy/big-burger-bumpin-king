#include "renderer_metal.h"
#include "gpu_trace.h"
#include "shader_translate.h"

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {
const char* kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
};

struct RasterizerData {
    float4 position [[position]];
    float pointSize [[point_size]];
};

vertex RasterizerData vertex_main(VertexIn in [[stage_in]]) {
    RasterizerData out;
    out.position = float4(in.position, 1.0);
    // Final review finding I3: Metal leaves point size undefined without
    // an explicit [[point_size]] output. This placeholder shader draws
    // point lists (this project's own observed real primitive type),
    // so a fixed, visible size matters for the "is anything on screen"
    // milestone goal -- real point-size state (if any) is untranslated
    // until shader translation exists.
    out.pointSize = 8.0;
    return out;
}

fragment float4 fragment_main() {
    return float4(1.0, 1.0, 1.0, 1.0);
}
)";

id<MTLRenderPipelineState> g_drawPipelineState = nil;
uint64_t g_lastCompiledRealShaderHashPair = 0; // 0 = nothing real compiled yet; combines vertex+pixel hashes
// Final review finding I2: TakeReady() drains and clears every tick, so
// a real draw batch (often just one, observed live as a single early
// burst of PM4 traffic) would otherwise flash for one display-link tick
// (~16ms) and then vanish on every later tick that has nothing new.
// Cache the last non-empty batch and keep redrawing it until a newer one
// replaces it -- this does not fabricate geometry, it only makes a real,
// already-decoded batch observable for longer than one tick, matching
// the milestone's own "visible and stable" testing goal.
std::vector<DrawCommand> g_lastDrawCommands;
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

void TryUpdateRealPipeline(id<MTLDevice> device)
{
    uint32_t vsHash = g_gpuTracer.ShaderTranslation().CurrentVertexShaderHash();
    uint32_t psHash = g_gpuTracer.ShaderTranslation().CurrentPixelShaderHash();
    TranslationResult vs = g_gpuTracer.ShaderTranslation().CurrentVertexShader();
    TranslationResult ps = g_gpuTracer.ShaderTranslation().CurrentPixelShader();

    if (!vs.success || !ps.success)
    {
        return; // keep whatever pipeline is already running
    }

    uint64_t combinedHash = (uint64_t(vsHash) << 32) | uint64_t(psHash);
    if (combinedHash == g_lastCompiledRealShaderHashPair)
    {
        return; // already compiled this exact pair
    }

    std::string fullSource = "#include <metal_stdlib>\nusing namespace metal;\n";
    fullSource += vs.vertexShaderSource;
    fullSource += ps.fragmentShaderSource;

    NSError *libraryError = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@(fullSource.c_str())
                                                    options:nil
                                                      error:&libraryError];
    if (!library)
    {
        fprintf(stderr, "[renderer] real shader compile failed: %s\n",
            libraryError.localizedDescription.UTF8String);
        return; // keep whatever pipeline is already running
    }

    MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    for (size_t i = 0; i < vs.attributes.size(); i++)
    {
        const TranslatedAttribute &attr = vs.attributes[i];
        vertexDescriptor.attributes[i].format = (attr.format == TranslatedVertexFormat::Float3)
            ? MTLVertexFormatFloat3 : MTLVertexFormatFloat4;
        vertexDescriptor.attributes[i].offset = attr.byteOffset;
        vertexDescriptor.attributes[i].bufferIndex = 0;
    }
    vertexDescriptor.layouts[0].stride = vs.vertexStrideBytes;

    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = [library newFunctionWithName:@"vertex_main"];
    pipelineDescriptor.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
    pipelineDescriptor.vertexDescriptor = vertexDescriptor;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    NSError *pipelineError = nil;
    id<MTLRenderPipelineState> newPipeline = [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&pipelineError];
    if (!newPipeline)
    {
        fprintf(stderr, "[renderer] real pipeline state creation failed: %s\n",
            pipelineError.localizedDescription.UTF8String);
        return; // keep whatever pipeline is already running
    }

    g_drawPipelineState = newPipeline;
    g_lastCompiledRealShaderHashPair = combinedHash;
    fprintf(stderr, "[renderer] now using real translated shader (vsHash=0x%X psHash=0x%X)\n", vsHash, psHash);
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
    // Sub-project 2 (untextured geometry draw path) now executes real
    // PM4_DRAW_INDX_2 draws with a placeholder shader -- see below.
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.392, 0.584, 0.929, 1.0);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];

    std::vector<DrawCommand> newDrawCommands = g_gpuTracer.DrawList().TakeReady();
    if (!newDrawCommands.empty()) {
        g_lastDrawCommands = std::move(newDrawCommands);
    }
    TryUpdateRealPipeline(self.commandQueue.device);
    [encoder setRenderPipelineState:g_drawPipelineState];
    for (const DrawCommand &cmd : g_lastDrawCommands) {
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
