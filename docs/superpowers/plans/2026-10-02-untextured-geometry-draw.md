# Untextured Geometry Draw Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute real `PM4_DRAW_INDX_2` packets end to end: resolve real vertex (and, for indexed draws, index) data from guest memory, hand it across the pump-thread/render-thread boundary, and issue real Metal draw calls with a hardcoded placeholder shader — getting real game geometry visible on screen for the first time.

**Architecture:** A new, Metal/PM4-free `FrameDrawList` (double-buffered, mutex-guarded) carries `DrawCommand`s from `GpuCommandTracer::ScanBuffer` (pump thread, where `DRAW_INDX_2` is parsed and guest memory is copied+byte-swapped) to `drawInMTKView:` (render thread, where `MTLBuffer`s are created and real draw calls issued) — the same threading boundary the Metal renderer milestone already established. One hardcoded placeholder `MTLRenderPipelineState` (passthrough position, solid white) handles every draw this sub-project produces.

**Tech Stack:** C++17, Objective-C++/Metal Shading Language (inline MSL string, no `.metal` file), this project's existing PM4-parsing/byte-swap conventions.

**Spec:** `docs/superpowers/specs/2026-10-02-untextured-geometry-draw-design.md`

## Global Constraints

- Vertex format assumption: fetch-constant slot 0 is a tightly-packed stream of 3 packed 32-bit floats per vertex (x, y, z), stride 12 bytes.
- Real byte address from a fetch constant / `VGT_DMA_BASE`: `(address << 2) | 0xA0000000` (this project's established guest physical-address segment convention). `VGT_DMA_BASE` is already a byte address (not `<<2`'d) per Xenia's own code — only the fetch-constant's `address` field is in dwords.
- Vertex buffer size sanity cap: 16MB. A declared size beyond this is logged and the draw is skipped, not allocated.
- Primitive type mapping (real Xenia `xenos::PrimitiveType` values, confirmed against `xenos.h`): `kPointList`(1)→Point, `kLineList`(2)→Line, `kLineStrip`(3)→LineStrip, `kTriangleList`(4)→Triangle, `kTriangleStrip`(6)→TriangleStrip. Everything else (including `kTriangleFan`=5, which has no modern Metal equivalent, and `kRectangleList`=8, confirmed real and common in this project's own captured trace) is unsupported: log and skip.
- `sourceSelect` values (from `DrawInitiator`, already decoded by sub-project 1): 0=`kDMA` (indexed), 1=`kImmediate` (unsupported, matches Xenia itself — log and skip), 2=`kAutoIndex` (non-indexed, implicit indices `0..numIndices-1`).
- This sub-project only adds real drawing to `PM4_DRAW_INDX_2` (opcode `0x36`). `PM4_DRAW_INDX` (opcode `0x22`) keeps its existing sub-project-1 behavior (draw-initiator register mirror only, no drawing) — it has never been observed in this project's real captured trace (confirmed: 49 real `DRAW_INDX_2` packets, 0 `DRAW_INDX`).
- Headless regression baseline that must stay byte-for-byte unchanged throughout: 153 `NtReadFile` lines, tail message ending `"_xstart did not return within 10 seconds (watchdog timeout) -- this is an expected, informative outcome for Phase 2A, not a crash."`, exit code 2. Test command: `./build/BigBumpinHost > run.log 2>&1; echo "exit: $?"; grep -c NtReadFile run.log; tail -3 run.log`.
- `--window` regression baseline that must stay unchanged except for the new, documented behavior: exit code 2, `[renderer] event loop stopped -- drawn frames: N, present signals received: M` line still printed (Milestone 1's own counters are untouched by this sub-project).

## Review Focus

- A `DRAW_INDX_2` with `sourceSelect == kDMA` but a corrupt/zero `VGT_DMA_SIZE.num_words` — a reasonable implementation draws zero indices or skips cleanly, not a huge allocation or an out-of-bounds read. Task 3's index-buffer test exercises this.
- `GetVertexFetchConstant(0)`'s declared `size` field describing a buffer that extends past the actual guest memory region (e.g. a corrupt or stale fetch constant from before the real vertex buffer was set up) — the copy must not read past `base`'s real allocated extent. Task 3 bounds the copy to the 16MB cap but does not itself validate against the real guest allocation's extent; ledgered as a known limitation (matches `LoadU32`'s own existing unchecked-read pattern used throughout `gpu_trace.cpp` already, not a new gap this sub-project introduces).
- Two `DRAW_INDX_2` packets in the same frame with different primitive types or vertex data — `FrameDrawList` must keep them as separate `DrawCommand` entries, not merge or overwrite. Task 1's smoke test exercises `AddDrawCommand` being called multiple times before a `SwapReady`.
- `TakeReady()` called when nothing was added since the last swap (e.g. a frame with only unsupported/skipped draws) — must return an empty list cleanly, not a stale previous frame's commands. Task 1's smoke test exercises this.
- `newBufferWithBytes:` returning `nil` for a zero-length `vertexData` (an edge case possible if a vertex fetch constant's declared size rounds down to zero after the stride-12 division) — Task 4 skips the draw command rather than passing a zero-length/null buffer to the encoder.

---

## Task 1: `FrameDrawList` / `DrawCommand` core data structures (`host/gpu_draw_list.h/.cpp`)

**Files:**
- Create: `host/gpu_draw_list.h`
- Create: `host/gpu_draw_list.cpp`
- Modify: `CMakeLists.txt:28` (add `host/gpu_draw_list.cpp` to the `BigBumpinHost` source list)

**Interfaces:**
- Produces: `enum class DrawPrimitiveType { Point, Line, LineStrip, Triangle, TriangleStrip };`, `struct DrawCommand { DrawPrimitiveType primitiveType; std::vector<uint8_t> vertexData; uint32_t vertexCount; std::vector<uint8_t> indexData; uint32_t indexCount; bool indexIs32Bit; };`, `class FrameDrawList` with `void AddDrawCommand(DrawCommand&& cmd)`, `void SwapReady()`, `std::vector<DrawCommand> TakeReady()`.

- [ ] **Step 1: Write a standalone smoke-test main exercising the not-yet-existing types**

Create `host/gpu_draw_list_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "gpu_draw_list.h"
#include <cassert>
#include <cstdio>

int main()
{
    FrameDrawList list;

    // TakeReady with nothing ever added: empty, not a crash.
    assert(list.TakeReady().empty());

    // Add two commands, then swap -- both should appear together.
    DrawCommand cmd1;
    cmd1.primitiveType = DrawPrimitiveType::Point;
    cmd1.vertexData = {1, 2, 3, 4};
    cmd1.vertexCount = 1;
    cmd1.indexCount = 0;
    cmd1.indexIs32Bit = false;
    list.AddDrawCommand(std::move(cmd1));

    DrawCommand cmd2;
    cmd2.primitiveType = DrawPrimitiveType::Triangle;
    cmd2.vertexData = {5, 6, 7, 8, 9, 10};
    cmd2.vertexCount = 3;
    cmd2.indexCount = 0;
    cmd2.indexIs32Bit = false;
    list.AddDrawCommand(std::move(cmd2));

    list.SwapReady();
    std::vector<DrawCommand> ready = list.TakeReady();
    assert(ready.size() == 2);
    assert(ready[0].primitiveType == DrawPrimitiveType::Point);
    assert(ready[0].vertexData.size() == 4);
    assert(ready[1].primitiveType == DrawPrimitiveType::Triangle);
    assert(ready[1].vertexData.size() == 6);

    // TakeReady drains and clears -- a second call returns empty.
    assert(list.TakeReady().empty());

    // A SwapReady with nothing added since the last swap produces an
    // empty ready list, not a stale repeat of the previous frame.
    list.SwapReady();
    assert(list.TakeReady().empty());

    printf("gpu_draw_list_smoketest: PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (the types don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_draw_list_smoketest.cpp -o /tmp/gpu_draw_list_smoketest 2>&1`
Expected: FAIL — `'gpu_draw_list.h' file not found` (or equivalent; the header does not exist yet).

- [ ] **Step 3: Write `host/gpu_draw_list.h` and `host/gpu_draw_list.cpp`**

`host/gpu_draw_list.h`:
```cpp
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
```

`host/gpu_draw_list.cpp`:
```cpp
#include "gpu_draw_list.h"

void FrameDrawList::AddDrawCommand(DrawCommand&& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    building_.push_back(std::move(cmd));
}

void FrameDrawList::SwapReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = std::move(building_);
    building_.clear();
}

std::vector<DrawCommand> FrameDrawList::TakeReady()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DrawCommand> result = std::move(ready_);
    ready_.clear();
    return result;
}
```

- [ ] **Step 4: Compile and run the smoke test to verify it passes, then delete it**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_draw_list_smoketest.cpp host/gpu_draw_list.cpp -o /tmp/gpu_draw_list_smoketest && /tmp/gpu_draw_list_smoketest`
Expected: `gpu_draw_list_smoketest: PASS`, exit code 0.

```bash
rm host/gpu_draw_list_smoketest.cpp
```

- [ ] **Step 5: Add to the build, rebuild the real project, verify regression, commit**

Edit `CMakeLists.txt:28` — change:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/game_overrides.cpp host/renderer_metal.mm)
```
to:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/game_overrides.cpp host/renderer_metal.mm)
```

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/gpu_draw_list.h host/gpu_draw_list.cpp CMakeLists.txt
git commit -m "feat: add FrameDrawList/DrawCommand core data structures (unused by GpuCommandTracer yet)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — byte-for-byte the established baseline, since `gpu_draw_list.cpp` is compiled in but not yet referenced by any existing code path.

---

## Task 2: Wire `FrameDrawList` into `GpuCommandTracer` (`host/gpu_trace.h`)

**Files:**
- Modify: `host/gpu_trace.h` (add `#include "gpu_draw_list.h"`, add `frameDrawList_` member, add `DrawList()` accessor)

**Interfaces:**
- Consumes: `FrameDrawList` (Task 1).
- Produces: `FrameDrawList& GpuCommandTracer::DrawList()` (for `gpu_trace.cpp`'s own `PM4_DRAW_INDX_2` handling, `kernel_impl.cpp`'s `VdSwap`, and `renderer_metal.mm`'s `drawInMTKView:` to consume).

- [ ] **Step 1: Add the include, member, and accessor**

In `host/gpu_trace.h`, add near the top (after `#include "gpu_state.h"`):
```cpp
#include "gpu_draw_list.h"
```

Add to the `public:` section (after the `RegisterState()` accessor added in the prior sub-project):
```cpp
    FrameDrawList& DrawList() { return frameDrawList_; }
```

Add to the `private:` section (after `GpuRegisterState gpuState_;`):
```cpp
    FrameDrawList frameDrawList_;
```

- [ ] **Step 2: Rebuild, verify regression, commit**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/gpu_trace.h
git commit -m "feat: add FrameDrawList member and accessor to GpuCommandTracer (unused yet)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — unchanged, since `DrawList()` is not yet called from anywhere.

---

## Task 3: Resolve real vertex/index data and populate `FrameDrawList` (`host/gpu_trace.cpp`)

**Files:**
- Modify: `host/gpu_trace.cpp` (extend the existing `PM4_DRAW_INDX_2` case, currently lines 346-361)

**Interfaces:**
- Consumes: `GpuRegisterState::GetDrawInitiator()`, `GetVertexFetchConstant(slot)` (prior sub-project); `FrameDrawList::AddDrawCommand` (Task 1-2); `LoadU32` (existing file-local helper).
- Produces: real `DrawCommand`s appended to `gpuState_`'s sibling `frameDrawList_` for every supported, real `PM4_DRAW_INDX_2` packet.

- [ ] **Step 1: Replace the existing `PM4_DRAW_INDX_2` case with the full draw-resolution logic**

In `host/gpu_trace.cpp`, replace (currently lines 346-361):

```cpp
            if (opcode == kOpcodeDrawIndx2 && count >= 1)
            {
                // Real semantics (Xenia's ExecutePacketType3Draw): the
                // draw initiator travels inside the draw packet itself,
                // not as a pre-set register -- real hardware mirrors it
                // into the register file as a side effect of executing
                // the draw, which is what this write reproduces. DRAW_INDX_2
                // has no leading viz-query dword, so the initiator is the
                // packet's first payload dword. Final review finding I1
                // (this project's own prior assumption that TYPE0 writes
                // alone would populate this register was wrong -- live
                // data showed 49 real DRAW_INDX_2 packets and zero TYPE0
                // writes to this register).
                uint32_t drawInitiatorValue = LoadU32(base, bufferAddr + offsetBytes + 4);
                gpuState_.WriteRegister(GpuRegisterState::kDrawInitiatorRegister, drawInitiatorValue);
            }
```

with:

```cpp
            if (opcode == kOpcodeDrawIndx2 && count >= 1)
            {
                // Real semantics (Xenia's ExecutePacketType3Draw): the
                // draw initiator travels inside the draw packet itself,
                // not as a pre-set register -- real hardware mirrors it
                // into the register file as a side effect of executing
                // the draw, which is what this write reproduces. DRAW_INDX_2
                // has no leading viz-query dword, so the initiator is the
                // packet's first payload dword. Final review finding I1
                // (this project's own prior assumption that TYPE0 writes
                // alone would populate this register was wrong -- live
                // data showed 49 real DRAW_INDX_2 packets and zero TYPE0
                // writes to this register).
                uint32_t drawInitiatorValue = LoadU32(base, bufferAddr + offsetBytes + 4);
                gpuState_.WriteRegister(GpuRegisterState::kDrawInitiatorRegister, drawInitiatorValue);

                DrawInitiator di = gpuState_.GetDrawInitiator();

                // Only real Xenos primitive types with a direct Metal
                // equivalent produce a DrawCommand (confirmed real values
                // against Xenia's xenos::PrimitiveType). Notably
                // kTriangleFan (5) has no modern Metal equivalent, and
                // kRectangleList (8) is confirmed real and common in this
                // project's own captured trace but also unsupported here.
                bool havePrimType = true;
                DrawPrimitiveType mappedPrimType = DrawPrimitiveType::Point;
                switch (di.primType)
                {
                    case 1: mappedPrimType = DrawPrimitiveType::Point; break;
                    case 2: mappedPrimType = DrawPrimitiveType::Line; break;
                    case 3: mappedPrimType = DrawPrimitiveType::LineStrip; break;
                    case 4: mappedPrimType = DrawPrimitiveType::Triangle; break;
                    case 6: mappedPrimType = DrawPrimitiveType::TriangleStrip; break;
                    default: havePrimType = false; break;
                }

                if (!havePrimType)
                {
                    if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: unsupported primType=%u, skipped\n", indent, di.primType);
                }
                else if (di.sourceSelect == 1)
                {
                    // kImmediate: unsupported even by Xenia itself.
                    if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: kImmediate source select unsupported, skipped\n", indent);
                }
                else
                {
                    VertexFetchConstant vfc = gpuState_.GetVertexFetchConstant(0);
                    uint32_t vertexByteAddr = (vfc.address << 2) | 0xA0000000u;
                    uint32_t vertexByteSize = vfc.size * 4;
                    constexpr uint32_t kMaxVertexBufferBytes = 16u * 1024u * 1024u;

                    if (vertexByteSize == 0 || vertexByteSize > kMaxVertexBufferBytes)
                    {
                        if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: vertex buffer size %u bytes out of sane range, skipped\n", indent, vertexByteSize);
                    }
                    else
                    {
                        DrawCommand cmd;
                        cmd.primitiveType = mappedPrimType;
                        cmd.vertexData.resize(vertexByteSize);
                        for (uint32_t i = 0; i < vertexByteSize; i += 4)
                        {
                            uint32_t floatBits = LoadU32(base, vertexByteAddr + i);
                            cmd.vertexData[i + 0] = (floatBits >> 0) & 0xFF;
                            cmd.vertexData[i + 1] = (floatBits >> 8) & 0xFF;
                            cmd.vertexData[i + 2] = (floatBits >> 16) & 0xFF;
                            cmd.vertexData[i + 3] = (floatBits >> 24) & 0xFF;
                        }
                        cmd.vertexCount = vertexByteSize / 12;
                        cmd.indexCount = di.numIndices;
                        cmd.indexIs32Bit = (di.indexSize == 1);

                        if (di.sourceSelect == 0 && count >= 3)
                        {
                            // kDMA: real index buffer base/size travel in
                            // this same packet's next two payload dwords
                            // (Xenia's ExecutePacketType3Draw). VGT_DMA_BASE
                            // is already a byte address (unlike the fetch
                            // constant's dword-granular address field).
                            uint32_t dmaBase = LoadU32(base, bufferAddr + offsetBytes + 8);
                            uint32_t dmaSizeValue = LoadU32(base, bufferAddr + offsetBytes + 12);
                            uint32_t numWords = dmaSizeValue & 0xFFFFFF;
                            uint32_t indexWidthBytes = cmd.indexIs32Bit ? 4 : 2;
                            uint32_t indexByteAddr = dmaBase | 0xA0000000u;
                            uint32_t indexByteSize = numWords * indexWidthBytes;

                            if (indexByteSize > 0 && indexByteSize <= kMaxVertexBufferBytes)
                            {
                                cmd.indexData.resize(indexByteSize);
                                if (cmd.indexIs32Bit)
                                {
                                    for (uint32_t i = 0; i + 4 <= indexByteSize; i += 4)
                                    {
                                        uint32_t v = LoadU32(base, indexByteAddr + i);
                                        cmd.indexData[i + 0] = (v >> 0) & 0xFF;
                                        cmd.indexData[i + 1] = (v >> 8) & 0xFF;
                                        cmd.indexData[i + 2] = (v >> 16) & 0xFF;
                                        cmd.indexData[i + 3] = (v >> 24) & 0xFF;
                                    }
                                }
                                else
                                {
                                    for (uint32_t i = 0; i + 2 <= indexByteSize; i += 2)
                                    {
                                        uint16_t v = __builtin_bswap16(*reinterpret_cast<volatile uint16_t*>(base + indexByteAddr + i));
                                        cmd.indexData[i + 0] = v & 0xFF;
                                        cmd.indexData[i + 1] = (v >> 8) & 0xFF;
                                    }
                                }
                            }
                        }

                        if (logFile_)
                        {
                            fprintf(logFile_, "%s-> DRAW_INDX_2: primType=%u vertexCount=%u indexCount=%u (indexed=%s)\n",
                                indent, di.primType, cmd.vertexCount, cmd.indexCount, cmd.indexData.empty() ? "no" : "yes");
                        }
                        frameDrawList_.AddDrawCommand(std::move(cmd));
                    }
                }
            }
```

- [ ] **Step 2: Add the `#include "gpu_draw_list.h"` this code needs**

`gpu_trace.cpp` already transitively includes it via `gpu_trace.h` (Task 2) — no new include needed in the `.cpp` file. `DrawCommand`/`DrawPrimitiveType` are visible through that same chain.

- [ ] **Step 3: Build and run live, verify the regression baseline and real draw-list output**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log
./build/BigBumpinHost --window > run2.log 2>&1
echo "exit: $?"
grep "DRAW_INDX_2:" gpu_trace.log | sort | uniq -c | head -20
rm -f run2.log gpu_trace.log
```
Expected: headless run unchanged (exit 2, `153`, same tail message — this path never runs headless, since no PM4 buffer gets registered before the watchdog fires in that mode, matching every prior sub-project's own observation). `--window` run exits 2 (unchanged). The `DRAW_INDX_2:` lines should show two distinct real patterns matching this project's own already-confirmed real decode values: `primType=1 vertexCount=... indexCount=1 (indexed=no)` (point list, `sourceSelect=2`/kAutoIndex, real) and `-> DRAW_INDX_2: unsupported primType=8, skipped` (rectangle list, confirmed real and common, correctly skipped rather than drawn). If the counts or primType values differ from this, investigate against `GetDrawInitiator()`'s own already-unit-tested decode (sub-project 1, Task 2) before assuming this task's new code is wrong.

- [ ] **Step 4: Commit**

```bash
git add host/gpu_trace.cpp
git commit -m "feat: resolve real vertex/index data for PM4_DRAW_INDX_2 into FrameDrawList

Live-verified (--window run): real point-list draws (primType=1,
sourceSelect=kAutoIndex) produce a DrawCommand; real rectangle-list
draws (primType=8, confirmed common in this project's own captured
trace) are correctly logged and skipped as unsupported.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```

---

## Task 4: Swap the draw list at `VdSwap` (`host/kernel_impl.cpp`)

**Files:**
- Modify: `host/kernel_impl.cpp:1605-1608`

**Interfaces:**
- Consumes: `GpuCommandTracer::DrawList()` (Task 2), `FrameDrawList::SwapReady()` (Task 1).

- [ ] **Step 1: Add the `SwapReady()` call alongside the existing present-signal call**

In `host/kernel_impl.cpp`, replace (currently lines 1605-1608):

```cpp
    if (Renderer_IsActive())
    {
        Renderer_PostPresentSignal();
    }
```

with:

```cpp
    if (Renderer_IsActive())
    {
        Renderer_PostPresentSignal();
    }
    // Sub-project 2: move this frame's accumulated real draw commands to
    // the "ready" list for the render thread to pick up at the next
    // present. Called unconditionally (matches the existing
    // PPC_STORE_U32 reset just above this block) -- SwapReady() is cheap
    // (two vector moves under a short-held mutex) even when nothing was
    // added, so there's no headless-mode cost concern here the way
    // Renderer_PostPresentSignal has with Renderer_IsActive().
    g_gpuTracer.DrawList().SwapReady();
```

- [ ] **Step 2: Build and verify regression**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
```
Expected: clean build, exit code 2, `153`, same tail message — unchanged (this is a cheap, unconditional, headless-safe call; no behavior anyone can observe changes here, since `TakeReady()` isn't called from anywhere yet).

- [ ] **Step 3: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "feat: swap FrameDrawList at VdSwap (not yet consumed by the renderer)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```

---

## Task 5: Placeholder Metal pipeline and real draw submission (`host/renderer_metal.mm`)

**Files:**
- Modify: `host/renderer_metal.mm`

**Interfaces:**
- Consumes: `g_gpuTracer.DrawList().TakeReady()` (Tasks 1-4, via `#include "gpu_trace.h"`), `DrawCommand`/`DrawPrimitiveType` (Task 1).

- [ ] **Step 1: Add the `gpu_trace.h` include**

In `host/renderer_metal.mm`, add after the existing `#include "renderer_metal.h"`:
```cpp
#include "gpu_trace.h"
```

- [ ] **Step 2: Add the MSL shader source and a file-local pipeline state**

In `host/renderer_metal.mm`, add to the existing anonymous `namespace { ... }` block near the top (alongside `g_presentSignalCount` etc.):

```cpp
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
```

In the existing `namespace { NSWindow *g_window = nil; ... }` block (near the bottom of the file, before `Renderer_Init`), add:
```cpp
id<MTLRenderPipelineState> g_drawPipelineState = nil;
```

- [ ] **Step 3: Build the pipeline state in `Renderer_Init`**

In `host/renderer_metal.mm`, inside `Renderer_Init`'s `@autoreleasepool` block, after the existing `g_renderDelegate.commandQueue = [device newCommandQueue];` line and before `[g_window setContentView:g_view];`, add:

```cpp
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
```

- [ ] **Step 4: Issue real draw calls in `drawInMTKView:`**

In `host/renderer_metal.mm`, inside `drawInMTKView:`, after the existing `id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];` line and before `[encoder endEncoding];`, add:

```cpp
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
```

Also add `#include <vector>` to the file's existing `#include <atomic>` / `#include <cstdio>` block.

- [ ] **Step 5: Build and run live, verify headless regression and `--window` output**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log
./build/BigBumpinHost --window > run2.log 2>&1
echo "exit: $?"
grep "renderer\]" run2.log
rm -f run2.log gpu_trace.log
```
Expected: headless run unchanged (exit 2, `153`, same tail message). `--window` run exits 2 (unchanged), no `failed to compile placeholder shader` or `failed to create placeholder pipeline state` error lines, and the existing `[renderer] event loop stopped -- drawn frames: N, present signals received: M` line still appears with healthy nonzero counts (Milestone 1's own counters, confirming the render loop as a whole is still healthy with real draw calls now happening inside it).

- [ ] **Step 6: Commit**

```bash
git add host/renderer_metal.mm
git commit -m "feat: real Metal draw submission for PM4_DRAW_INDX_2 with a placeholder shader

Placeholder vertex shader passes real vertex positions through with no
transform; fragment shader outputs solid white, distinct from the
existing cornflower-blue clear color. Real shader translation (reading
the game's actual compiled vertex/pixel shader microcode) is deferred
to sub-project 3.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```

---

## Task 6: Extended live verification and manual visual confirmation request

**Files:**
- None (verification-only task; no code changes).

- [ ] **Step 1: Run an extended `--window` session and capture real draw-list statistics**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && ./build/BigBumpinHost --window > run.log 2>&1
echo "exit: $?"
grep -c "DRAW_INDX_2: primType=1" gpu_trace.log
grep -c "unsupported primType=8" gpu_trace.log
grep "failed to create\|failed to compile" run.log gpu_trace.log
rm -f run.log gpu_trace.log
```
Expected: exit code 2 (unchanged watchdog-bound baseline); the point-list draw count and the skipped-rectangle-list count both roughly match the 24-25 occurrences each already independently confirmed in sub-project 1's own final review (two distinct real draw-initiator values observed 24-25 times each in the real captured trace); zero `failed to create`/`failed to compile` lines.

- [ ] **Step 2: Request manual visual confirmation**

This sandboxed environment has no attached display or Accessibility access (established in Milestone 1) — screenshotting or otherwise visually confirming the rendered output is not possible here. End this task by asking the user to run `./build/BigBumpinHost --window` on their own machine and confirm: real geometry (whatever shape results from this sub-project's hardcoded vertex-format assumption) is visible against the cornflower-blue background, stays stable (no flicker/corruption) for the run's duration, and the process still exits cleanly at the watchdog boundary or on window close as established in Milestone 1.
