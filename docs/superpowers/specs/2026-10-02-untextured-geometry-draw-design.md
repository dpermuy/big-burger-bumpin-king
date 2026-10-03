# Untextured Geometry Draw Path — Design Spec

## Context

Sub-project 2 of 3 toward real PM4 draw-call rendering, following the
GPU register state tracking sub-project (merged). That sub-project's
`GpuRegisterState` (`host/gpu_state.h/.cpp`) already tracks real register
writes with two confirmed-live decoded accessors: `GetDrawInitiator()`
(real `DRAW_INDX_2` packets observed decoding to `primType=1/numIndices=1`
and `primType=8/numIndices=3`) and `GetVertexFetchConstant(slot)` (real
vertex fetch constant addresses confirmed landing inside the game's
actual allocated physical RAM). `GpuCommandTracer::RegisterState()`
exposes a locked snapshot. `PM4_DRAW_INDX_2` itself still does nothing
beyond mirroring its draw-initiator dword into register state — no
vertex data is read, no Metal draw call happens.

Separately, the already-merged Metal renderer milestone gave this project
a real `NSWindow`/`MTKView`/`MTLDevice`/`MTLCommandQueue`
(`host/renderer_metal.h/.mm`), currently only clearing to a fixed debug
color each present — no real draw commands are ever submitted to it.

This sub-project connects the two: real `PM4_DRAW_INDX_2` execution,
resolving real vertex (and, for indexed draws, index) data from guest
memory, rendered with a **hardcoded placeholder shader** (passthrough
position, solid color) rather than real shader translation (sub-project
3, deferred — Xenos microcode → MSL is normally the hardest, most
speculative part of any Xbox 360 GPU reimplementation).

## Goals

- Execute real `PM4_DRAW_INDX_2` packets: resolve vertex data (and index
  data, for indexed draws) from guest memory, and issue a real Metal draw
  call per packet.
- Get real game geometry visibly on screen for the first time, even if
  its exact shape/position is only approximately correct (real vertex
  *data* with a hardcoded, probably-wrong *interpretation* of its
  format — see Non-goals).
- Keep the PM4-parsing thread (the GPU pump thread) and the Metal
  rendering thread (the AppKit main thread) correctly separated, the same
  threading boundary the Metal renderer milestone already established.

## Non-goals

- No real shader translation (sub-project 3). Position data is drawn
  as-is with no camera/projection transform; color is a fixed solid value.
- No real vertex *format* detection. Real Xbox 360 vertex fetch is driven
  by shader microcode ("vfetch" instructions) that isn't parsed yet —
  this sub-project hardcodes an explicit, documented, known-provisional
  assumption instead (see Vertex format assumption below).
- No texture sampling, no primitive types without a direct Metal
  equivalent (rectangle lists, quad lists, etc. — logged and skipped, not
  rendered).
- No change to the existing headless regression path or to Milestone 1's
  own clear-color behavior when no real draws exist for a frame.

## Architecture

**New files:** `host/gpu_draw_list.h/.cpp` — plain data structures with
no Metal, PM4, or Objective-C dependency, matching this project's
established file-separation discipline (PM4 parsing / Metal specifics /
plain shared data each get their own files, so a future second rendering
backend would be a new file, not a rewrite).

```cpp
enum class DrawPrimitiveType { Point, Line, LineStrip, Triangle, TriangleStrip };

struct DrawCommand
{
    DrawPrimitiveType primitiveType;
    std::vector<uint8_t> vertexData;   // already copied + byte-swapped host floats
    uint32_t vertexCount;
    std::vector<uint8_t> indexData;    // empty for non-indexed (kAutoIndex) draws
    uint32_t indexCount;
    bool indexIs32Bit;                 // only meaningful if indexData is non-empty
};

class FrameDrawList
{
public:
    void AddDrawCommand(DrawCommand&& cmd);  // pump thread, while building
    void SwapReady();                        // pump thread, at VdSwap
    std::vector<DrawCommand> TakeReady();    // render thread, drains and clears
private:
    std::mutex mutex_;
    std::vector<DrawCommand> building_;
    std::vector<DrawCommand> ready_;
};
```

**Threading:** `GpuCommandTracer::ScanBuffer` (pump thread) calls
`frameDrawList_.AddDrawCommand(...)` for each real, supported
`PM4_DRAW_INDX_2` it parses — this is where guest memory is read, copied,
and byte-swapped (guest memory can be reused/overwritten by the time a
render thread would get to it later, so the copy must happen eagerly, on
the thread that has the real guest-memory pointer and offset in hand).
`VdSwap` (`host/kernel_impl.cpp`) calls `frameDrawList_.SwapReady()`
right alongside its existing `Renderer_PostPresentSignal()` call, moving
`building_` into `ready_` under lock. `drawInMTKView:`
(`host/renderer_metal.mm`, render thread) calls `TakeReady()` once per
present and issues one real Metal draw call per entry — this is the only
place `MTLBuffer`s get created, keeping every real Metal API call on the
thread Milestone 1 already established for them.

`FrameDrawList` is a new member of `GpuCommandTracer`
(`host/gpu_trace.h`), alongside the existing `GpuRegisterState gpuState_`.

## Vertex format assumption

Real vertex fetch is shader-driven (vfetch instructions reference a
specific fetch-constant slot per attribute) and that microcode isn't
parsed until sub-project 3. This sub-project hardcodes: **vertex fetch
constant slot 0 is a tightly-packed stream of 3 packed 32-bit floats per
vertex (x, y, z), stride 12 bytes** — the most common real D3D9
position-only/position-first vertex-buffer convention. This is an
explicit, temporary, documented assumption; sub-project 3's real shader
translation replaces it with the real per-shader vertex format. Any
vertex position's correctness beyond "some real geometry shape is
visible" is not a goal of this sub-project.

Real Xbox 360 floats are big-endian; each 4-byte component is
byte-swapped during the guest-to-host copy (`__builtin_bswap32`,
reinterpreted as `float` — the same pattern `gpu_trace.cpp`'s existing
`LoadU32` already uses for `uint32_t`).

## Data flow

1. `ScanBuffer` sees `PM4_DRAW_INDX_2` (as today, mirrors the
   draw-initiator dword into `gpuState_`), then additionally:
2. Reads `GetDrawInitiator()`: `primType`, `sourceSelect`, `indexSize`,
   `numIndices`.
3. Maps `primType` to a `DrawPrimitiveType`. No mapping exists (rectangle
   list, quad list, etc.) → log and return, no draw added.
4. `sourceSelect == kImmediate` → log and return (unsupported, matches
   Xenia itself).
5. Reads `GetVertexFetchConstant(0)`. Computes the real byte address
   (`(address << 2) | 0xA0000000`, this project's established segment
   convention) and real byte size (`size` field, in 32-bit words, × 4).
   Bounds-check the size against a sane cap (16MB) — a corrupt/absurd
   declared size is logged and the draw is skipped, not allocated.
   Copies and byte-swaps that many bytes from guest memory into
   `DrawCommand::vertexData`. `vertexCount` = byte size / 12.
6. `sourceSelect == kAutoIndex`: `indexData` stays empty; the draw uses
   implicit vertex indices `0..numIndices-1`.
   `sourceSelect == kDMA`: also resolves and copies the real index
   buffer (`VGT_DMA_BASE`/`VGT_DMA_SIZE` — already read directly from the
   `DRAW_INDX_2`/`DRAW_INDX` packet payload itself, same as the
   draw-initiator dword; `index_size` from the draw initiator selects
   16-bit vs. 32-bit indices), byte-swapped the same way.
7. The resulting `DrawCommand` is appended via `AddDrawCommand`.
8. At `VdSwap`, `SwapReady()` moves the accumulated list to `ready_`.
9. `drawInMTKView:` calls `TakeReady()`, and for each `DrawCommand`:
   creates one `MTLBuffer` from `vertexData` (and one from `indexData` if
   non-empty) via `newBufferWithBytes:length:options:`, binds the
   placeholder pipeline state, and calls `drawPrimitives:` (non-indexed)
   or `drawIndexedPrimitives:` (indexed).

## Metal pipeline

One new minimal MSL vertex/fragment function pair, compiled into the
existing Metal device at `Renderer_Init`: the vertex function takes a
`float3` position attribute and outputs it directly as clip-space
position (no transform — real camera/projection matrices are shader
constants, untranslated until sub-project 3); the fragment function
outputs a fixed solid color (e.g. opaque white), chosen to be visually
distinct from the existing cornflower-blue clear color so real geometry
is unambiguous against the background. One `MTLRenderPipelineState` is
built once and reused for every draw command — all commands share the
same placeholder shader and vertex layout (a single `float3` attribute,
matching the Vertex format assumption above).

## Error handling

- Unsupported primitive type (no `DrawPrimitiveType` mapping): log once
  per occurrence (not rate-limited — this is expected, frequent, and
  informative, not a bounds-safety concern like `GpuRegisterState`'s
  out-of-range path), skip the draw.
- `kImmediate` source select: log and skip, matching Xenia's own
  unsupported path.
- Vertex buffer declared size exceeds the 16MB sanity cap: log and skip
  the draw, do not allocate.
- Any Metal object creation failure (`newBufferWithBytes:` returning
  `nil`, which Apple's docs note can happen under real memory pressure):
  log and skip that draw command, do not crash the render loop — matches
  this project's established "hard-fail only on genuinely unrecoverable
  setup, tolerate per-frame anomalies" posture (Milestone 1 hard-fails
  only on no-GPU-device at `Renderer_Init`, never mid-loop).

## Testing

1. **Headless regression unchanged** — this entire path only activates
   under `--window` (the only place `drawInMTKView:` ever runs); the
   default headless run must stay byte-for-byte identical to the
   established baseline (153 `NtReadFile` lines, same watchdog-timeout
   tail message, same exit code).
2. **Live `--window` run, non-visual evidence** — temporary (or
   permanent, low-volume) logging of real per-frame draw-list contents:
   how many `DrawCommand`s were added, how many were skipped (and why:
   unsupported primitive type vs. `kImmediate` vs. oversized buffer),
   real vertex/index counts. Cross-check against sub-project 1's own
   already-confirmed real decode values (`primType=1/numIndices=1`,
   `primType=8/numIndices=3` — note `primType=8` is a rectangle list,
   which this sub-project explicitly skips, so expect it to show up in
   the skip count, not the draw count).
3. **Visual confirmation (manual, by the user)** — same pattern as
   Milestone 1: this sandboxed environment cannot screenshot or verify
   pixels. Request a manual `--window` run to confirm real geometry
   (whatever shape results from the hardcoded vertex-format assumption)
   is visible and stable against the cornflower-blue background, with no
   crash or corruption over a short run.
