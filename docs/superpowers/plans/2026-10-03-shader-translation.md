# Real Shader Translation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recognize the two real instruction patterns this project's actual shader pair uses (vertex fetch → attribute, ALU export-via-self-mov → output wiring), generate real MSL from them, and swap the placeholder Metal pipeline for a real compiled one when translation succeeds — falling back to the placeholder otherwise.

**Architecture:** Extend 3a's `shader_decode.h/.cpp` with swizzle/register-kind decode it deferred. A new, Metal-free `shader_translate.h/.cpp` walks the raw microcode directly (not 3a's disassembly text) and either produces real MSL + binding metadata or a tagged failure. A small hash-keyed cache on `GpuCommandTracer` carries the latest translation attempt (success or not) from the pump thread to the render thread, which owns its own separate memory of the last *successfully compiled* Metal pipeline.

**Tech Stack:** C++17, Objective-C++/Metal Shading Language, this project's existing PM4-parsing/byte-swap conventions.

**Spec:** `docs/superpowers/specs/2026-10-03-shader-translation-design.md`

## Global Constraints

- Real `AluInstructionFields` sel bits (word2): bit31=`src1_sel`, bit30=`src2_sel`, bit29=`src3_sel`. `true` means the operand is a real TEMP register (Xenia's `src_is_temp` semantics); `false` means a real CONSTANT register.
- Real ALU swizzle fields (word1): bits16-23=`src1_swiz`, bits8-15=`src2_swiz`, bits0-7=`src3_swiz`. Real component-relative resolve formula: `((rawSwizzle >> (2 * destComponent)) + destComponent) & 3`.
- Real vertex-fetch destination swizzle (word1, bits0-11, 3 bits/component): real absolute resolve formula `(rawSwizzle >> (3 * component)) & 0b111` → 0=X,1=Y,2=Z,3=W,4=const-0,5=const-1,7=keep-current.
- Real `ExportRegister` values (confirmed against Xenia's `ucode.h`): `kVSPosition = 62`; interpolators `kVSInterpolator0..15 = 0..15`; pixel shader `kPSColor0 = 0` (same numeric value as `kVSInterpolator0`, disambiguated by `shaderType`).
- This project's own real, already-live-verified shader pair, re-confirmed fresh immediately before this plan was written (resolves which of two textually-identical-looking ALU lines is the real filler vs. the real export):
  - **Real VS (27 dwords):** fetch slot 3 (`fetchConstantIndex=0`, `destReg=1`, `format=57`/float3, `stride=7`, `offset=0`, not a mini-fetch) and fetch slot 4 (`fetchConstantIndex=0`, `destReg=0`, `format=38`/float4, `offset=3`, **is** a mini-fetch). ALU slot 5 is the real **filler**: write mask `0`, `src1Reg=src2Reg=0`, both real `CONSTANT` (`sel=false`) — skip, not a failure. ALU slot 6 is the real **position export**: `vectorDest=62`, write mask `0xF`, `src1Reg=src2Reg=1`, both real `TEMP` (`sel=true`), `vector_opc=2` (kMax). ALU slot 7 (reached via `kExecEnd`) is the real **interpolator-0 export**: `vectorDest=0`, write mask `0xF`, `src1Reg=src2Reg=0`, both real `TEMP`, `vector_opc=2`.
  - **Real PS (9 dwords):** ALU slot 1 is the real **color-0 export**: `vectorDest=0`, write mask `0xF`, `src1Reg=src2Reg=0`, both real `TEMP`, `vector_opc=2`.
- Real recognized ALU export pattern: write mask `0` → skip (not a failure). Write mask non-zero and (`vector_opc==2` AND `src1Reg==src2Reg` AND both `sel==true`) → real export binding. Anything else with a non-zero write mask → translation fails for the whole shader.
- Real recognized vertex-fetch pattern: `format==57` → `Float3`; `format==38` → `Float4`; any other `format` value → translation fails for the whole shader. (A plain translator-internal `enum class TranslatedVertexFormat { Float3, Float4 };` is used — never embed Apple's raw `MTLVertexFormat` integer values in plain C++ code; the Objective-C++ renderer maps this enum to the real symbolic Metal constant.)
- Real FNV-1a 32-bit hash (standard, well-known constants): offset basis `2166136261u`, prime `16777619u`, over the raw microcode dword bytes (4 bytes per dword, in the order they appear in the `dwords` array passed to `TranslateShader`).
- Ruling on cache/fallback responsibility (the spec left room for either design, resolved here for a single, simple, correct split): `ShaderTranslationCache` stores and returns the latest translation *attempt* verbatim, success or not — it never tries to preserve a prior success internally. The renderer owns a separate, its-own memory of the last *successfully compiled* Metal pipeline, and only replaces it when both the cached vertex and pixel results show `success == true` and their combined hash differs from what's already compiled. A failed attempt therefore leaves whatever pipeline the renderer already had running untouched.
- Headless regression baseline that must stay byte-for-byte unchanged throughout: 153 `NtReadFile` lines, tail message ending `"_xstart did not return within 10 seconds (watchdog timeout) -- this is an expected, informative outcome for Phase 2A, not a crash."`, exit code 2. Test command: `./build/BigBumpinHost > run.log 2>&1; echo "exit: $?"; grep -c NtReadFile run.log; tail -3 run.log`.

## Review Focus

- A vertex-fetch instruction whose real `format` has no recognized mapping (anything but 57/38) — a reasonable implementation fails that whole shader's translation cleanly, not silently emits wrong-format MSL or crashes. Task 2's test exercises this directly.
- An ALU export instruction whose two sources are *not* both the same TEMP register (e.g. real arithmetic combining two different registers) — must fail translation, not silently generate MSL that only reads one operand. Task 2's test exercises this directly.
- Two translation attempts in a row that both fail (e.g. a shader this translator will never support) — must not re-attempt compilation every single frame once the hash has already been seen and already failed; Task 4's own hash-comparison gate already covers this structurally, but it's worth a specific live check since this project's real traffic reloads the same shader every frame.
- The renderer receiving a *newly successful* translation for one shader (say, the vertex shader) while the other (pixel shader) is still on a stale/failed hash — must not build a mismatched half-real, half-placeholder pipeline; Task 5's "both must have succeeded" gate covers this, worth a dedicated live check since this project's real two shaders load via two independent packets.
- A real mini-fetch instruction (confirmed present in this project's own real vertex shader) being treated as a *separate* vertex attribute at its own buffer offset rather than correctly sharing the preceding full fetch's real base address — Task 2's test uses this project's own real mini-fetch data and checks the generated attribute's `byteOffset`.

---

## Task 1: Swizzle and register-kind decode (`host/shader_decode.h/.cpp`)

**Files:**
- Modify: `host/shader_decode.h` (add fields to `AluInstructionFields`/`VertexFetchInstructionFields`, add two helper function declarations)
- Modify: `host/shader_decode.cpp` (add field extraction, add helper implementations)

**Interfaces:**
- Produces: `AluInstructionFields` gains `bool src1Sel, src2Sel, src3Sel;` and `uint32_t src1Swizzle, src2Swizzle, src3Swizzle;`. `VertexFetchInstructionFields` gains `uint32_t destSwizzle;`. New: `uint32_t ResolveAluSwizzleComponent(uint32_t rawSwizzle, uint32_t destComponent);`, `uint32_t GetFetchSwizzleComponent(uint32_t rawSwizzle, uint32_t component);`.

- [ ] **Step 1: Write a standalone smoke-test main exercising the new fields/helpers**

Create `host/shader_decode_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "shader_decode.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Real sel bits: word2 bit31=src1_sel, bit30=src2_sel, bit29=src3_sel.
    // true = TEMP, false = CONSTANT.
    uint32_t aluWord2 = (1u << 31) | (0u << 30) | (1u << 29);
    AluInstructionFields alu = DecodeAluInstruction(0, 0, aluWord2);
    assert(alu.src1Sel == true);
    assert(alu.src2Sel == false);
    assert(alu.src3Sel == true);

    // Real swizzle bits: word1 bits16-23=src1_swiz, 8-15=src2_swiz, 0-7=src3_swiz.
    uint32_t aluWord1 = (0x12u << 16) | (0x34u << 8) | 0x56u;
    AluInstructionFields alu2 = DecodeAluInstruction(0, aluWord1, 0);
    assert(alu2.src1Swizzle == 0x12);
    assert(alu2.src2Swizzle == 0x34);
    assert(alu2.src3Swizzle == 0x56);

    // Real component-relative resolve formula:
    // ((raw >> (2*destComponent)) + destComponent) & 3.
    // raw=0 means every 2-bit field is 0: component i resolves to
    // (0 + i) & 3 == i -- an identity swizzle.
    assert(ResolveAluSwizzleComponent(0, 0) == 0);
    assert(ResolveAluSwizzleComponent(0, 1) == 1);
    assert(ResolveAluSwizzleComponent(0, 2) == 2);
    assert(ResolveAluSwizzleComponent(0, 3) == 3);
    // raw with component-0 field = 2: resolves to (2+0)&3 = 2.
    assert(ResolveAluSwizzleComponent(0x2, 0) == 2);

    // Real vertex-fetch destSwizzle: word1 bits 0-11, 3 bits/component.
    uint32_t fetchWord1 = (5u << 9) | (3u << 6) | (1u << 3) | 0u; // comp3=5,comp2=3,comp1=1,comp0=0
    VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(0, fetchWord1, 0);
    assert(vf.destSwizzle == ((5u << 9) | (3u << 6) | (1u << 3) | 0u));

    // Real absolute per-component resolve formula: (raw >> (3*component)) & 0b111.
    assert(GetFetchSwizzleComponent(vf.destSwizzle, 0) == 0); // X
    assert(GetFetchSwizzleComponent(vf.destSwizzle, 1) == 1); // Y
    assert(GetFetchSwizzleComponent(vf.destSwizzle, 2) == 3); // W
    assert(GetFetchSwizzleComponent(vf.destSwizzle, 3) == 5); // const-1

    printf("shader_decode_smoketest (swizzle/sel decode): PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (the new fields/helpers don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp host/shader_decode.cpp -o /tmp/shader_decode_smoketest 2>&1`
Expected: FAIL — compiler error, `'struct AluInstructionFields' has no member named 'src1Sel'` (or equivalent).

- [ ] **Step 3: Add the fields and helpers**

In `host/shader_decode.h`, add to `AluInstructionFields` (after the existing fields):
```cpp
    bool src1Sel;            // true = real TEMP register, false = real CONSTANT register
    bool src2Sel;
    bool src3Sel;
    uint32_t src1Swizzle;    // 8 bits, raw (component-relative -- see ResolveAluSwizzleComponent)
    uint32_t src2Swizzle;
    uint32_t src3Swizzle;
```

Add to `VertexFetchInstructionFields` (after the existing fields):
```cpp
    uint32_t destSwizzle;    // 12 bits, raw (absolute per-component -- see GetFetchSwizzleComponent)
```

Add after the `VertexFetchInstructionFields` struct declaration, before `DecodeVertexFetchInstruction`'s own declaration is fine either order, but add these two new free functions near `DecodeAluInstruction`'s declaration:
```cpp
// Real component-relative swizzle resolve (confirmed exact against
// Xenia's GetSwizzledComponentIndex): returns which source component
// (0=x,1=y,2=z,3=w) feeds a given destination component (0-3).
uint32_t ResolveAluSwizzleComponent(uint32_t rawSwizzle, uint32_t destComponent);

// Real absolute per-component fetch-destination swizzle resolve
// (confirmed exact against Xenia's GetFetchDestinationComponentSwizzle):
// returns a real FetchDestinationSwizzle value for the given component
// (0-3): 0=X,1=Y,2=Z,3=W,4=const-0,5=const-1,7=keep-current.
uint32_t GetFetchSwizzleComponent(uint32_t rawSwizzle, uint32_t component);
```

In `host/shader_decode.cpp`, modify `DecodeAluInstruction` to add (before `return fields;`):
```cpp
    fields.src1Sel = ((word2 >> 31) & 0x1) != 0;
    fields.src2Sel = ((word2 >> 30) & 0x1) != 0;
    fields.src3Sel = ((word2 >> 29) & 0x1) != 0;
    fields.src1Swizzle = (word1 >> 16) & 0xFF;
    fields.src2Swizzle = (word1 >> 8) & 0xFF;
    fields.src3Swizzle = word1 & 0xFF;
```

Modify `DecodeVertexFetchInstruction` to add (before `return fields;`):
```cpp
    fields.destSwizzle = word1 & 0xFFF;
```

Add the two new free functions (anywhere at file scope, e.g. after `DecodeVertexFetchInstruction`'s own definition):
```cpp
uint32_t ResolveAluSwizzleComponent(uint32_t rawSwizzle, uint32_t destComponent)
{
    return ((rawSwizzle >> (2 * destComponent)) + destComponent) & 0x3;
}

uint32_t GetFetchSwizzleComponent(uint32_t rawSwizzle, uint32_t component)
{
    return (rawSwizzle >> (3 * component)) & 0x7;
}
```

- [ ] **Step 4: Compile and run the smoke test to verify it passes, then delete it**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_decode_smoketest.cpp host/shader_decode.cpp -o /tmp/shader_decode_smoketest && /tmp/shader_decode_smoketest`
Expected: `shader_decode_smoketest (swizzle/sel decode): PASS`, exit code 0.

```bash
rm host/shader_decode_smoketest.cpp
```

- [ ] **Step 5: Rebuild the real project, verify regression, commit**

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/shader_decode.h host/shader_decode.cpp
git commit -m "feat: decode real ALU/vertex-fetch swizzle and register-kind fields

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — byte-for-byte the established baseline, since these new fields/helpers are compiled in but not yet consumed by any real code path.

---

## Task 2: The real translator (`host/shader_translate.h/.cpp`)

**Files:**
- Create: `host/shader_translate.h`
- Create: `host/shader_translate.cpp`
- Modify: `CMakeLists.txt:28` (add `host/shader_translate.cpp` to the `BigBumpinHost` source list)

**Interfaces:**
- Consumes: `UnpackControlFlowPair`, `DecodeAluInstruction`, `DecodeVertexFetchInstruction`, `ResolveAluSwizzleComponent`, `IsExecFamily`, `ControlFlowInstruction`, `AluInstructionFields`, `VertexFetchInstructionFields` (Task 1 and 3a).
- Produces: `enum class TranslatedVertexFormat { Float3, Float4 };`, `struct TranslatedAttribute`, `struct TranslatedExport`, `struct TranslationResult`, `TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType);`.

- [ ] **Step 1: Write a standalone smoke-test main using this project's own real confirmed shader data**

Create `host/shader_translate_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "shader_translate.h"
#include <cassert>
#include <cstdio>

namespace
{
    // Builds the raw 3-dword control-flow-pair encoding this project's
    // own real UnpackControlFlowPair/DecodeOneControlFlowInstruction
    // expect, given two logical instructions' own real fields.
    void PackControlFlowPair(
        uint32_t addrA, uint32_t countA, uint32_t seqA, uint32_t opcodeA,
        uint32_t addrB, uint32_t countB, uint32_t seqB, uint32_t opcodeB,
        uint32_t& outD0, uint32_t& outD1, uint32_t& outD2)
    {
        uint32_t word0A = (addrA & 0xFFF) | ((countA & 0x7) << 12) | ((seqA & 0xFFF) << 16);
        uint32_t word1A16 = (opcodeA & 0xF) << 12;
        uint32_t word0B = (addrB & 0xFFF) | ((countB & 0x7) << 12) | ((seqB & 0xFFF) << 16);
        uint32_t word1B16 = (opcodeB & 0xF) << 12;

        outD0 = word0A;
        outD1 = (word1A16 & 0xFFFF) | (word0B << 16);
        outD2 = (word0B >> 16) | (word1B16 << 16);
    }
}

int main()
{
    // --- This project's own real, confirmed vertex shader (27 dwords) ---
    uint32_t vs[27] = {};
    // CF pair 0: kExec(address=3,count=2,seq=0x005) + kAlloc(unused)
    PackControlFlowPair(3, 2, 0x005, 1 /*kExec*/, 0, 0, 0, 12 /*kAlloc*/, vs[0], vs[1], vs[2]);
    // CF pair 1: kExec(address=5,count=1,seq=0x000) + kAlloc(unused)
    PackControlFlowPair(5, 1, 0x000, 1, 0, 0, 0, 12, vs[3], vs[4], vs[5]);
    // CF pair 2: kExec(address=6,count=1,seq=0x000) + kExecEnd(address=7,count=1,seq=0x000)
    PackControlFlowPair(6, 1, 0x000, 1, 7, 1, 0x000, 2 /*kExecEnd*/, vs[6], vs[7], vs[8]);

    // Slot 3: real fetch, fetchConstantIndex=0 (const_index=0,sel=0),
    // destReg=1, format=57, stride=7, offset=0, not mini-fetch.
    vs[9] = (0u << 20) | (0u << 25) | (1u << 12) | (0u << 5) | 0u /*opcode=kVertexFetch*/;
    vs[10] = (57u << 16);
    vs[11] = 7u | (0u << 8);
    // Slot 4: real fetch, fetchConstantIndex=0, destReg=0, format=38,
    // offset=3 dwords, IS a mini-fetch (word1 bit 30).
    vs[12] = (0u << 20) | (0u << 25) | (0u << 12) | (0u << 5) | 0u;
    vs[13] = (38u << 16) | (1u << 30);
    vs[14] = 0u | (3u << 8);
    // Slot 5: real filler. write mask 0 (word0 bits16-19=0), src1=src2=0
    // both CONSTANT (word2 bit31=0,bit30=0).
    vs[15] = 0u; // vectorDest=0, write masks=0
    vs[16] = 0u;
    vs[17] = 0u; // src1Reg=0,src2Reg=0, sel bits both 0 (CONSTANT)
    // Slot 6: real position export. vectorDest=62, write mask 0xF
    // (word0 bits16-19), src1=src2=1 both TEMP (word2 bits31,30=1),
    // vector_opc=2 (word2 bits24-28).
    vs[18] = 62u | (0xFu << 16);
    vs[19] = 0u;
    vs[20] = 1u | (1u << 16) | (2u << 24) | (1u << 31) | (1u << 30);
    // Slot 7: real interpolator-0 export. vectorDest=0, write mask 0xF,
    // src1=src2=0 both TEMP, vector_opc=2.
    vs[21] = 0u | (0xFu << 16);
    vs[22] = 0u;
    vs[23] = 0u | (0u << 16) | (2u << 24) | (1u << 31) | (1u << 30);
    // Slots 24-26: unused padding (never reached by the real CF program's
    // own address/count ranges).

    TranslationResult vsResult = TranslateShader(vs, 27, 0 /*vertex*/);
    assert(vsResult.success);
    assert(vsResult.attributes.size() == 2);
    assert(vsResult.attributes[0].fetchConstantIndex == 0);
    assert(vsResult.attributes[0].destReg == 1);
    assert(vsResult.attributes[0].format == TranslatedVertexFormat::Float3);
    assert(vsResult.attributes[0].byteOffset == 0);
    assert(vsResult.attributes[1].format == TranslatedVertexFormat::Float4);
    assert(vsResult.attributes[1].byteOffset == 12); // offset=3 dwords * 4
    assert(vsResult.vertexStrideBytes == 28); // stride=7 dwords * 4
    assert(vsResult.vertexExports.size() == 2);
    bool sawPosition = false, sawInterpolator0 = false;
    for (const TranslatedExport& e : vsResult.vertexExports)
    {
        if (e.exportRegister == 62) { sawPosition = true; assert(e.sourceRegister == 1); }
        if (e.exportRegister == 0) { sawInterpolator0 = true; assert(e.sourceRegister == 0); }
    }
    assert(sawPosition);
    assert(sawInterpolator0);
    assert(!vsResult.vertexShaderSource.empty());

    // --- This project's own real, confirmed pixel shader (9 dwords) ---
    uint32_t ps[9] = {};
    PackControlFlowPair(0, 0, 0x000, 12 /*kAlloc*/, 1, 1, 0x000, 2 /*kExecEnd*/, ps[0], ps[1], ps[2]);
    // Slot 1: real color-0 export. vectorDest=0, write mask 0xF,
    // src1=src2=0 both TEMP, vector_opc=2.
    ps[3] = 0u | (0xFu << 16);
    ps[4] = 0u;
    ps[5] = 0u | (0u << 16) | (2u << 24) | (1u << 31) | (1u << 30);

    TranslationResult psResult = TranslateShader(ps, 9, 1 /*pixel*/);
    assert(psResult.success);
    assert(psResult.pixelExports.size() == 1);
    assert(psResult.pixelExports[0].exportRegister == 0);
    assert(psResult.pixelExports[0].sourceRegister == 0);
    assert(!psResult.fragmentShaderSource.empty());

    // --- Review Focus: unsupported vertex-fetch format fails cleanly ---
    uint32_t badFormat[6] = {};
    PackControlFlowPair(1, 1, 0x001 /*bit0=1=fetch*/, 1 /*kExec*/, 0, 0, 0, 0, badFormat[0], badFormat[1], badFormat[2]);
    badFormat[3] = (0u << 20) | (0u << 25) | (0u << 12) | (0u << 5) | 0u;
    badFormat[4] = (6u << 16); // format=6 (k_8_8_8_8) -- not Float3/Float4, unsupported
    badFormat[5] = 0u;
    TranslationResult badFormatResult = TranslateShader(badFormat, 6, 0);
    assert(!badFormatResult.success);
    assert(!badFormatResult.failureReason.empty());

    // --- Review Focus: non-self-mov ALU (real arithmetic) fails cleanly ---
    uint32_t badAlu[6] = {};
    PackControlFlowPair(1, 1, 0x000, 1 /*kExec*/, 0, 0, 0, 0, badAlu[0], badAlu[1], badAlu[2]);
    badAlu[3] = 0u | (0xFu << 16); // write mask 0xF, vectorDest=0
    badAlu[4] = 0u;
    badAlu[5] = 0u | (1u << 16) | (2u << 24) | (1u << 31) | (1u << 30); // src1=0,src2=1 -- DIFFERENT registers
    TranslationResult badAluResult = TranslateShader(badAlu, 6, 0);
    assert(!badAluResult.success);

    printf("shader_translate_smoketest: PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (the types/function don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_translate_smoketest.cpp host/shader_translate.cpp host/shader_decode.cpp -o /tmp/shader_translate_smoketest 2>&1`
Expected: FAIL — `'shader_translate.h' file not found` (the header does not exist yet).

- [ ] **Step 3: Write `host/shader_translate.h`**

```cpp
#pragma once
#include "shader_decode.h"
#include <cstdint>
#include <string>
#include <vector>

// A plain, Metal-free representation of a real vertex attribute format
// this translator recognizes. The Objective-C++ renderer maps this to
// the real symbolic MTLVertexFormat constant -- this header never
// embeds Apple's raw enum integer values.
enum class TranslatedVertexFormat
{
    Float3,
    Float4,
};

// A real vertex fetch instruction this translator recognized and turned
// into an attribute binding.
struct TranslatedAttribute
{
    uint32_t fetchConstantIndex; // 0-95, the real GpuRegisterState slot
    uint32_t destReg;            // which register this attribute lands in
    TranslatedVertexFormat format;
    uint32_t byteOffset;         // real offset field * 4
};

// A real ALU export-via-self-mov instruction this translator recognized.
struct TranslatedExport
{
    uint32_t exportRegister; // real ExportRegister value (vectorDest)
    uint32_t sourceRegister; // the real TEMP register both sources matched
};

// The result of attempting to translate one real shader's microcode.
// On failure, vertexShaderSource/fragmentShaderSource/attributes/exports
// are all empty -- callers must check success before using anything else.
struct TranslationResult
{
    bool success = false;
    std::string failureReason;
    std::string vertexShaderSource;   // real MSL vertex_main function text, only set for shaderType==0
    std::string fragmentShaderSource; // real MSL fragment_main function text, only set for shaderType==1
    std::vector<TranslatedAttribute> attributes;   // only populated for shaderType==0
    std::vector<TranslatedExport> vertexExports;   // only populated for shaderType==0
    std::vector<TranslatedExport> pixelExports;    // only populated for shaderType==1
    uint32_t vertexStrideBytes = 0; // real per-vertex byte stride, only set for shaderType==0
};

// dwords are already host-byte-order (matching this project's established
// convention). shaderType: 0 = vertex, 1 = pixel.
//
// Recognizes exactly two real instruction patterns -- a vertex fetch
// with a recognized format (Float3/Float4), and an ALU export-via-
// self-mov (vector_opc==2/kMax, both sources the same real TEMP
// register, non-zero write mask). A write-mask-0 ALU instruction is a
// real no-op and is skipped, not a failure. Anything else (real
// arithmetic, a non-zero constant reference, an unrecognized fetch
// format, real control flow beyond simple sequential EXEC blocks) fails
// translation for the whole shader -- this is a narrow, honest
// translator for exactly the passthrough pattern this project has ever
// observed, not a general Xenos-to-MSL compiler.
TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType);
```

- [ ] **Step 4: Write `host/shader_translate.cpp`**

```cpp
#include "shader_translate.h"
#include <cstdio>

namespace
{
    struct FailedTranslation
    {
        TranslationResult result;
        FailedTranslation(const std::string& reason)
        {
            result.success = false;
            result.failureReason = reason;
        }
    };
}

TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType)
{
    TranslationResult result;
    result.success = true;

    // Same real EXEC-family-bound-shrinking walk 3a's own
    // DecodeShaderMicrocode uses (confirmed exact, including 3a's own
    // final-review fix for where the real control-flow program ends).
    uint32_t cfEndDword = (dwordCount / 3) * 3;
    for (uint32_t i = 0; i * 3 < cfEndDword; i++)
    {
        ControlFlowInstruction a, b;
        UnpackControlFlowPair(dwords[i * 3], dwords[i * 3 + 1], dwords[i * 3 + 2], a, b);
        ControlFlowInstruction pairInstrs[2] = { a, b };

        for (int which = 0; which < 2; which++)
        {
            const ControlFlowInstruction& cf = pairInstrs[which];
            if (IsExecFamily(cf.opcode))
            {
                uint32_t candidateEnd = cf.address * 3;
                if (candidateEnd < cfEndDword)
                {
                    cfEndDword = candidateEnd;
                }
            }

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
                    return FailedTranslation("control-flow address/count referenced a slot past the real microcode end").result;
                }

                uint32_t iw0 = dwords[slotDwordStart];
                uint32_t iw1 = dwords[slotDwordStart + 1];
                uint32_t iw2 = dwords[slotDwordStart + 2];
                bool isFetch = (cf.sequence >> (2 * j)) & 0x1;

                if (isFetch)
                {
                    uint32_t fetchOpcode = iw0 & 0x1F;
                    if (fetchOpcode != 0)
                    {
                        return FailedTranslation("a real texture fetch has no recognized translation").result;
                    }
                    VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(iw0, iw1, iw2);
                    TranslatedVertexFormat translatedFormat;
                    if (vf.format == 57) translatedFormat = TranslatedVertexFormat::Float3;
                    else if (vf.format == 38) translatedFormat = TranslatedVertexFormat::Float4;
                    else return FailedTranslation("unrecognized real vertex-fetch format").result;

                    TranslatedAttribute attr;
                    attr.fetchConstantIndex = vf.fetchConstantIndex;
                    attr.destReg = vf.destReg;
                    attr.format = translatedFormat;
                    attr.byteOffset = static_cast<uint32_t>(vf.offset) * 4;
                    result.attributes.push_back(attr);

                    if (!vf.isMiniFetch)
                    {
                        result.vertexStrideBytes = vf.stride * 4;
                    }
                }
                else
                {
                    AluInstructionFields alu = DecodeAluInstruction(iw0, iw1, iw2);
                    if (alu.vectorWriteMask == 0)
                    {
                        continue; // real no-op/filler, not a failure
                    }
                    bool isSelfMov = (alu.vectorOpcode == 2) && (alu.src1Reg == alu.src2Reg)
                        && alu.src1Sel && alu.src2Sel;
                    if (!isSelfMov)
                    {
                        return FailedTranslation("a real ALU instruction outside the recognized export-via-self-mov pattern").result;
                    }

                    TranslatedExport exp;
                    exp.exportRegister = alu.vectorDest;
                    exp.sourceRegister = alu.src1Reg;
                    if (shaderType == 1)
                    {
                        result.pixelExports.push_back(exp);
                    }
                    else
                    {
                        result.vertexExports.push_back(exp);
                    }
                }
            }
        }
    }

    if (shaderType == 0)
    {
        if (result.attributes.empty() || result.vertexExports.empty())
        {
            return FailedTranslation("no real attributes or exports recognized").result;
        }

        char source[2048];
        std::string attributeFields;
        std::string assignments;
        for (size_t i = 0; i < result.attributes.size(); i++)
        {
            const char* mslType = (result.attributes[i].format == TranslatedVertexFormat::Float3) ? "float3" : "float4";
            char fieldLine[128];
            snprintf(fieldLine, sizeof(fieldLine), "    %s r%u [[attribute(%zu)]];\n", mslType, result.attributes[i].destReg, i);
            attributeFields += fieldLine;
        }
        for (const TranslatedExport& e : result.vertexExports)
        {
            char assignLine[128];
            if (e.exportRegister == 62)
            {
                snprintf(assignLine, sizeof(assignLine), "    out.position = float4(in.r%u.xyz, 1.0);\n    out.pointSize = 8.0;\n", e.sourceRegister);
            }
            else
            {
                snprintf(assignLine, sizeof(assignLine), "    out.interpolator%u = float4(in.r%u);\n", e.exportRegister, e.sourceRegister);
            }
            assignments += assignLine;
        }
        snprintf(source, sizeof(source),
            "struct VertexIn {\n%s};\n"
            "struct RasterizerData {\n    float4 position [[position]];\n    float pointSize [[point_size]];\n    float4 interpolator0;\n};\n"
            "vertex RasterizerData vertex_main(VertexIn in [[stage_in]]) {\n    RasterizerData out;\n%s    return out;\n}\n",
            attributeFields.c_str(), assignments.c_str());
        result.vertexShaderSource = source;
    }
    else
    {
        if (result.pixelExports.empty())
        {
            return FailedTranslation("no real pixel exports recognized").result;
        }
        result.fragmentShaderSource =
            "fragment float4 fragment_main(RasterizerData in [[stage_in]]) {\n"
            "    return in.interpolator0;\n"
            "}\n";
    }

    return result;
}
```

- [ ] **Step 5: Compile and run to verify the smoke test passes**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_translate_smoketest.cpp host/shader_translate.cpp host/shader_decode.cpp -o /tmp/shader_translate_smoketest && /tmp/shader_translate_smoketest`
Expected: `shader_translate_smoketest: PASS`, exit code 0.

- [ ] **Step 6: Delete the temporary smoke test, add to the build, rebuild the real project, verify regression, commit**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king
rm host/shader_translate_smoketest.cpp
```

Edit `CMakeLists.txt:28` — change:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/shader_decode.cpp host/game_overrides.cpp host/renderer_metal.mm)
```
to:
```cmake
add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/shader_decode.cpp host/shader_translate.cpp host/game_overrides.cpp host/renderer_metal.mm)
```

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/shader_translate.h host/shader_translate.cpp CMakeLists.txt
git commit -m "feat: add real shader translator (unused by GpuCommandTracer yet)

Live-verified via standalone smoke test against this project's own
real, confirmed vertex and pixel shader microcode -- both translate
successfully, recognizing the real fetch-to-attribute and
export-via-self-mov patterns this game's shaders actually use.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — unchanged, since `TranslateShader` is still unused by any real code path.

---

## Task 3: The translation cache (`host/shader_translate.h/.cpp`)

**Files:**
- Modify: `host/shader_translate.h` (add `ShaderTranslationCache` class)
- Modify: `host/shader_translate.cpp` (add its implementation)

**Interfaces:**
- Consumes: `TranslationResult` (Task 2).
- Produces: `class ShaderTranslationCache` with `void UpdateVertexShader(uint32_t hash, TranslationResult result)`, `void UpdatePixelShader(uint32_t hash, TranslationResult result)`, `uint32_t CurrentVertexShaderHash()`, `uint32_t CurrentPixelShaderHash()`, `TranslationResult CurrentVertexShader()`, `TranslationResult CurrentPixelShader()`; free function `uint32_t Fnv1aHash(const uint8_t* data, size_t len)`.

- [ ] **Step 1: Write a standalone smoke test**

Create `host/shader_translate_smoketest.cpp` (temporary, deleted in Step 4):

```cpp
#include "shader_translate.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    // Real, standard FNV-1a 32-bit hash: offset basis 2166136261,
    // prime 16777619. A few bytes of known input, computed by hand:
    // hash = 2166136261
    // byte 0x00: hash = (2166136261 ^ 0x00) * 16777619 (mod 2^32)
    uint8_t data[1] = { 0x00 };
    uint32_t expected = (2166136261u ^ 0x00u) * 16777619u;
    assert(Fnv1aHash(data, 1) == expected);

    // Different data must (in this simple case) produce a different hash.
    uint8_t data2[1] = { 0x01 };
    assert(Fnv1aHash(data2, 1) != Fnv1aHash(data, 1));

    ShaderTranslationCache cache;

    // No updates yet: hashes read as 0 (never-seen sentinel), results
    // read as a default (unsuccessful) TranslationResult.
    assert(cache.CurrentVertexShaderHash() == 0);
    assert(cache.CurrentVertexShader().success == false);

    TranslationResult goodVs;
    goodVs.success = true;
    goodVs.vertexShaderSource = "real vs source";
    cache.UpdateVertexShader(42, goodVs);
    assert(cache.CurrentVertexShaderHash() == 42);
    assert(cache.CurrentVertexShader().success == true);
    assert(cache.CurrentVertexShader().vertexShaderSource == "real vs source");

    // A later failed update still overwrites -- the cache does not try
    // to preserve a prior success (that's the renderer's own job).
    TranslationResult badVs;
    badVs.success = false;
    badVs.failureReason = "real failure";
    cache.UpdateVertexShader(43, badVs);
    assert(cache.CurrentVertexShaderHash() == 43);
    assert(cache.CurrentVertexShader().success == false);

    // Vertex and pixel are tracked independently.
    TranslationResult goodPs;
    goodPs.success = true;
    cache.UpdatePixelShader(99, goodPs);
    assert(cache.CurrentPixelShaderHash() == 99);
    assert(cache.CurrentPixelShader().success == true);
    assert(cache.CurrentVertexShaderHash() == 43); // unaffected

    printf("shader_translate_smoketest (cache): PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile to verify it fails (the cache/hash don't exist yet)**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_translate_smoketest.cpp host/shader_translate.cpp host/shader_decode.cpp -o /tmp/shader_translate_smoketest2 2>&1`
Expected: FAIL — `'Fnv1aHash' was not declared` (or equivalent; `ShaderTranslationCache` doesn't exist yet).

- [ ] **Step 3: Add `Fnv1aHash` and `ShaderTranslationCache` to `host/shader_translate.h` and `.cpp`**

Add to `host/shader_translate.h`, after the existing includes:
```cpp
#include <mutex>
```

Add after `TranslateShader`'s declaration:
```cpp
// Real, standard FNV-1a 32-bit hash (offset basis 2166136261, prime
// 16777619) over raw bytes -- used to detect when this project's real
// repeated shader reloads (confirmed: identical microcode every frame)
// describe unchanged content, so translation/compilation isn't redone
// every single frame.
uint32_t Fnv1aHash(const uint8_t* data, size_t len);

// Carries the latest translation ATTEMPT (success or not) from the GPU
// pump thread (where PM4_IM_LOAD_IMMEDIATE is parsed) to the Metal
// render thread. This cache never tries to preserve a prior success
// internally -- a failed UpdateX overwrites the previous (possibly
// successful) result. The renderer owns its own separate memory of the
// last successfully COMPILED Metal pipeline, and decides for itself
// whether to replace it based on what CurrentVertexShader/
// CurrentPixelShader return.
class ShaderTranslationCache
{
public:
    void UpdateVertexShader(uint32_t hash, TranslationResult result);
    void UpdatePixelShader(uint32_t hash, TranslationResult result);
    uint32_t CurrentVertexShaderHash();
    uint32_t CurrentPixelShaderHash();
    TranslationResult CurrentVertexShader();
    TranslationResult CurrentPixelShader();

private:
    std::mutex vertexMutex_;
    uint32_t vertexHash_ = 0;
    TranslationResult vertexResult_;

    std::mutex pixelMutex_;
    uint32_t pixelHash_ = 0;
    TranslationResult pixelResult_;
};
```

Add to `host/shader_translate.cpp`:
```cpp
uint32_t Fnv1aHash(const uint8_t* data, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void ShaderTranslationCache::UpdateVertexShader(uint32_t hash, TranslationResult result)
{
    std::lock_guard<std::mutex> lock(vertexMutex_);
    vertexHash_ = hash;
    vertexResult_ = std::move(result);
}

void ShaderTranslationCache::UpdatePixelShader(uint32_t hash, TranslationResult result)
{
    std::lock_guard<std::mutex> lock(pixelMutex_);
    pixelHash_ = hash;
    pixelResult_ = std::move(result);
}

uint32_t ShaderTranslationCache::CurrentVertexShaderHash()
{
    std::lock_guard<std::mutex> lock(vertexMutex_);
    return vertexHash_;
}

uint32_t ShaderTranslationCache::CurrentPixelShaderHash()
{
    std::lock_guard<std::mutex> lock(pixelMutex_);
    return pixelHash_;
}

TranslationResult ShaderTranslationCache::CurrentVertexShader()
{
    std::lock_guard<std::mutex> lock(vertexMutex_);
    return vertexResult_;
}

TranslationResult ShaderTranslationCache::CurrentPixelShader()
{
    std::lock_guard<std::mutex> lock(pixelMutex_);
    return pixelResult_;
}
```

- [ ] **Step 4: Compile and run to verify the smoke test passes, then delete it**

Run: `cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && c++ -std=c++17 -I host host/shader_translate_smoketest.cpp host/shader_translate.cpp host/shader_decode.cpp -o /tmp/shader_translate_smoketest2 && /tmp/shader_translate_smoketest2`
Expected: `shader_translate_smoketest (cache): PASS`, exit code 0.

```bash
rm host/shader_translate_smoketest.cpp
```

- [ ] **Step 5: Rebuild the real project, verify regression, commit**

```bash
cd build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log gpu_trace.log
git add host/shader_translate.h host/shader_translate.cpp
git commit -m "feat: add ShaderTranslationCache (unused by GpuCommandTracer yet)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```
Expected: clean build, exit code 2, `153`, same tail message — unchanged.

---

## Task 4: Wire translation into `GpuCommandTracer` and fix the vertex stride (`host/gpu_trace.h/.cpp`)

**Files:**
- Modify: `host/gpu_trace.h` (add `#include "shader_translate.h"`, add member + accessor)
- Modify: `host/gpu_trace.cpp` (extend `PM4_IM_LOAD_IMMEDIATE` handling; fix the `PM4_DRAW_INDX_2` vertex-count calculation)

**Interfaces:**
- Consumes: `ShaderTranslationCache`, `TranslateShader`, `Fnv1aHash`, `TranslationResult` (Tasks 2-3).
- Produces: `ShaderTranslationCache& GpuCommandTracer::ShaderTranslation()`.

- [ ] **Step 1: Add the include, member, and accessor**

In `host/gpu_trace.h`, add after `#include "gpu_draw_list.h"`:
```cpp
#include "shader_translate.h"
```

Add to the `public:` section (after `FrameDrawList& DrawList() { return frameDrawList_; }`):
```cpp
    ShaderTranslationCache& ShaderTranslation() { return shaderTranslationCache_; }
```

Add to the `private:` section (after `FrameDrawList frameDrawList_;`):
```cpp
    ShaderTranslationCache shaderTranslationCache_;
```

- [ ] **Step 2: Hash and translate inside the existing `PM4_IM_LOAD_IMMEDIATE` handling**

In `host/gpu_trace.cpp`, the existing code (inside the `else { ... }` branch that builds `microcodeDwords` and calls `DecodeShaderMicrocode`) currently ends with the disassembly-logging loop. Add immediately after that loop, still inside the same `else` block:

```cpp
                    uint32_t microcodeHash = Fnv1aHash(
                        reinterpret_cast<const uint8_t*>(microcodeDwords.data()),
                        microcodeDwords.size() * sizeof(uint32_t));
                    uint32_t cachedHash = (shaderTypeValue == 0)
                        ? shaderTranslationCache_.CurrentVertexShaderHash()
                        : shaderTranslationCache_.CurrentPixelShaderHash();
                    if (microcodeHash != cachedHash)
                    {
                        TranslationResult translation = TranslateShader(microcodeDwords.data(), sizeDwords, (int)shaderTypeValue);
                        if (logFile_)
                        {
                            fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: translation %s%s\n",
                                indent, translation.success ? "succeeded" : "FAILED",
                                translation.success ? "" : (" (" + translation.failureReason + ")").c_str());
                        }
                        if (shaderTypeValue == 0)
                        {
                            shaderTranslationCache_.UpdateVertexShader(microcodeHash, translation);
                        }
                        else
                        {
                            shaderTranslationCache_.UpdatePixelShader(microcodeHash, translation);
                        }
                    }
```

- [ ] **Step 3: Use the real translated stride for vertex-count calculation when available**

In `host/gpu_trace.cpp`, find the existing line inside the `PM4_DRAW_INDX_2` handling:
```cpp
                        uint32_t bufferVertexCapacity = vertexByteSize / 12;
```
Replace it with:
```cpp
                        // Use the real translated vertex shader's own
                        // decoded stride once translation has succeeded
                        // -- the hardcoded 12 (float3-only) was always a
                        // documented placeholder assumption (sub-project
                        // 2's own spec), and this project's real vertex
                        // shader actually interleaves a second, float4
                        // attribute at stride 28, not 12.
                        TranslationResult currentVs = shaderTranslationCache_.CurrentVertexShader();
                        uint32_t realStride = (currentVs.success && currentVs.vertexStrideBytes != 0)
                            ? currentVs.vertexStrideBytes : 12;
                        uint32_t bufferVertexCapacity = vertexByteSize / realStride;
```

- [ ] **Step 4: Add `#include <mutex>` check and build and run live**

`host/gpu_trace.cpp` doesn't need a new direct include for this step (`shader_translate.h`, transitively included via `gpu_trace.h`, already brings in `<mutex>` and everything else used here).

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king/build && cmake --build . --target BigBumpinHost -j 8
cd .. && ./build/BigBumpinHost > run.log 2>&1
echo "exit: $?"
grep -c NtReadFile run.log
tail -3 run.log
rm -f run.log
./build/BigBumpinHost --window > run2.log 2>&1
echo "exit: $?"
grep "translation succeeded\|translation FAILED" gpu_trace.log | sort -u
grep "DRAW_INDX_2: primType=1" gpu_trace.log | head -3
rm -f run2.log gpu_trace.log
```
Expected: headless run unchanged (exit 2, `153`, same tail message). `--window` run exits 2 (unchanged). Both real shader loads should show `translation succeeded` (not `FAILED` — if either fails, read the logged `failureReason` and compare against this project's own real confirmed shader data in this plan's Global Constraints before assuming the code is wrong; the data here was re-verified fresh immediately before this plan was written). The `DRAW_INDX_2` lines should now show `vertexCount=1` still (numIndices=1, unchanged from sub-project 2's own already-correct fix — the stride change affects `bufferVertexCapacity`, the safety cap, not the real draw count when `numIndices` is smaller than it).

- [ ] **Step 5: Commit**

```bash
git add host/gpu_trace.h host/gpu_trace.cpp
git commit -m "feat: translate real shader microcode and use its real stride for vertex counts

Live-verified (--window run): both of this project's own real shader
loads now translate successfully (not falling back to a failure),
confirming the recognized fetch-to-attribute and export-via-self-mov
patterns match this project's real data end to end.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```

---

## Task 5: Real pipeline swap in the renderer (`host/renderer_metal.mm`)

**Files:**
- Modify: `host/renderer_metal.mm`

**Interfaces:**
- Consumes: `g_gpuTracer.ShaderTranslation().CurrentVertexShader()`/`CurrentPixelShader()` (Task 4), `TranslationResult`, `TranslatedAttribute`, `TranslatedVertexFormat` (Task 2).

- [ ] **Step 1: Add file-local state for the real compiled pipeline's own tracked hash**

In `host/renderer_metal.mm`, add to the existing first anonymous `namespace { ... }` block (alongside `g_drawPipelineState`):
```cpp
uint64_t g_lastCompiledRealShaderHashPair = 0; // 0 = nothing real compiled yet; combines vertex+pixel hashes
```

- [ ] **Step 2: Add a helper that attempts to build and swap in a real pipeline**

Add this function inside the existing first anonymous `namespace { ... }` block, after `StopApplication`:

```cpp
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
```

Also add `#include <string>` to the file's existing `#include <vector>` block, and add `#include "shader_translate.h"` after the existing `#include "gpu_trace.h"`.

- [ ] **Step 3: Call it once per tick from `drawInMTKView:`**

In `host/renderer_metal.mm`, inside `drawInMTKView:`, add right after the existing `[encoder setRenderPipelineState:g_drawPipelineState];` line is set up -- actually, the pipeline must be decided BEFORE that line, so add the call immediately before it instead:

Replace:
```cpp
    [encoder setRenderPipelineState:g_drawPipelineState];
```
with:
```cpp
    TryUpdateRealPipeline(self.commandQueue.device);
    [encoder setRenderPipelineState:g_drawPipelineState];
```

- [ ] **Step 4: Build and run live, verify headless regression and the real pipeline swap**

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
Expected: headless run unchanged (exit 2, `153`, same tail message). `--window` run exits 2 (unchanged), shows the existing `[renderer] event loop stopped -- drawn frames: N, present signals received: M` line with healthy counts, AND a new `[renderer] now using real translated shader (vsHash=... psHash=...)` line — confirming the renderer actually swapped away from the placeholder pipeline to the real compiled one. No `real shader compile failed`/`real pipeline state creation failed` lines.

- [ ] **Step 5: Commit**

```bash
git add host/renderer_metal.mm
git commit -m "feat: swap to a real compiled Metal pipeline when shader translation succeeds

Live-verified (--window run): the renderer now compiles and uses the
real translated vertex+pixel shader pair instead of the Milestone-1
placeholder, confirmed via a new log line reporting the real shader
hashes now in use.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KP35nmQ1NKuwuMtF9NnWA3"
```

---

## Task 6: Manual visual confirmation request

**Files:**
- None (verification-only task; no code changes).

- [ ] **Step 1: Run an extended `--window` session and capture real translation/pipeline statistics**

```bash
cd /Users/dylan/Documents/GitHub/big-burger-bumpin-king && ./build/BigBumpinHost --window > run.log 2>&1
echo "exit: $?"
grep "translation succeeded\|translation FAILED" gpu_trace.log | sort -u
grep "renderer\]" run.log
rm -f run.log gpu_trace.log
```
Expected: exit code 2 (unchanged watchdog-bound baseline); both real shader loads show `translation succeeded`; the `now using real translated shader` line appears exactly once (the hash pair doesn't change across frames, since this project's real shader content is identical every reload — confirms the caching gate is working, not recompiling every frame).

- [ ] **Step 2: Request manual visual confirmation**

This sandboxed environment has no attached display (established throughout every rendering sub-project this session) — ask the user to run `./build/BigBumpinHost --window` on their own machine and confirm: the rendered point is no longer the placeholder's hardcoded solid white — its color should now reflect whatever real value this game's actual second vertex attribute (the real float4 fetched via the mini-fetch) carries. Any color other than pure white is itself the real, positive confirmation that an actual game-authored shader — not a stand-in — is now driving what appears on screen, closing out the full rendering effort this session has built across sub-projects 1, 2, 3a, and 3b.
