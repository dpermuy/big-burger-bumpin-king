# Shader Microcode Decode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse real `PM4_IM_LOAD_IMMEDIATE` shader-load packets and decode the embedded Xenos microcode (control-flow + ALU + vertex-fetch instructions) into a real, inspectable disassembly — no MSL generation, no rendering change.

**Architecture:** A new, PM4/Metal-free `host/shader_decode.h/.cpp` holds the real bit-layout structs and a single `DecodeShaderMicrocode` entry point. `host/gpu_trace.cpp`'s existing TYPE3 dispatch gets one new case that reads the real packet, byte-swaps the embedded microcode the same way every other PM4 dword in this file already is, calls the decoder, and logs its output.

**Tech Stack:** C++17, this project's existing PM4-parsing/byte-swap conventions.

**Spec:** `docs/superpowers/specs/2026-10-03-shader-microcode-decode-design.md`

## Global Constraints

- Real control-flow instruction packing: 3 raw dwords (`d0,d1,d2`) encode 2 logical 48-bit instructions — instruction A = `{word0: d0, word1: d1 & 0xFFFF}`, instruction B = `{word0: (d1 >> 16) | (d2 << 16), word1: d2 >> 16}` (confirmed exact against Xenia's `UnpackControlFlowInstructions`).
- Real `ControlFlowInstruction` field positions (within the unpacked `word0`/`word1` pair): `address` = `word0` bits 0-11 (12 bits), `count` = `word0` bits 12-14 (3 bits), `sequence` = `word0` bits 16-27 (12 bits), `opcode` = `word1` bits 12-15 (4 bits) — `word1` is only ever a 16-bit value, so this is NOT bits 28-31 of a 32-bit word.
- Real 16 `ControlFlowOpcode` values: `kNop=0, kExec=1, kExecEnd=2, kCondExec=3, kCondExecEnd=4, kCondExecPred=5, kCondExecPredEnd=6, kLoopStart=7, kLoopEnd=8, kCondCall=9, kReturn=10, kCondJmp=11, kAlloc=12, kCondExecPredClean=13, kCondExecPredCleanEnd=14, kMarkVsFetchDone=15`.
- Real EXEC-family opcodes (the 8 values whose `address`/`count` reference ALU/fetch instructions): `kExec, kExecEnd, kCondExec, kCondExecEnd, kCondExecPred, kCondExecPredEnd, kCondExecPredClean, kCondExecPredCleanEnd`.
- `sequence` bit `2*j` (for the j-th instruction in an EXEC block, `j` from 0) selects ALU (0) or fetch (1) — confirmed against Xenia's own `ControlFlowExecInstruction::sequence()` doc comment.
- Real `AluInstruction` (3 raw dwords): word0 `vector_dest:6(0-5) vector_dest_rel:1(6) abs_constants:1(7) scalar_dest:6(8-13) scalar_dest_rel:1(14) export_data:1(15) vector_write_mask:4(16-19) scalar_write_mask:4(20-23) vector_clamp:1(24) scalar_clamp:1(25) scalar_opc:6(26-31)`; word1 `src3_swiz:8(0-7) src2_swiz:8(8-15) src1_swiz:8(16-23) src3_reg_negate:1(24) src2_reg_negate:1(25) src1_reg_negate:1(26) pred_condition:1(27) is_predicated:1(28) const_address_register_relative:1(29) const_1_rel_abs:1(30) const_0_rel_abs:1(31)`; word2 `src3_reg:8(0-7) src2_reg:8(8-15) src1_reg:8(16-23) vector_opc:5(24-28) src3_sel:1(29) src2_sel:1(30) src1_sel:1(31)`.
- Real `VertexFetchInstruction` (3 raw dwords): word0 `opcode:5(0-4) src_reg:6(5-10) src_reg_am:1(11) dst_reg:6(12-17) dst_reg_am:1(18) must_be_one:1(19) const_index:5(20-24) const_index_sel:2(25-26) prefetch_count:3(27-29) src_swiz:2(30-31)`; word1 `dst_swiz:12(0-11) fomat_comp_all:1(12) num_format_all:1(13) signed_rf_mode_all:1(14) is_index_rounded:1(15) format:6(16-21) reserved2:2(22-23) exp_adjust:6(24-29, signed) is_mini_fetch:1(30) is_predicated:1(31)`; word2 `stride:8(0-7) offset:23(8-30, signed) pred_condition:1(31)`. Real fetch-constant slot: `const_index * 3 + const_index_sel`.
- Real `PM4_IM_LOAD_IMMEDIATE` packet payload: dword 0 = shader type (`0`=vertex, `1`=pixel), dword 1 = `start_size` (bits 16-31 = start, expected `0`; bits 0-15 = `size_dwords`), followed by `size_dwords` raw big-endian microcode dwords embedded directly in the packet (need byte-swapping via the existing `LoadU32` helper, same as every other PM4 dword in this file).
- Headless regression baseline that must stay byte-for-byte unchanged throughout: 153 `NtReadFile` lines, tail message ending `"_xstart did not return within 10 seconds (watchdog timeout) -- this is an expected, informative outcome for Phase 2A, not a crash."`, exit code 2. Test command: `./build/BigBumpinHost > run.log 2>&1; echo "exit: $?"; grep -c NtReadFile run.log; tail -3 run.log`.
- Full `AluVectorOpcode`/`AluScalarOpcode` name tables are out of scope for this plan (each spans dozens of real values) — decode to a bounded numeric opcode value with a range check (0-31 for the 5-bit `vector_opc`, 0-63 for the 6-bit `scalar_opc`) rather than a name lookup table.

## Review Focus

- A control-flow `address`/`count` pair that references slots past the real `dwordCount` (a corrupt or truncated microcode blob) — a reasonable implementation clamps/skips and logs, never reads out of bounds. Task 2's bounds-check test exercises this directly.
- An unrecognized `shaderType` value in the real `PM4_IM_LOAD_IMMEDIATE` payload (anything but 0 or 1) — must be logged and skipped, not passed into the decoder with an undefined meaning. Task 3's Step covers this.
- A `start_size` field with a non-zero `start` (Xenia itself asserts this is always 0 for real traffic) — must be logged and skipped per the spec's own error-handling section, not silently ignored. Task 3's Step covers this.
- A packet whose declared `count` doesn't actually leave room for `size_dwords` worth of payload (`count - 2 < size_dwords`) — must be logged and skipped, not read past the packet's own declared payload. Task 3's Step covers this.
- A `ControlFlowOpcode` that is a real, valid value but NOT in the EXEC-family list (e.g. `kLoopStart`, `kCondJmp`) — must still produce a real, readable disassembly line (opcode name + raw address/count), not be silently dropped just because this plan doesn't resolve its branching semantics. Task 1's Step 3 covers this.

---

## Task 1: Control-flow instruction decode (`host/shader_decode.h/.cpp`)

**Files:**
- Create: `host/shader_decode.h`
- Create: `host/shader_decode.cpp`
- Modify: `CMakeLists.txt:28` (add `host/shader_decode.cpp` to the `BigBumpinHost` source list)

**Interfaces:**
- Produces: `enum class ControlFlowOpcode : uint32_t` with all 16 real values; `bool IsExecFamily(ControlFlowOpcode op)`; `struct ControlFlowInstruction { ControlFlowOpcode opcode; uint32_t address; uint32_t count; uint32_t sequence; };`; `void UnpackControlFlowPair(uint32_t d0, uint32_t d1, uint32_t d2, ControlFlowInstruction& outA, ControlFlowInstruction& outB);`.

- [ ] **Step 1: Write a standalone smoke-test main exercising the not-yet-existing types**

Create `host/shader_decode_smoketest.cpp` (temporary, deleted in Step 5):

```cpp
#include "shader_decode.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Real layout: word0 bits [address:12][count:3][_pad is_yield:1][sequence:12][_pad vc_hi:4]
    // word1 (16 bits only): [_pad vc_lo:2][_pad:7][_pad is_predicate_clean:1][_pad:1][_pad address_mode:1][opcode:4]
    uint32_t address = 5;
    uint32_t count = 3;
    uint32_t sequence = 0x2A; // arbitrary 12-bit value, bit layout checked separately below
    uint32_t word0A = (address & 0xFFF) | ((count & 0x7) << 12) | ((sequence & 0xFFF) << 16);
    uint32_t opcodeA = 2; // kExecEnd
    uint32_t word1A16 = (opcodeA & 0xF) << 12;

    // Pack instruction A into d0, d1 (low 16 bits), and a second instruction B
    // into (d1 high 16 bits) | (d2 low 16 bits), d2 high 16 bits, per the real
    // 3-dwords-to-2-instructions scheme.
    uint32_t addressB = 10;
    uint32_t countB = 1;
    uint32_t sequenceB = 0x001;
    uint32_t word0B = (addressB & 0xFFF) | ((countB & 0x7) << 12) | ((sequenceB & 0xFFF) << 16);
    uint32_t opcodeB = 8; // kLoopEnd
    uint32_t word1B16 = (opcodeB & 0xF) << 12;

    uint32_t d0 = word0A;
    uint32_t d1 = (word1A16 & 0xFFFF) | (word0B << 16);
    uint32_t d2 = (word0B >> 16) | (word1B16 << 16);

    ControlFlowInstruction a, b;
    UnpackControlFlowPair(d0, d1, d2, a, b);

    assert(a.opcode == ControlFlowOpcode::kExecEnd);
    assert(a.address == 5);
    assert(a.count == 3);
    assert(a.sequence == 0x2A);

    assert(b.opcode == ControlFlowOpcode::kLoopEnd);
    assert(b.address == 10);
    assert(b.count == 1);
    assert(b.sequence == 0x001);

    assert(IsExecFamily(ControlFlowOpcode::kExec));
    assert(IsExecFamily(ControlFlowOpcode::kExecEnd));
    assert(IsExecFamily(ControlFlowOpcode::kCondExec));
    assert(IsExecFamily(ControlFlowOpcode::kCondExecEnd));
    assert(IsExecFamily(ControlFlowOpcode::kCondExecPred));
    assert(IsExecFamily(ControlFlowOpcode::kCondExecPredEnd));
    assert(IsExecFamily(ControlFlowOpcode::kCondExecPredClean));
    assert(IsExecFamily(ControlFlowOpcode::kCondExecPredCleanEnd));
    assert(!IsExecFamily(ControlFlowOpcode::kLoopStart));
    assert(!IsExecFamily(ControlFlowOpcode::kLoopEnd));
    assert(!IsExecFamily(ControlFlowOpcode::kNop));

    printf("shader_decode_smoketest (control flow): PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (the types don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp -o /tmp/shader_decode_smoketest 2>&1`
Expected: FAIL — `'shader_decode.h' file not found` (or equivalent; the header does not exist yet).

- [ ] **Step 3: Write `host/shader_decode.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Real Xenos control-flow opcodes (xenos::ControlFlowOpcode). The shader
// microcode's control-flow program is a sequence of these, each either
// executing a block of ALU/fetch instructions (the 8 "EXEC-family"
// values -- see IsExecFamily) or controlling flow in a way this decoder
// does not resolve (loops, calls, jumps) -- it disassembles those as
// plain opcode + raw field text, it does not simulate them.
enum class ControlFlowOpcode : uint32_t
{
    kNop = 0,
    kExec = 1,
    kExecEnd = 2,
    kCondExec = 3,
    kCondExecEnd = 4,
    kCondExecPred = 5,
    kCondExecPredEnd = 6,
    kLoopStart = 7,
    kLoopEnd = 8,
    kCondCall = 9,
    kReturn = 10,
    kCondJmp = 11,
    kAlloc = 12,
    kCondExecPredClean = 13,
    kCondExecPredCleanEnd = 14,
    kMarkVsFetchDone = 15,
};

// True for the 8 real opcodes whose address/count fields reference a
// block of ALU/fetch instructions to execute (real Xenia
// IsControlFlowOpcodeExec semantics).
bool IsExecFamily(ControlFlowOpcode op);

// One decoded real control-flow instruction. address/count only have a
// defined meaning for EXEC-family opcodes (see IsExecFamily) -- they are
// still decoded and reported for every opcode, since the raw fields are
// real regardless of whether this decoder resolves their semantics.
struct ControlFlowInstruction
{
    ControlFlowOpcode opcode;
    uint32_t address;  // real: slot index into the same flat microcode array
    uint32_t count;    // real: number of ALU/fetch instructions this block executes
    uint32_t sequence; // real: 2 bits per instruction, bit (2*j) = ALU(0)/fetch(1) for instruction j
};

// Unpacks 2 logical 48-bit control-flow instructions from 3 raw
// microcode dwords (real Xenos packing -- confirmed exact against
// Xenia's own UnpackControlFlowInstructions).
void UnpackControlFlowPair(uint32_t d0, uint32_t d1, uint32_t d2,
    ControlFlowInstruction& outA, ControlFlowInstruction& outB);
```

- [ ] **Step 4: Write `host/shader_decode.cpp`**

```cpp
#include "shader_decode.h"

bool IsExecFamily(ControlFlowOpcode op)
{
    switch (op)
    {
        case ControlFlowOpcode::kExec:
        case ControlFlowOpcode::kExecEnd:
        case ControlFlowOpcode::kCondExec:
        case ControlFlowOpcode::kCondExecEnd:
        case ControlFlowOpcode::kCondExecPred:
        case ControlFlowOpcode::kCondExecPredEnd:
        case ControlFlowOpcode::kCondExecPredClean:
        case ControlFlowOpcode::kCondExecPredCleanEnd:
            return true;
        default:
            return false;
    }
}

namespace
{
    ControlFlowInstruction DecodeOneControlFlowInstruction(uint32_t word0, uint32_t word1_16bit)
    {
        ControlFlowInstruction instr;
        instr.address = word0 & 0xFFF;
        instr.count = (word0 >> 12) & 0x7;
        instr.sequence = (word0 >> 16) & 0xFFF;
        instr.opcode = static_cast<ControlFlowOpcode>((word1_16bit >> 12) & 0xF);
        return instr;
    }
}

void UnpackControlFlowPair(uint32_t d0, uint32_t d1, uint32_t d2,
    ControlFlowInstruction& outA, ControlFlowInstruction& outB)
{
    uint32_t aWord0 = d0;
    uint32_t aWord1 = d1 & 0xFFFF;
    uint32_t bWord0 = (d1 >> 16) | (d2 << 16);
    uint32_t bWord1 = d2 >> 16;

    outA = DecodeOneControlFlowInstruction(aWord0, aWord1);
    outB = DecodeOneControlFlowInstruction(bWord0, bWord1);
}
```

- [ ] **Step 5: Compile and run the smoke test to verify it passes, then delete it**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp host/shader_decode.cpp -o /tmp/shader_decode_smoketest && /tmp/shader_decode_smoketest`
Expected: `shader_decode_smoketest (control flow): PASS`, exit code 0.

```bash
rm host/shader_decode_smoketest.cpp
```

- [ ] **Step 6: Add to the build, rebuild the real project, verify regression, commit**

Edit `CMakeLists.txt:28` — change:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/game_overrides.cpp host/renderer_metal.mm)
```
to:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/shader_decode.cpp host/game_overrides.cpp host/renderer_metal.mm)
```

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/shader_decode.h host/shader_decode.cpp CMakeLists.txt
git commit -m "feat: add real control-flow instruction decode (unused yet)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — byte-for-byte the established baseline, since `shader_decode.cpp` is compiled in but not yet referenced by any existing code path.

---

## Task 2: ALU/vertex-fetch instruction decode and the full program decoder

**Files:**
- Modify: `host/shader_decode.h` (add structs + `DecodeShaderMicrocode` declaration)
- Modify: `host/shader_decode.cpp` (add implementations)

**Interfaces:**
- Consumes: `ControlFlowInstruction`, `UnpackControlFlowPair`, `IsExecFamily` (Task 1).
- Produces: `struct DecodedShaderProgram { std::vector<std::string> disassemblyLines; };`, `DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType);`.

- [ ] **Step 1: Write a failing smoke test for ALU/vertex-fetch decode and the full decoder**

Create `host/shader_decode_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "shader_decode.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    // --- A real, hand-constructed minimal microcode program ---
    // Slot 0: one control-flow pair (3 dwords) encoding:
    //   instruction A: kExecEnd, address=1, count=1, sequence=0 (ALU, instr 0)
    //   instruction B: kNop (unused)
    uint32_t cfWord0A = (1 /*address*/) | (1 /*count*/ << 12) | (0 /*sequence*/ << 16);
    uint32_t cfOpcodeA = 2; // kExecEnd
    uint32_t cfWord1A16 = (cfOpcodeA & 0xF) << 12;
    uint32_t cfWord0B = 0;
    uint32_t cfOpcodeB = 0; // kNop
    uint32_t cfWord1B16 = (cfOpcodeB & 0xF) << 12;

    uint32_t d0 = cfWord0A;
    uint32_t d1 = (cfWord1A16 & 0xFFFF) | (cfWord0B << 16);
    uint32_t d2 = (cfWord0B >> 16) | (cfWord1B16 << 16);

    // Slot 1 (address=1): one real AluInstruction (3 dwords).
    // word0: scalar_opc bits 26-31 = 5, everything else 0.
    uint32_t aluWord0 = (5u << 26);
    // word2: vector_opc bits 24-28 = 9, src1_reg bits 16-23 = 3.
    uint32_t aluWord2 = (9u << 24) | (3u << 16);
    uint32_t aluWord1 = 0;

    uint32_t dwords[6] = { d0, d1, d2, aluWord0, aluWord1, aluWord2 };

    DecodedShaderProgram program = DecodeShaderMicrocode(dwords, 6, 0 /*vertex*/);

    // At least one line for the control-flow instruction and one for the
    // ALU instruction it references.
    bool sawExecEnd = false;
    bool sawAlu = false;
    for (const std::string& line : program.disassemblyLines)
    {
        if (line.find("kExecEnd") != std::string::npos) sawExecEnd = true;
        if (line.find("ALU") != std::string::npos && line.find("vector_opc=9") != std::string::npos
            && line.find("scalar_opc=5") != std::string::npos) sawAlu = true;
    }
    assert(sawExecEnd);
    assert(sawAlu);

    // --- Direct unit test of DecodeVertexFetchInstruction (the real
    // fetch-constant-index formula is the single most important real
    // fact this decoder produces -- it must have its own direct test,
    // not just be exercised incidentally through the full-program path
    // above, which only covered the ALU case). const_index=7,
    // const_index_sel=2 -> real fetchConstantIndex = 7*3+2 = 23.
    {
        uint32_t fetchWord0 = (7u << 20) /*const_index*/ | (2u << 25) /*const_index_sel*/
            | (4u << 12) /*dst_reg*/ | (9u << 5) /*src_reg*/;
        uint32_t fetchWord1 = (36u << 16); /*format*/
        uint32_t fetchWord2 = (12u) /*stride*/ | (5u << 8); /*offset*/
        VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(fetchWord0, fetchWord1, fetchWord2);
        assert(vf.fetchConstantIndex == 23);
        assert(vf.destReg == 4);
        assert(vf.srcReg == 9);
        assert(vf.format == 36);
        assert(vf.stride == 12);
        assert(vf.offset == 5);
    }

    // --- Full-program test of the FETCH path (sequence bit=1 selects
    // fetch instead of ALU) ---
    {
        uint32_t cfWord0F = (1u /*address*/) | (1u /*count*/ << 12) | (1u /*sequence, bit0=1=fetch*/ << 16);
        uint32_t cfOpcodeF = 1; // kExec
        uint32_t cfWord1F16 = (cfOpcodeF & 0xF) << 12;
        uint32_t fd0 = cfWord0F;
        uint32_t fd1 = (cfWord1F16 & 0xFFFF);
        uint32_t fd2 = 0;

        uint32_t fetchWord0 = (7u << 20) | (2u << 25);
        uint32_t fetchWord1 = 0;
        uint32_t fetchWord2 = 0;
        uint32_t fetchDwords[6] = { fd0, fd1, fd2, fetchWord0, fetchWord1, fetchWord2 };

        DecodedShaderProgram fetchProgram = DecodeShaderMicrocode(fetchDwords, 6, 0);
        bool sawFetch = false;
        for (const std::string& line : fetchProgram.disassemblyLines)
        {
            if (line.find("FETCH") != std::string::npos && line.find("fetchConstantIndex=23") != std::string::npos)
            {
                sawFetch = true;
            }
        }
        assert(sawFetch);
    }

    // --- Bounds safety: a control-flow address/count that reaches past
    // dwordCount must not crash or read out of bounds. ---
    uint32_t cfOnlyWord0 = (100 /*address, way out of range*/) | (5 /*count*/ << 12);
    uint32_t cfOnlyOpcode = 1; // kExec
    uint32_t cfOnlyWord1_16 = (cfOnlyOpcode & 0xF) << 12;
    uint32_t od0 = cfOnlyWord0;
    uint32_t od1 = (cfOnlyWord1_16 & 0xFFFF);
    uint32_t od2 = 0;
    uint32_t oobDwords[3] = { od0, od1, od2 };
    DecodedShaderProgram oobProgram = DecodeShaderMicrocode(oobDwords, 3, 0);
    // Must return without crashing; exact line content isn't asserted,
    // just that it completed.
    (void)oobProgram;

    printf("shader_decode_smoketest (alu/fetch/program): PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (types/function don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp host/shader_decode.cpp -o /tmp/shader_decode_smoketest 2>&1`
Expected: FAIL — compiler error, `'DecodedShaderProgram' was not declared` (or equivalent; `DecodeShaderMicrocode` doesn't exist yet).

- [ ] **Step 3: Add the ALU/vertex-fetch structs and `DecodeShaderMicrocode` to `host/shader_decode.h`**

Add to `host/shader_decode.h`, above the existing declarations:

```cpp
// Real Xenos ALU instruction fields this decoder reports (bit-exact
// against Xenia's AluInstruction::Data). Full opcode NAME tables
// (AluVectorOpcode/AluScalarOpcode, each dozens of real values) are out
// of scope for this decoder -- opcodes are reported as bounded numeric
// values (vectorOpcode: 0-31, scalarOpcode: 0-63).
struct AluInstructionFields
{
    uint32_t vectorOpcode;   // 5 bits
    uint32_t scalarOpcode;   // 6 bits
    uint32_t vectorDest;     // 6 bits
    uint32_t scalarDest;     // 6 bits
    uint32_t vectorWriteMask; // 4 bits
    uint32_t scalarWriteMask; // 4 bits
    uint32_t src1Reg;        // 8 bits
    uint32_t src2Reg;        // 8 bits
    uint32_t src3Reg;        // 8 bits
};
AluInstructionFields DecodeAluInstruction(uint32_t word0, uint32_t word1, uint32_t word2);

// Real Xenos vertex fetch instruction fields (bit-exact against Xenia's
// VertexFetchInstruction::Data). fetchConstantIndex is the real formula
// (const_index * 3 + const_index_sel) that replaces this project's
// earlier hardcoded "always slot 0" vertex-format assumption.
struct VertexFetchInstructionFields
{
    uint32_t fetchConstantIndex; // 0-95, the real GpuRegisterState slot
    uint32_t destReg;            // 6 bits
    uint32_t srcReg;             // 6 bits
    uint32_t format;             // 6 bits (real xenos::VertexFormat value)
    uint32_t stride;             // 8 bits, in dwords
    int32_t offset;              // 23 bits, signed, in dwords
};
VertexFetchInstructionFields DecodeVertexFetchInstruction(uint32_t word0, uint32_t word1, uint32_t word2);

// Real, human-readable disassembly of a decoded shader microcode
// program -- one text line per instruction. No MSL generation, no
// control-flow execution/simulation; this is a static disassembly.
struct DecodedShaderProgram
{
    std::vector<std::string> disassemblyLines;
};

// dwords are already host-byte-order (the caller byte-swaps guest
// memory before calling this, matching this project's established
// convention). shaderType: 0 = vertex, 1 = pixel (real xenos::ShaderType
// values) -- used only to label the output, decode logic is identical
// for both.
DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType);
```

- [ ] **Step 4: Implement `DecodeAluInstruction`, `DecodeVertexFetchInstruction`, and `DecodeShaderMicrocode` in `host/shader_decode.cpp`**

Add `#include <cstdio>` to the top of `host/shader_decode.cpp` (for the `snprintf`-based line formatting below), then add:

```cpp
AluInstructionFields DecodeAluInstruction(uint32_t word0, uint32_t word1, uint32_t word2)
{
    (void)word1; // swizzle/predication/negate fields not decoded in this pass
    AluInstructionFields fields;
    fields.vectorDest = word0 & 0x3F;
    fields.scalarDest = (word0 >> 8) & 0x3F;
    fields.vectorWriteMask = (word0 >> 16) & 0xF;
    fields.scalarWriteMask = (word0 >> 20) & 0xF;
    fields.scalarOpcode = (word0 >> 26) & 0x3F;
    fields.src1Reg = (word2 >> 16) & 0xFF;
    fields.src2Reg = (word2 >> 8) & 0xFF;
    fields.src3Reg = word2 & 0xFF;
    fields.vectorOpcode = (word2 >> 24) & 0x1F;
    return fields;
}

VertexFetchInstructionFields DecodeVertexFetchInstruction(uint32_t word0, uint32_t word1, uint32_t word2)
{
    uint32_t constIndex = (word0 >> 20) & 0x1F;
    uint32_t constIndexSel = (word0 >> 25) & 0x3;
    VertexFetchInstructionFields fields;
    fields.fetchConstantIndex = constIndex * 3 + constIndexSel;
    fields.srcReg = (word0 >> 5) & 0x3F;
    fields.destReg = (word0 >> 12) & 0x3F;
    fields.format = (word1 >> 16) & 0x3F;
    fields.stride = word2 & 0xFF;
    uint32_t rawOffset = (word2 >> 8) & 0x7FFFFF;
    // Sign-extend the real 23-bit signed offset field.
    if (rawOffset & 0x400000) rawOffset |= 0xFF800000;
    fields.offset = static_cast<int32_t>(rawOffset);
    return fields;
}

DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType)
{
    DecodedShaderProgram program;
    char line[256];
    const char* shaderTag = (shaderType == 1) ? "PS" : "VS";

    uint32_t cfPairCount = dwordCount / 3;
    for (uint32_t i = 0; i < cfPairCount; i++)
    {
        ControlFlowInstruction a, b;
        UnpackControlFlowPair(dwords[i * 3], dwords[i * 3 + 1], dwords[i * 3 + 2], a, b);

        ControlFlowInstruction pairInstrs[2] = { a, b };
        for (int which = 0; which < 2; which++)
        {
            const ControlFlowInstruction& cf = pairInstrs[which];
            const char* opcodeName = "UNKNOWN";
            switch (cf.opcode)
            {
                case ControlFlowOpcode::kNop: opcodeName = "kNop"; break;
                case ControlFlowOpcode::kExec: opcodeName = "kExec"; break;
                case ControlFlowOpcode::kExecEnd: opcodeName = "kExecEnd"; break;
                case ControlFlowOpcode::kCondExec: opcodeName = "kCondExec"; break;
                case ControlFlowOpcode::kCondExecEnd: opcodeName = "kCondExecEnd"; break;
                case ControlFlowOpcode::kCondExecPred: opcodeName = "kCondExecPred"; break;
                case ControlFlowOpcode::kCondExecPredEnd: opcodeName = "kCondExecPredEnd"; break;
                case ControlFlowOpcode::kLoopStart: opcodeName = "kLoopStart"; break;
                case ControlFlowOpcode::kLoopEnd: opcodeName = "kLoopEnd"; break;
                case ControlFlowOpcode::kCondCall: opcodeName = "kCondCall"; break;
                case ControlFlowOpcode::kReturn: opcodeName = "kReturn"; break;
                case ControlFlowOpcode::kCondJmp: opcodeName = "kCondJmp"; break;
                case ControlFlowOpcode::kAlloc: opcodeName = "kAlloc"; break;
                case ControlFlowOpcode::kCondExecPredClean: opcodeName = "kCondExecPredClean"; break;
                case ControlFlowOpcode::kCondExecPredCleanEnd: opcodeName = "kCondExecPredCleanEnd"; break;
                case ControlFlowOpcode::kMarkVsFetchDone: opcodeName = "kMarkVsFetchDone"; break;
            }
            snprintf(line, sizeof(line), "[%s] CF pair=%u.%d %s address=%u count=%u sequence=0x%03X",
                shaderTag, i, which, opcodeName, cf.address, cf.count, cf.sequence);
            program.disassemblyLines.push_back(line);

            if (!IsExecFamily(cf.opcode))
            {
                continue;
            }

            for (uint32_t j = 0; j < cf.count; j++)
            {
                uint32_t slot = cf.address + j;
                uint32_t slotDwordStart = slot * 3;
                if (slotDwordStart + 2 >= dwordCount)
                {
                    snprintf(line, sizeof(line), "[%s]   instr %u: slot %u out of range (dwordCount=%u), skipped",
                        shaderTag, j, slot, dwordCount);
                    program.disassemblyLines.push_back(line);
                    continue;
                }

                uint32_t iw0 = dwords[slotDwordStart];
                uint32_t iw1 = dwords[slotDwordStart + 1];
                uint32_t iw2 = dwords[slotDwordStart + 2];
                bool isFetch = (cf.sequence >> (2 * j)) & 0x1;

                if (isFetch)
                {
                    VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(iw0, iw1, iw2);
                    snprintf(line, sizeof(line), "[%s]   instr %u: FETCH fetchConstantIndex=%u destReg=%u srcReg=%u format=%u stride=%u offset=%d",
                        shaderTag, j, vf.fetchConstantIndex, vf.destReg, vf.srcReg, vf.format, vf.stride, vf.offset);
                }
                else
                {
                    AluInstructionFields alu = DecodeAluInstruction(iw0, iw1, iw2);
                    snprintf(line, sizeof(line), "[%s]   instr %u: ALU vector_opc=%u scalar_opc=%u vectorDest=%u scalarDest=%u src1=%u src2=%u src3=%u",
                        shaderTag, j, alu.vectorOpcode, alu.scalarOpcode, alu.vectorDest, alu.scalarDest, alu.src1Reg, alu.src2Reg, alu.src3Reg);
                }
                program.disassemblyLines.push_back(line);
            }
        }
    }

    return program;
}
```

- [ ] **Step 5: Compile and run to verify the smoke test passes**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp host/shader_decode.cpp -o /tmp/shader_decode_smoketest && /tmp/shader_decode_smoketest`
Expected: `shader_decode_smoketest (alu/fetch/program): PASS`, exit code 0.

- [ ] **Step 6: Delete the temporary smoke test, rebuild the real project, verify regression, commit**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
rm host/shader_decode_smoketest.cpp
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/shader_decode.h host/shader_decode.cpp
git commit -m "feat: add ALU/vertex-fetch instruction decode and the full microcode decoder

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — unchanged, since `DecodeShaderMicrocode` is still unused by any real code path.

---

## Task 3: Wire real `PM4_IM_LOAD_IMMEDIATE` parsing into `gpu_trace.cpp`

**Files:**
- Modify: `host/gpu_trace.cpp` (add opcode constant, add a new TYPE3 case, update the log-name ternary)

**Interfaces:**
- Consumes: `DecodeShaderMicrocode`, `DecodedShaderProgram` (Task 2); `LoadU32` (existing file-local helper).

- [ ] **Step 1: Add the `#include` and opcode constant**

In `host/gpu_trace.cpp`, add near the top (after the existing `#include <cstring>` if present from the prior sub-project, otherwise after `#include <ppc_context.h>`):
```cpp
#include "shader_decode.h"
```

In the anonymous namespace near the top (alongside `kOpcodeDrawIndx`/`kOpcodeDrawIndx2`), add:
```cpp
    constexpr uint32_t kOpcodeImLoadImmediate = 0x2B;  // PM4_IM_LOAD_IMMEDIATE
```

- [ ] **Step 2: Update the log-name ternary for readability**

Replace:
```cpp
            const char* name = (opcode == kOpcodeMeInit) ? " (ME_INIT)"
                : (opcode == kOpcodeIndirectBuffer) ? " (INDIRECT_BUFFER)"
                : (opcode == kOpcodeSetConstant) ? " (SET_CONSTANT)" : "";
```
with:
```cpp
            const char* name = (opcode == kOpcodeMeInit) ? " (ME_INIT)"
                : (opcode == kOpcodeIndirectBuffer) ? " (INDIRECT_BUFFER)"
                : (opcode == kOpcodeSetConstant) ? " (SET_CONSTANT)"
                : (opcode == kOpcodeImLoadImmediate) ? " (IM_LOAD_IMMEDIATE)" : "";
```

- [ ] **Step 3: Add the new case after the existing `kOpcodeDrawIndx` handling**

In `host/gpu_trace.cpp`, add after the existing:
```cpp
            else if (opcode == kOpcodeDrawIndx && count >= 2)
            {
                // DRAW_INDX has a leading viz-query-condition dword before
                // the draw initiator (Xenia's ExecutePacketType3_DRAW_INDX).
                uint32_t drawInitiatorValue = LoadU32(base, bufferAddr + offsetBytes + 8);
                gpuState_.WriteRegister(GpuRegisterState::kDrawInitiatorRegister, drawInitiatorValue);
            }
```
and before `offsetBytes += 4 + payloadBytes;`:

```cpp
            else if (opcode == kOpcodeImLoadImmediate && count >= 2)
            {
                // Real semantics (Xenia's ExecutePacketType3_IM_LOAD_IMMEDIATE):
                // payload dword 0 is the shader type (0=vertex, 1=pixel),
                // dword 1 is start_size (bits 16-31 = start, expected 0;
                // bits 0-15 = size_dwords), followed by size_dwords raw
                // big-endian microcode dwords embedded directly in the
                // packet -- byte-swapped here the same way every other PM4
                // dword in this file already is.
                uint32_t shaderTypeValue = LoadU32(base, bufferAddr + offsetBytes + 4);
                uint32_t startSizeValue = LoadU32(base, bufferAddr + offsetBytes + 8);
                uint32_t start = startSizeValue >> 16;
                uint32_t sizeDwords = startSizeValue & 0xFFFF;

                if (start != 0)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: non-zero start=%u, skipped\n", indent, start);
                }
                else if (count - 2 < sizeDwords)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: packet too short for sizeDwords=%u (count=%u), skipped\n", indent, sizeDwords, count);
                }
                else if (shaderTypeValue != 0 && shaderTypeValue != 1)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: unrecognized shaderType=%u, skipped\n", indent, shaderTypeValue);
                }
                else
                {
                    std::vector<uint32_t> microcodeDwords(sizeDwords);
                    for (uint32_t i = 0; i < sizeDwords; i++)
                    {
                        microcodeDwords[i] = LoadU32(base, bufferAddr + offsetBytes + 12 + i * 4);
                    }
                    DecodedShaderProgram program = DecodeShaderMicrocode(microcodeDwords.data(), sizeDwords, (int)shaderTypeValue);
                    if (logFile_)
                    {
                        fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: shaderType=%u sizeDwords=%u, %zu disassembly lines:\n",
                            indent, shaderTypeValue, sizeDwords, program.disassemblyLines.size());
                        for (const std::string& disasmLine : program.disassemblyLines)
                        {
                            fprintf(logFile_, "%s  %s\n", indent, disasmLine.c_str());
                        }
                    }
                }
            }
```

- [ ] **Step 4: Add `#include <vector>` and `#include <string>` if not already present**

Check the top of `host/gpu_trace.cpp` for existing `#include <vector>`/`#include <string>` (added in the prior sub-project for `DrawCommand`'s own `std::vector<uint8_t>` usage, transitively visible via `gpu_draw_list.h`). If `std::vector<uint32_t>` and `std::string` are not already visible, add:
```cpp
#include <vector>
#include <string>
```
near the top, alongside the existing `#include <cstring>`.

- [ ] **Step 5: Build and run live, verify the regression baseline and real decoded output**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log
./build/BigBumpinHost --window > run2.log 2>&1
echo "exit: $?"
grep -A 20 "IM_LOAD_IMMEDIATE: shaderType=0" gpu_trace.log | head -40
grep -A 10 "IM_LOAD_IMMEDIATE: shaderType=1" gpu_trace.log | head -20
rm -f run2.log gpu_trace.log
```
Expected: headless run unchanged (exit 2, `153`, same tail message). `--window` run exits 2 (unchanged). The real decoded output should show: a `shaderType=0` (vertex) entry with `sizeDwords` around 27 (matching this project's own already-confirmed real `count=29` minus the 2 header dwords) and a `shaderType=1` (pixel) entry with `sizeDwords` around 9 (matching `count=11` minus 2). Each should show at least one real `CF pair` line with a valid opcode name (not `UNKNOWN`), and at least one `ALU` or `FETCH` instruction line. If a `FETCH` line appears for the vertex shader, check its `fetchConstantIndex` value against `0` — a match would independently confirm sub-project 1's register tracking and this new microcode decode agree on the same real vertex buffer slot (not required to match for this task to be considered complete, since the real shader might reference a different slot than sub-project 2's placeholder guessed — report whatever the real value is).

- [ ] **Step 6: Commit**

```bash
git add host/gpu_trace.cpp
git commit -m "feat: decode real PM4_IM_LOAD_IMMEDIATE shader microcode

Live-verified (--window run): both of this project's own real shader
loads (vertex, ~27 real microcode dwords; pixel, ~9 real microcode
dwords) decode to real, valid control-flow opcodes and ALU/fetch
instructions, not garbage.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
