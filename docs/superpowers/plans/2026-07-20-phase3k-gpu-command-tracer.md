# Phase 3K: Real GPU Command-Buffer Plumbing + PM4 Tracer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the game's real Xenos GPU ring buffer actually receive command data (it
currently has nowhere real to write to), and produce a human-readable trace of the real PM4
packets it contains, without attempting any rendering.

**Architecture:** A new `GpuCommandTracer` class (`host/gpu_trace.h`/`.cpp`, following the
`XdvdfsImage` module pattern) owns all ring-buffer bookkeeping and PM4 parsing. Four kernel
imports (`VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`,
`VdSetSystemCommandBufferGpuIdentifierAddress`, `VdGetSystemCommandBuffer`) become real,
feeding the tracer. `VdSwap` (already real, currently a no-op) gets one added line calling
into the tracer at each frame boundary.

**Tech Stack:** C++17, existing `PPC_LOAD_U32`/`PPC_STORE_U32` guest-memory macros, `FILE*`
for the trace log (matches project's existing `fmt::println` / raw-file conventions).

## Global Constraints

- Every guest-memory access goes through `PPC_LOAD_U32`/`PPC_STORE_U32` (never raw
  pointer arithmetic on `base` without them) — same rule as every prior phase.
- Standalone (non-`PPC_FUNC`) functions that use these macros must take `base` as an
  explicit parameter, named exactly `base` (macro expects that identifier in scope) —
  established in Phase 3A after a real compile failure.
- No diagnostic instrumentation left in a committed state — any temporary `fprintf` added
  while verifying must be reverted before the final commit, verified via `git diff --stat`.
- Real behavior only where there's real evidence (call-site register usage, documented
  stable PM4 header format); anything uncertain (ring size formula, RPTR units) gets a
  comment flagging it as a starting point, not asserted-correct.

---

### Task 1: GpuCommandTracer module — ring buffer registration

**Files:**
- Create: `host/gpu_trace.h`
- Create: `host/gpu_trace.cpp`
- Modify: `CMakeLists.txt:26` (add `host/gpu_trace.cpp` to `add_executable(BigBumpinHost ...)`)

**Interfaces:**
- Produces: `class GpuCommandTracer` with `RegisterRingBuffer(uint32_t physAddr, uint32_t
  sizeLog2Raw)`, `SetRptrWriteBackAddr(uint32_t addr)`, `SetIdentifierAddr(uint32_t addr)`,
  `ScanAndTraceFrame(uint8_t* base)`, `bool HasRingBuffer() const`. Global instance
  `extern GpuCommandTracer g_gpuTracer;`

- [ ] **Step 1: Write `host/gpu_trace.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstdio>

class GpuCommandTracer
{
public:
    void RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw);
    void SetRptrWriteBackAddr(uint32_t addr);
    void SetIdentifierAddr(uint32_t addr);
    void ScanAndTraceFrame(uint8_t* base);
    bool HasRingBuffer() const { return ringBufferBase_ != 0; }

private:
    void EnsureLogOpen();

    uint32_t ringBufferBase_ = 0;
    uint32_t ringBufferSize_ = 0;
    uint32_t rptrWriteBackAddr_ = 0;
    uint32_t identifierAddr_ = 0;
    uint32_t lastParsedOffset_ = 0;
    uint32_t frameCounter_ = 0;
    FILE* logFile_ = nullptr;
};

extern GpuCommandTracer g_gpuTracer;
```

- [ ] **Step 2: Write `host/gpu_trace.cpp` (registration + log setup only, parsing in Task 2)**

```cpp
#include "gpu_trace.h"

GpuCommandTracer g_gpuTracer;

void GpuCommandTracer::EnsureLogOpen()
{
    if (!logFile_)
    {
        logFile_ = fopen("gpu_trace.log", "w");
    }
}

void GpuCommandTracer::RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw)
{
    ringBufferBase_ = physAddr;
    // Inverts the call site's own computation (private/ppc/ppc_recomp.4.cpp:14842-14856):
    // the game derives sizeLog2Raw as (31 - clz(originalSizeBytes)) - 3, so
    // originalSizeBytes is approximately 8 << (sizeLog2Raw + 3). Not independently
    // verified against real hardware docs -- a starting point, correctable once trace
    // output shows packets running past this boundary.
    ringBufferSize_ = 8u << (sizeLog2Raw + 3);
    lastParsedOffset_ = 0;
    frameCounter_ = 0;

    EnsureLogOpen();
    if (logFile_)
    {
        fprintf(logFile_, "[init] ring buffer base=0x%08X sizeLog2Raw=%u decodedSize=%u bytes\n",
            physAddr, sizeLog2Raw, ringBufferSize_);
        fflush(logFile_);
    }
}

void GpuCommandTracer::SetRptrWriteBackAddr(uint32_t addr)
{
    rptrWriteBackAddr_ = addr;
}

void GpuCommandTracer::SetIdentifierAddr(uint32_t addr)
{
    identifierAddr_ = addr;
}
```

- [ ] **Step 3: Add `host/gpu_trace.cpp` to CMakeLists.txt**

In `CMakeLists.txt`, change:
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp)
```
to:
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp)
```

- [ ] **Step 4: Configure and build to verify it compiles (ScanAndTraceFrame not yet called anywhere)**

Run: `cmake --build build --target BigBumpinHost -j8`
Expected: clean build (CMake re-globs automatically; if `gpu_trace.cpp` isn't picked up,
re-run `cmake -S . -B build` first).

- [ ] **Step 5: Commit**

```bash
git add host/gpu_trace.h host/gpu_trace.cpp CMakeLists.txt
git commit -m "Add GpuCommandTracer module: ring buffer registration + trace log setup"
```

---

### Task 2: PM4 packet parsing

**Files:**
- Modify: `host/gpu_trace.cpp`

**Interfaces:**
- Consumes: nothing new from other tasks.
- Produces: `ScanAndTraceFrame(uint8_t* base)` fully implemented — later tasks (Task 3) call
  this from `VdSwap`.

- [ ] **Step 1: Implement `ScanAndTraceFrame` in `host/gpu_trace.cpp`**

Add near the top of the file (after the `#include`):
```cpp
namespace
{
    inline uint32_t LoadU32(uint8_t* base, uint32_t addr)
    {
        return __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(base + addr));
    }

    inline void StoreU32(uint8_t* base, uint32_t addr, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t*>(base + addr) = __builtin_bswap32(value);
    }
}
```

Append to the end of the file:
```cpp
void GpuCommandTracer::ScanAndTraceFrame(uint8_t* base)
{
    EnsureLogOpen();
    frameCounter_++;

    uint32_t offsetBytes = lastParsedOffset_;
    uint32_t packetsParsed = 0;

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u (starting offset %u) ---\n", frameCounter_, offsetBytes);
    }

    while (offsetBytes + 4 <= ringBufferSize_)
    {
        uint32_t header = LoadU32(base, ringBufferBase_ + offsetBytes);
        uint32_t type = (header >> 30) & 0x3;

        if (type == 0x2)
        {
            // Type 2: single-dword filler/no-op, no payload.
            if (logFile_) fprintf(logFile_, "TYPE2 (filler)\n");
            offsetBytes += 4;
            packetsParsed++;
            continue;
        }

        if (type == 0x0)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t baseIndex = header & 0x7FFF;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > ringBufferSize_)
            {
                break; // count runs past the buffer -- not a real packet, stop here
            }
            if (logFile_) fprintf(logFile_, "TYPE0 reg=0x%04X count=%u\n", baseIndex, count);
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }

        if (type == 0x3)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t opcode = (header >> 8) & 0x7F;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > ringBufferSize_)
            {
                break;
            }
            if (logFile_) fprintf(logFile_, "TYPE3 opcode=0x%02X count=%u\n", opcode, count);
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }

        // Type 1 (legacy two-register write, not expected on Xenos) or anything else
        // that didn't decode above: stop and dump raw bytes for manual inspection
        // rather than guessing further, matching the project's established posture
        // toward unparsed data (see xdvdfs.cpp's BST padding handling).
        break;
    }

    if (offsetBytes < ringBufferSize_ && offsetBytes == lastParsedOffset_ && packetsParsed == 0)
    {
        // Nothing new parsed at all -- dump a small raw window so the header format
        // can be checked by hand instead of silently producing an empty trace.
        if (logFile_)
        {
            fprintf(logFile_, "RAW (unparsed) at offset %u:", offsetBytes);
            uint32_t dumpBytes = (ringBufferSize_ - offsetBytes < 64) ? (ringBufferSize_ - offsetBytes) : 64;
            for (uint32_t i = 0; i < dumpBytes; i += 4)
            {
                fprintf(logFile_, " %08X", LoadU32(base, ringBufferBase_ + offsetBytes + i));
            }
            fprintf(logFile_, "\n");
        }
    }

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u: parsed %u packets, offset %u -> %u ---\n",
            frameCounter_, packetsParsed, lastParsedOffset_, offsetBytes);
        fflush(logFile_);
    }

    lastParsedOffset_ = offsetBytes;

    // Real semantics: the GPU writes its "consumed up to here" read pointer so the CPU
    // knows how much ring space is free. There's no real GPU, so report "caught up to
    // everything we just parsed" every frame -- this is what keeps the game from ever
    // blocking waiting for a completion signal that would otherwise never come (the
    // exact shape of bug Phase 3H fixed for APC delivery). Units are dwords, matching
    // real CP_RB_RPTR/CP_RB_WPTR register convention -- not independently verified,
    // flagged here as a starting point.
    if (rptrWriteBackAddr_ != 0)
    {
        StoreU32(base, rptrWriteBackAddr_, offsetBytes / 4);
    }
}
```

- [ ] **Step 2: Build to verify it compiles**

Run: `cmake --build build --target BigBumpinHost -j8`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add host/gpu_trace.cpp
git commit -m "Implement PM4 packet header parsing in GpuCommandTracer"
```

---

### Task 3: Wire the four kernel imports to the tracer

**Files:**
- Modify: `host/kernel_impl.cpp`
- Modify: `host/kernel_imports.txt`
- Modify: `host/kernel_stubs.cpp` (regenerated, not hand-edited)

**Interfaces:**
- Consumes: `GpuCommandTracer` from Task 1/2 (`#include "gpu_trace.h"`).
- Produces: real `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`,
  `VdSetSystemCommandBufferGpuIdentifierAddress`, `VdGetSystemCommandBuffer` in
  `kernel_impl.cpp`; `VdSwap` (already real) gains one call into the tracer.

- [ ] **Step 1: Add the include**

In `host/kernel_impl.cpp`, near the top with the other project includes:
```cpp
#include "xdvdfs.h"
```
add directly after it:
```cpp
#include "gpu_trace.h"
```

- [ ] **Step 2: Replace the four stub bodies with real implementations**

Find the existing `PPC_FUNC(__imp__VdInitializeRingBuffer)` /
`__imp__VdEnableRingBufferRPtrWriteBack` /
`__imp__VdSetSystemCommandBufferGpuIdentifierAddress` bodies (currently no-ops, around
`host/kernel_impl.cpp:1125-1145` per the Phase 2J implementation) and replace with:

```cpp
PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    uint32_t physAddr = (uint32_t)ctx.r3.u64;
    uint32_t sizeLog2Raw = (uint32_t)ctx.r4.u64;
    g_gpuTracer.RegisterRingBuffer(physAddr, sizeLog2Raw);
}

PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    uint32_t addr = (uint32_t)ctx.r3.u64;
    g_gpuTracer.SetRptrWriteBackAddr(addr);
}
```

Find `PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)` (currently a no-op
around `host/kernel_impl.cpp:1133`) and replace with:

```cpp
PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    uint32_t addr = (uint32_t)ctx.r3.u64;
    if (addr != 0)
    {
        g_gpuTracer.SetIdentifierAddr(addr);
    }
}
```

- [ ] **Step 3: Implement real `VdGetSystemCommandBuffer`**

`VdGetSystemCommandBuffer` is currently generated as a stub in `host/kernel_stubs.cpp`
(it's still in `host/kernel_imports.txt`). Add this near the other `Vd*` implementations in
`host/kernel_impl.cpp`:

```cpp
static uint32_t g_systemCommandBufferAddr = 0;
static constexpr uint32_t kSystemCommandBufferSize = 65536;

// Real signature confirmed live (private/ppc/ppc_recomp.6.cpp:24604-24625): writes an
// address and a size back through two caller-stack out-pointers (r3, r4), and also
// returns the address directly in r3 -- the caller saves that return value separately
// right after the call. A second, smaller, kernel-owned buffer distinct from the game's
// main ring buffer; given a real, non-crashing region here rather than treated as a
// primary command stream (see Phase 3K design spec).
PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    uint32_t addrOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t sizeOutPtr = (uint32_t)ctx.r4.u64;

    if (g_systemCommandBufferAddr == 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_systemCommandBufferAddr == 0)
        {
            g_systemCommandBufferAddr = g_bumpAllocatorNext;
            g_bumpAllocatorNext += kSystemCommandBufferSize;
        }
    }

    if (addrOutPtr != 0)
    {
        PPC_STORE_U32(addrOutPtr, g_systemCommandBufferAddr);
    }
    if (sizeOutPtr != 0)
    {
        PPC_STORE_U32(sizeOutPtr, kSystemCommandBufferSize);
    }

    ctx.r3.u64 = g_systemCommandBufferAddr;
}
```

- [ ] **Step 4: Hook `VdSwap`**

Find `PPC_FUNC(__imp__VdSwap)` (currently a no-op, around `host/kernel_impl.cpp:1186`) and
replace its body with:

```cpp
PPC_FUNC(__imp__VdSwap)
{
    // The frame-present call. Still no real rendering -- nothing to actually present,
    // no renderer exists yet (Phase 3K only adds command-buffer tracing). Real frame
    // presentation is future work once the renderer design (informed by this phase's
    // trace output) exists.
    if (g_gpuTracer.HasRingBuffer())
    {
        g_gpuTracer.ScanAndTraceFrame(base);
    }
}
```

- [ ] **Step 5: Remove `VdGetSystemCommandBuffer` from the stub-generation list and
      regenerate**

In `host/kernel_imports.txt`, delete the line `VdGetSystemCommandBuffer`.

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote <N-1> stub functions to host/kernel_stubs.cpp` (one fewer than before).

(`VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`,
`VdSetSystemCommandBufferGpuIdentifierAddress` were already real, non-stub functions from
Phase 2J — only `VdGetSystemCommandBuffer` needs removing from the generator's input.)

- [ ] **Step 6: Build**

Run: `cmake --build build --target BigBumpinHost -j8`
Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add host/kernel_impl.cpp host/kernel_imports.txt host/kernel_stubs.cpp
git commit -m "Wire ring buffer, RPTR writeback, GPU identifier, and system command buffer to real implementations"
```

---

### Task 4: Verification run

**Files:** none (verification only).

- [ ] **Step 1: Run the host against the real ISO**

Run: `cd build && ./BigBumpinHost ../private/default.xex "../Big Bumpin' (USA).iso"`
Expected: same steady running behavior as the Phase 3J baseline (no crash, no new stall);
`gpu_trace.log` created in the `build/` working directory.

- [ ] **Step 2: Inspect the trace log**

Run: `head -50 build/gpu_trace.log` and `grep -c "TYPE3" build/gpu_trace.log`
Expected: at least one `[init] ring buffer base=...` line with a plausible decoded size, and
either real `TYPE0`/`TYPE2`/`TYPE3` packet lines or a `RAW (unparsed)` hex dump if the header
format assumptions need correcting. Both outcomes are valuable and should be reported
honestly (matching Phase 2J's success-criteria precedent) — this is a first-look evidence
pass, not a pass/fail gate on getting the format perfectly right on the first try.

- [ ] **Step 3: Confirm no regression via extended-watchdog + lldb sampling**

Temporarily bump `host/main.cpp`'s `future.wait_for(std::chrono::seconds(10))` to `45`,
rebuild, run, sample the main thread's backtrace via lldb twice a few seconds apart (same
technique as Phase 3I), confirm the call stack still genuinely evolves (not frozen). Revert
the watchdog change immediately after (`git checkout -- host/main.cpp`), verify
`git status --short` is clean, rebuild once more to confirm the revert built cleanly.

- [ ] **Step 4: Record findings**

Write a short verification note (matching the project's existing "Record Phase N
verification run" commit precedent) summarizing: ring buffer size observed, packet types/
opcodes observed (or lack thereof), and whether the header-parsing assumptions held up.
Commit any log excerpts worth keeping under `docs/superpowers/specs/` if there's a genuinely
informative capture (matching `phase3a-stub-hits.txt` precedent) — do not commit the full
`gpu_trace.log` itself (large, regenerable, working-directory output, not source).

```bash
git add docs/superpowers/specs/  # only if a findings file was actually added
git commit -m "Record Phase 3K verification run: real PM4 trace output"
```

---

## Execution Handoff

This plan is small and low-risk (four well-understood kernel functions plus a new, isolated
tracer module) — inline execution in the current session, matching Phase 3A/3B/3H-3J's
established rhythm, rather than a full subagent-driven dispatch.
