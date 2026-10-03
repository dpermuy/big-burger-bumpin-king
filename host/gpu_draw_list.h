#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

// Real Metal-mappable primitive types this project can actually draw.
// Xenos primitive types with no modern Metal equivalent (triangle fan,
// rectangle list, quad list, etc.) never produce a DrawCommand -- see
// gpu_trace.cpp's PM4_DRAW_INDX_2 handling.
enum class DrawPrimitiveType
{
    Point,
    Line,
    LineStrip,
    Triangle,
    TriangleStrip
};

// One real, fully-resolved draw: vertex (and optionally index) data
// already copied and byte-swapped out of guest memory by the pump
// thread, ready for the render thread to turn into real MTLBuffers and
// a real draw call. No Metal or PM4 dependency here -- this is pure,
// Metal-agnostic data, the same file-separation discipline this
// project's Metal renderer milestone established (a future second
// rendering backend consumes this same type).
struct DrawCommand
{
    DrawPrimitiveType primitiveType;
    std::vector<uint8_t> vertexData;  // tightly-packed float3 positions
    uint32_t vertexCount;
    std::vector<uint8_t> indexData;   // empty for non-indexed draws
    uint32_t indexCount;
    bool indexIs32Bit;                // only meaningful if indexData is non-empty
};

// Carries real draw commands from the GPU pump thread (where PM4 is
// parsed and guest memory is read) to the Metal render thread (where
// MTLBuffers are created and real draws are issued). Double-buffered:
// AddDrawCommand accumulates into a "building" list as the pump thread
// parses a frame's worth of PM4 traffic; SwapReady (called once per
// VdSwap) atomically moves that list to "ready" for the render thread
// to drain via TakeReady, matching how a real frame's geometry actually
// builds up before being presented as a whole.
class FrameDrawList
{
public:
    // Pump thread, while parsing a frame's PM4 traffic.
    void AddDrawCommand(DrawCommand&& cmd);

    // Pump thread, once per VdSwap -- moves the accumulated commands to
    // the ready list for the render thread, and starts a fresh building
    // list for the next frame.
    void SwapReady();

    // Render thread, once per present -- returns and clears the ready
    // list. Returns an empty vector if nothing is ready (e.g. a frame
    // with only unsupported/skipped draws, or called more than once
    // before the next SwapReady).
    std::vector<DrawCommand> TakeReady();

private:
    std::mutex mutex_;
    std::vector<DrawCommand> building_;
    std::vector<DrawCommand> ready_;
};
