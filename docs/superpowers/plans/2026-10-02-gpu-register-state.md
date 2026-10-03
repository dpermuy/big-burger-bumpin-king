# GPU Register State Tracking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Track real PM4 `TYPE0` and `PM4_SET_CONSTANT` register writes into a real, queryable `GpuRegisterState` object, with decoded accessors for vertex fetch constants and the draw-initiator register — no rendering, no draw execution.

**Architecture:** A new, standalone `GpuRegisterState` class (`host/gpu_state.h/.cpp`) owns a flat register array with bounds-checked read/write and bit-exact decoded accessors layered on top. `GpuCommandTracer` (`host/gpu_trace.cpp`) gets one new member of that type and is extended to actually read the payload dwords its `TYPE0` branch currently only counts, plus a new `PM4_SET_CONSTANT` case, both writing through to the new object.

**Tech Stack:** C++17, this project's existing PM4-parsing conventions (`host/gpu_trace.cpp`'s `LoadU32`/`StoreU32` byte-swap helpers, its indentation/logging style).

**Spec:** `docs/superpowers/specs/2026-10-02-gpu-register-state-design.md`

## Global Constraints

- Register storage: flat `uint32_t regs_[kRegisterCount]`, `kRegisterCount = 0x5000`.
- Decoded register indices (real, confirmed against Xenia's source): `VGT_DRAW_INITIATOR = 0x21FC`; vertex/texture fetch constant bank base = `0x4800`, 2 dwords per slot, 96 slots (0-95).
- `PM4_SET_CONSTANT` real opcode = `0x2D`; sub-bank offset formula: low 11 bits of the first payload dword = index, bits 16-23 = type (0=ALU +0x4000, 1=FETCH +0x4800, 2=BOOL +0x4900, 3=LOOP +0x4908, 4=REGISTERS +0x2000).
- No change to any existing opcode's behavior (`EVENT_WRITE_SHD`, `INTERRUPT`, `INDIRECT_BUFFER`, `TYPE2`) beyond the new additions.
- No draw execution, no guest-memory reads at decoded addresses, no shader parsing, no `--window`/rendering-path changes — any of these would be scope creep into sub-projects 2/3.
- Headless regression baseline that must stay byte-for-byte unchanged throughout: 153 `NtReadFile` lines, tail message ending `"_xstart did not return within 10 seconds (watchdog timeout) -- this is an expected, informative outcome for Phase 2A, not a crash."`, exit code 2. Test command: `./build/BigBumpinHost > run.log 2>&1; echo "exit: $?"; grep -c NtReadFile run.log; tail -3 run.log`.

## Review Focus

- A `TYPE0` or `SET_CONSTANT` packet writing an index at or beyond `kRegisterCount` (0x5000) — a reasonable implementation does not crash or corrupt adjacent memory; Task 1's bounds-check test exercises this directly.
- `PM4_SET_CONSTANT`'s `type` sub-field holding an undefined value (5-255, not the 5 real defined cases) — Xenia itself asserts and skips; this project should skip the payload without writing garbage into an unrelated sub-bank. Covered in Task 3, Step 2b.
- `GetVertexFetchConstant` called with `slot >= 96` — must not read out of the array; Task 2's bounds-check test exercises this directly.
- A `TYPE0` packet whose declared `count` extends past the real 96-slot fetch-constant region into the next sub-bank — not a new case to handle specially (writes land wherever the flat array says, which is correct: hardware doesn't segment TYPE0 writes by "logical region" either), but worth one comment where `kRegisterCount`'s margin is chosen, not a new test.
- Two packets writing the same register index in the same buffer scan (a register set, then overwritten before being read back) — the array must reflect the last write, not accumulate or ignore the second; Task 1's "overwrite" test exercises this directly.

---

## Task 1: `GpuRegisterState` core storage (`host/gpu_state.h/.cpp`)

**Files:**
- Create: `host/gpu_state.h`
- Create: `host/gpu_state.cpp`
- Modify: `CMakeLists.txt:28` (add `host/gpu_state.cpp` to the `BigBumpinHost` source list)

**Interfaces:**
- Produces: `class GpuRegisterState` with `void WriteRegister(uint32_t index, uint32_t value)`, `uint32_t ReadRegister(uint32_t index) const`, `static constexpr uint32_t kRegisterCount = 0x5000`.

- [ ] **Step 1: Write a standalone smoke-test main that exercises the not-yet-existing class**

Create `host/gpu_state_smoketest.cpp` (temporary, deleted in Step 6):

```cpp
#include "gpu_state.h"
#include <cassert>
#include <cstdio>

int main()
{
    GpuRegisterState state;

    // Basic write/read round-trip.
    state.WriteRegister(0x2000, 0xAABBCCDD);
    assert(state.ReadRegister(0x2000) == 0xAABBCCDD);

    // Overwrite: last write wins.
    state.WriteRegister(0x2000, 0x11223344);
    assert(state.ReadRegister(0x2000) == 0x11223344);

    // Unwritten register reads as zero.
    assert(state.ReadRegister(0x3000) == 0);

    // Out-of-range write is ignored, not a crash.
    state.WriteRegister(GpuRegisterState::kRegisterCount, 0xFFFFFFFF);
    state.WriteRegister(GpuRegisterState::kRegisterCount + 1000, 0xFFFFFFFF);

    // Out-of-range read returns zero, not a crash or garbage.
    assert(state.ReadRegister(GpuRegisterState::kRegisterCount) == 0);

    printf("gpu_state_smoketest: PASS\n");
    return 0;
}
```

- [ ] **Step 2: Try to compile it to confirm it fails (the class doesn't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_state_smoketest.cpp -o /tmp/gpu_state_smoketest 2>&1`
Expected: FAIL — `'gpu_state.h' file not found` (or equivalent compiler error; `gpu_state.h` does not exist yet).

- [ ] **Step 3: Write `host/gpu_state.h`**

```cpp
#pragma once
#include <cstdint>

// Real Xenos GPU register state. Owns a flat register bank written to by
// GpuCommandTracer's PM4 TYPE0 and SET_CONSTANT packet handling
// (host/gpu_trace.cpp). No PM4-parsing knowledge lives here -- this is
// pure storage plus decoded bit-layout views on top of it, so later
// milestones add a new accessor here without ever touching storage.
class GpuRegisterState
{
public:
    // Covers every real sub-bank confirmed in Xenia's own source with
    // margin: REGISTERS (0x2000+), ALU (0x4000+), FETCH (0x4800+, 96
    // slots x 2 dwords = 192 dwords), BOOL (0x4900+), LOOP (0x4908+).
    static constexpr uint32_t kRegisterCount = 0x5000;

    // Out-of-range index: logged once (rate-limited) and ignored. A game
    // writing beyond this project's current margin is a real, informative
    // signal -- not a reason to corrupt memory or crash.
    void WriteRegister(uint32_t index, uint32_t value);

    // Out-of-range index: logged once (rate-limited) and returns 0.
    uint32_t ReadRegister(uint32_t index) const;

private:
    uint32_t regs_[kRegisterCount] = {};
    mutable bool loggedOutOfRange_ = false;
};
```

- [ ] **Step 4: Write `host/gpu_state.cpp`**

```cpp
#include "gpu_state.h"
#include <cstdio>

void GpuRegisterState::WriteRegister(uint32_t index, uint32_t value)
{
    if (index >= kRegisterCount)
    {
        if (!loggedOutOfRange_)
        {
            fprintf(stderr, "[gpu_state] out-of-range register write index=0x%X (kRegisterCount=0x%X) -- ignoring (further occurrences not logged)\n",
                index, kRegisterCount);
            loggedOutOfRange_ = true;
        }
        return;
    }
    regs_[index] = value;
}

uint32_t GpuRegisterState::ReadRegister(uint32_t index) const
{
    if (index >= kRegisterCount)
    {
        if (!loggedOutOfRange_)
        {
            fprintf(stderr, "[gpu_state] out-of-range register read index=0x%X (kRegisterCount=0x%X) -- returning 0 (further occurrences not logged)\n",
                index, kRegisterCount);
            loggedOutOfRange_ = true;
        }
        return 0;
    }
    return regs_[index];
}
```

- [ ] **Step 5: Compile and run the smoke test to verify it passes**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_state_smoketest.cpp host/gpu_state.cpp -o /tmp/gpu_state_smoketest && /tmp/gpu_state_smoketest`
Expected: Two `[gpu_state] out-of-range register write ...` lines (first out-of-range write only, matching rate-limiting), then `gpu_state_smoketest: PASS`, exit code 0.

- [ ] **Step 6: Delete the temporary smoke-test file, add `gpu_state.cpp` to the build, commit**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
rm host/gpu_state_smoketest.cpp
```

Edit `CMakeLists.txt:28` — change:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/game_overrides.cpp host/renderer_metal.mm)
```
to:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/game_overrides.cpp host/renderer_metal.mm)
```

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/gpu_state.h host/gpu_state.cpp CMakeLists.txt
git commit -m "feat: add GpuRegisterState core storage (unused by GpuCommandTracer yet)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153` from the `grep -c`, tail line ending `"_xstart did not return within 10 seconds (watchdog timeout) -- this is an expected, informative outcome for Phase 2A, not a crash."` — byte-for-byte the established baseline, since `GpuRegisterState` is compiled in but not yet referenced by any existing code path.

---

## Task 2: Decoded accessors (`GetVertexFetchConstant`, `GetDrawInitiator`)

**Files:**
- Modify: `host/gpu_state.h` (add structs + method declarations)
- Modify: `host/gpu_state.cpp` (add method implementations)

**Interfaces:**
- Consumes: `GpuRegisterState::ReadRegister` (Task 1).
- Produces: `struct VertexFetchConstant { uint32_t type; uint32_t address; uint32_t endian; uint32_t size; };`, `VertexFetchConstant GpuRegisterState::GetVertexFetchConstant(uint32_t slot) const`; `struct DrawInitiator { uint32_t primType; uint32_t sourceSelect; uint32_t indexSize; uint32_t numIndices; };`, `DrawInitiator GpuRegisterState::GetDrawInitiator() const`.

- [ ] **Step 1: Write a failing smoke test for both decoded accessors**

Create `host/gpu_state_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "gpu_state.h"
#include <cassert>
#include <cstdio>

int main()
{
    GpuRegisterState state;

    // --- VGT_DRAW_INITIATOR (real register 0x21FC) ---
    // primType=4 (kTriangleList, bits 0-5), sourceSelect=2 (kAutoIndex, bits 6-7),
    // indexSize=0 (bit 11), numIndices=300 (bits 16-31).
    uint32_t drawInitiatorValue = (4) | (2 << 6) | (0 << 11) | (300u << 16);
    state.WriteRegister(0x21FC, drawInitiatorValue);
    DrawInitiator di = state.GetDrawInitiator();
    assert(di.primType == 4);
    assert(di.sourceSelect == 2);
    assert(di.indexSize == 0);
    assert(di.numIndices == 300);

    // --- Vertex fetch constant slot 0 (real base 0x4800, 2 dwords) ---
    // dword_0: type=3 (kVertex, bits 0-1), address=0x12345 (bits 2-31, in dwords)
    // dword_1: endian=1 (bits 0-1), size=0x1000 (bits 2-25, in words)
    uint32_t dword0 = (3) | (0x12345u << 2);
    uint32_t dword1 = (1) | (0x1000u << 2);
    state.WriteRegister(0x4800, dword0);
    state.WriteRegister(0x4801, dword1);
    VertexFetchConstant vfc = state.GetVertexFetchConstant(0);
    assert(vfc.type == 3);
    assert(vfc.address == 0x12345);
    assert(vfc.endian == 1);
    assert(vfc.size == 0x1000);

    // Slot 1 is untouched -- reads as all zero.
    VertexFetchConstant vfc1 = state.GetVertexFetchConstant(1);
    assert(vfc1.type == 0 && vfc1.address == 0 && vfc1.endian == 0 && vfc1.size == 0);

    // Out-of-range slot: returns a zeroed struct, not a crash.
    VertexFetchConstant vfcOOR = state.GetVertexFetchConstant(96);
    assert(vfcOOR.type == 0 && vfcOOR.address == 0 && vfcOOR.endian == 0 && vfcOOR.size == 0);

    printf("gpu_state_smoketest: PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (methods/structs don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_state_smoketest.cpp host/gpu_state.cpp -o /tmp/gpu_state_smoketest 2>&1`
Expected: FAIL — compiler error, `'VertexFetchConstant' was not declared` (or equivalent; `GetDrawInitiator`/`GetVertexFetchConstant` don't exist yet).

- [ ] **Step 3: Add the decoded structs and accessors**

In `host/gpu_state.h`, add above `class GpuRegisterState`:

```cpp
// Bit-exact match to Xenia's reg::VGT_DRAW_INITIATOR (real register 0x21FC).
struct DrawInitiator
{
    uint32_t primType;      // bits 0-5
    uint32_t sourceSelect;  // bits 6-7: 0=kDMA (indexed), 1=kImmediate (unsupported), 2=kAutoIndex
    uint32_t indexSize;     // bit 11: 0=16-bit, 1=32-bit
    uint32_t numIndices;    // bits 16-31
};

// Bit-exact match to Xenia's xe_gpu_vertex_fetch_t. Shared address space
// with texture fetch constants (type distinguishes which); vertex fetch
// uses type == 3.
struct VertexFetchConstant
{
    uint32_t type;      // dword_0 bits 0-1
    uint32_t address;   // dword_0 bits 2-31, in dwords (byte address = address << 2)
    uint32_t endian;    // dword_1 bits 0-1
    uint32_t size;      // dword_1 bits 2-25, in 32-bit words
};
```

Inside `class GpuRegisterState`, add to the `public:` section (after `ReadRegister`):

```cpp
    // Real register index for VGT_DRAW_INITIATOR (confirmed against
    // Xenia's register_table.inc) -- falls inside the already-observed
    // real TYPE0 range 0x2000-0x2312 (Finding 35, prior investigation).
    static constexpr uint32_t kDrawInitiatorRegister = 0x21FC;
    DrawInitiator GetDrawInitiator() const;

    // Real base register for vertex/texture fetch constants (confirmed:
    // XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 = 0x4800). 96 slots, 2 dwords
    // each. slot >= 96 returns a zeroed struct.
    static constexpr uint32_t kVertexFetchConstantBase = 0x4800;
    static constexpr uint32_t kVertexFetchConstantSlotCount = 96;
    VertexFetchConstant GetVertexFetchConstant(uint32_t slot) const;
```

- [ ] **Step 4: Implement both accessors in `host/gpu_state.cpp`**

Add to the end of the file:

```cpp
DrawInitiator GpuRegisterState::GetDrawInitiator() const
{
    uint32_t value = ReadRegister(kDrawInitiatorRegister);
    DrawInitiator result;
    result.primType = value & 0x3F;
    result.sourceSelect = (value >> 6) & 0x3;
    result.indexSize = (value >> 11) & 0x1;
    result.numIndices = (value >> 16) & 0xFFFF;
    return result;
}

VertexFetchConstant GpuRegisterState::GetVertexFetchConstant(uint32_t slot) const
{
    if (slot >= kVertexFetchConstantSlotCount)
    {
        return VertexFetchConstant{0, 0, 0, 0};
    }
    uint32_t baseIndex = kVertexFetchConstantBase + slot * 2;
    uint32_t dword0 = ReadRegister(baseIndex);
    uint32_t dword1 = ReadRegister(baseIndex + 1);
    VertexFetchConstant result;
    result.type = dword0 & 0x3;
    result.address = dword0 >> 2;
    result.endian = dword1 & 0x3;
    result.size = (dword1 >> 2) & 0xFFFFFF;
    return result;
}
```

- [ ] **Step 5: Compile and run to verify the smoke test passes**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/gpu_state_smoketest.cpp host/gpu_state.cpp -o /tmp/gpu_state_smoketest && /tmp/gpu_state_smoketest`
Expected: `gpu_state_smoketest: PASS`, exit code 0, no `[gpu_state]` warning lines (every index used is in range).

- [ ] **Step 6: Delete the temporary smoke test, rebuild the real project, verify regression, commit**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
rm host/gpu_state_smoketest.cpp
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/gpu_state.h host/gpu_state.cpp
git commit -m "feat: add GpuRegisterState decoded accessors for VGT_DRAW_INITIATOR and vertex fetch constants

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message as Task 1's baseline — unchanged, since these accessors are still unused by any real code path.

---

## Task 3: Wire real PM4 writes into `GpuRegisterState` (`host/gpu_trace.h/.cpp`)

**Files:**
- Modify: `host/gpu_trace.h` (add `#include "gpu_state.h"`, add `gpuState_` member, add `RegisterState()` accessor)
- Modify: `host/gpu_trace.cpp` (extend the `TYPE0` branch, add the `PM4_SET_CONSTANT` case, add temporary verification logging)

**Interfaces:**
- Consumes: `GpuRegisterState::WriteRegister`, `GetDrawInitiator`, `GetVertexFetchConstant` (Tasks 1-2); `LoadU32` (existing file-local helper, `host/gpu_trace.cpp:9`).
- Produces: `const GpuRegisterState& GpuCommandTracer::RegisterState() const` (for sub-project 2 to consume later).

- [ ] **Step 1: Add the member and accessor to `host/gpu_trace.h`**

Add near the top, after `struct PPCContext;`:
```cpp
#include "gpu_state.h"
```

Add to the `public:` section (after `uint32_t GraphicsInterruptContext();`):
```cpp
    const GpuRegisterState& RegisterState() const { return gpuState_; }
```

Add to the `private:` section (after `uint32_t ScanBuffer(...)` declaration, before `uint32_t ringBufferBase_ = 0;`):
```cpp
    GpuRegisterState gpuState_;
```

- [ ] **Step 2: Extend the `TYPE0` branch to read and store real payload dwords**

In `host/gpu_trace.cpp`, inside `ScanBuffer`, replace the `type == 0x0` block (currently lines 184-197):

```cpp
        if (type == 0x0)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t baseIndex = header & 0x7FFF;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > sizeBytes)
            {
                break; // count runs past the buffer -- not a real packet, stop here
            }
            if (logFile_) fprintf(logFile_, "%sTYPE0 reg=0x%04X count=%u\n", indent, baseIndex, count);
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }
```

with:

```cpp
        if (type == 0x0)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t baseIndex = header & 0x7FFF;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > sizeBytes)
            {
                break; // count runs past the buffer -- not a real packet, stop here
            }
            if (logFile_) fprintf(logFile_, "%sTYPE0 reg=0x%04X count=%u\n", indent, baseIndex, count);
            for (uint32_t i = 0; i < count; i++)
            {
                uint32_t value = LoadU32(base, bufferAddr + offsetBytes + 4 + i * 4);
                gpuState_.WriteRegister(baseIndex + i, value);
            }
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }
```

- [ ] **Step 2b: Note on the undefined-`SET_CONSTANT`-type Review Focus item**

The `switch` in Step 3 below handles an undefined `subType` (5-255) by setting
`validType = false` and skipping the payload without writing anything —
matches the spec's required behavior. This path has no live test: real
Direct3D 9 on Xbox 360 only ever emits the 5 defined sub-types, so no real
game traffic exercises it, the same situation Milestone 1 hit for its
no-GPU-device error path (verified by code inspection only, ledgered rather
than blocked on). Verify by reading the `switch` in Step 3 once written:
confirm the `default` case sets `validType = false` and that no
`WriteRegister` call happens when `validType` is false.

- [ ] **Step 3: Add the `PM4_SET_CONSTANT` TYPE3 case**

In `host/gpu_trace.cpp`, in the anonymous namespace near the top (alongside `kOpcodeInterrupt`/`kOpcodeEventWriteShd`), add:

```cpp
    constexpr uint32_t kOpcodeSetConstant = 0x2D;  // PM4_SET_CONSTANT
```

Inside `ScanBuffer`'s `type == 0x3` block, after the existing `if (opcode == kOpcodeInterrupt && count == 1) { ... }` block and before `offsetBytes += 4 + payloadBytes;`, add:

```cpp
            if (opcode == kOpcodeSetConstant && count >= 1)
            {
                // Real semantics (Xenia's ExecutePacketType3_SET_CONSTANT):
                // first payload dword's low 11 bits are the sub-bank index,
                // bits 16-23 select which real register sub-bank it's
                // relative to. An undefined type (anything but the 5 real
                // cases) is skipped entirely rather than guessed at --
                // matches this project's established posture toward
                // unparsed data.
                uint32_t offsetType = LoadU32(base, bufferAddr + offsetBytes + 4);
                uint32_t subIndex = offsetType & 0x7FF;
                uint32_t subType = (offsetType >> 16) & 0xFF;
                uint32_t baseRegister = 0;
                bool validType = true;
                switch (subType)
                {
                    case 0: baseRegister = subIndex + 0x4000; break; // ALU
                    case 1: baseRegister = subIndex + 0x4800; break; // FETCH
                    case 2: baseRegister = subIndex + 0x4900; break; // BOOL
                    case 3: baseRegister = subIndex + 0x4908; break; // LOOP
                    case 4: baseRegister = subIndex + 0x2000; break; // REGISTERS
                    default: validType = false; break;
                }
                if (validType)
                {
                    for (uint32_t i = 0; i < count - 1; i++)
                    {
                        uint32_t value = LoadU32(base, bufferAddr + offsetBytes + 8 + i * 4);
                        gpuState_.WriteRegister(baseRegister + i, value);
                    }
                    if (logFile_)
                    {
                        fprintf(logFile_, "%s-> SET_CONSTANT: subType=%u subIndex=0x%X -> base=0x%X, %u dwords\n",
                            indent, subType, subIndex, baseRegister, count - 1);
                    }
                }
                else if (logFile_)
                {
                    fprintf(logFile_, "%s-> SET_CONSTANT: undefined subType=%u, skipped\n", indent, subType);
                }
            }

```

Also update the `name` string just above (the `const char* name = ...` line) to include the new opcode for log readability:
```cpp
            const char* name = (opcode == kOpcodeMeInit) ? " (ME_INIT)"
                : (opcode == kOpcodeIndirectBuffer) ? " (INDIRECT_BUFFER)"
                : (opcode == kOpcodeSetConstant) ? " (SET_CONSTANT)" : "";
```

- [ ] **Step 4: Add temporary live-verification logging**

Immediately after the `for` loop added in Step 2 (still inside the `type == 0x0` block, before `offsetBytes += 4 + payloadBytes;`), add:

```cpp
            // TEMP (removed in Step 6 once live-verified): confirm the
            // decoded accessors produce plausible real values.
            if (baseIndex <= GpuRegisterState::kDrawInitiatorRegister && GpuRegisterState::kDrawInitiatorRegister < baseIndex + count)
            {
                DrawInitiator di = gpuState_.GetDrawInitiator();
                fprintf(stderr, "[gpu_state TEMP] DrawInitiator: primType=%u sourceSelect=%u indexSize=%u numIndices=%u\n",
                    di.primType, di.sourceSelect, di.indexSize, di.numIndices);
            }
            if (baseIndex <= GpuRegisterState::kVertexFetchConstantBase + 1 && GpuRegisterState::kVertexFetchConstantBase + 1 < baseIndex + count)
            {
                VertexFetchConstant vfc = gpuState_.GetVertexFetchConstant(0);
                fprintf(stderr, "[gpu_state TEMP] VertexFetchConstant[0]: type=%u address=0x%X (byte addr 0x%X) endian=%u size=%u\n",
                    vfc.type, vfc.address, (vfc.address << 2) | 0xA0000000u, vfc.endian, vfc.size);
            }
```

- [ ] **Step 5: Build and run live, verify the regression baseline and the new decoded values**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
grep "gpu_state TEMP" run.log | head -20
```
Expected: clean build; exit code 2; `153` from `grep -c`; same tail message as Tasks 1-2 (headless path doesn't register PM4 buffers at all in this project's current flow, so this is expected to be unchanged — confirm it is). If any `[gpu_state TEMP]` lines appear (depends on whether the headless path ever calls `ScanBuffer` with real content before the watchdog fires), inspect them: `primType` must be 0-21, `sourceSelect` one of 0/1/2, and if a `VertexFetchConstant` line appears, its byte address must look like a real guest address (starts with `0xA0` matching the segment convention, or another address range already known valid from prior findings). If zero `[gpu_state TEMP]` lines appear because the headless run's watchdog fires before any GPU buffer content is scanned, that is not a failure of this task — proceed to Step 5b.

- [ ] **Step 5b: If Step 5 produced no TEMP output, run the `--window` path instead to get real PM4 traffic**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && ./build/BigBumpinHost --window > run.log 2>&1
echo "exit: $?"
grep "gpu_state TEMP" run.log | head -20
rm -f run.log gpu_trace.log
```
Expected: exit code 2 (unchanged from the established `--window` baseline); at least one `[gpu_state TEMP]` line for `DrawInitiator` and/or `VertexFetchConstant[0]`, with plausible values as described in Step 5 — this is the sub-project's real, live confirmation that decoded register values are genuinely correct, not just compiled correctly.

- [ ] **Step 6: Remove the TEMP logging (Step 4), keep the production code, final regression check, commit**

Remove the two `if` blocks added in Step 4 from `host/gpu_trace.cpp` (the `[gpu_state TEMP]` logging) — the production `WriteRegister` calls from Steps 2-3 stay.

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/gpu_trace.h host/gpu_trace.cpp
git commit -m "feat: wire real TYPE0 and PM4_SET_CONSTANT register writes into GpuRegisterState

Live-verified (--window run): decoded VGT_DRAW_INITIATOR and vertex fetch
constant slot 0 both produced plausible real values before this
instrumentation was removed.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — byte-for-byte unchanged from every prior baseline in this plan and in Milestone 1.
