# Real Shader Translation — Design Spec

## Context

This is sub-project 3b — the final sub-project of the whole multi-stage
rendering effort, following sub-project 1 (real GPU register tracking,
merged), sub-project 2 (real `PM4_DRAW_INDX_2` draw submission with a
hardcoded placeholder shader, merged, visually confirmed — a real white
point renders), and sub-project 3a (a real Xenos shader microcode
decoder, merged, live-verified against this project's own real captured
shader pair).

3a's decoded output, cross-checked against Xenia's real `ExportRegister`
enum (`kVSInterpolator0=0`, `kVSPosition=62`, `kPSColor0=0`, all
confirmed exact), fully resolves this project's one real observed shader
pair:

- **Real vertex shader:** fetches a real `float3` position (fetch-constant
  slot 0, stride 7 dwords) into `r1`; fetches a real `float4` via a
  mini-fetch (inheriting the full fetch's stride) into `r0`; one real ALU
  instruction exports `r0` unchanged to `kVSInterpolator0`; one real ALU
  instruction exports `r1` unchanged to `kVSPosition`. One more ALU
  instruction is a real no-op (write mask `0x0`, discarded).
- **Real pixel shader:** one real ALU instruction exports `r0` unchanged
  to `kPSColor0`.

Both real export instructions use the same real pattern: `vector_opc=2`
(`kMax`) with identical source operands (`src1==src2`, both real `TEMP`
registers — confirmed via a one-off research check of the `src_sel`
bits), `scalar_opc=50` (`kRetainPrev`) — the standard real idiom a shader
compiler uses to synthesize a plain `MOV` when the ISA has no dedicated
move opcode. **Every real source operand in this shader pair is either a
`TEMP` register (self-mov) or the constant register `c0` in the one
no-op instruction (which writes nothing).** There is no real arithmetic,
no real camera/projection transform, no real branching, and no real
constant-register usage anywhere in the one shader this project has ever
observed.

## Goals

- Decode the real ALU swizzle fields (component-relative rotational
  encoding) and real vertex-fetch destination swizzle fields (absolute
  per-component encoding) — both explicitly deferred by 3a.
- Recognize exactly two real instruction patterns and generate real MSL
  from them: a real vertex fetch → a real vertex attribute binding; the
  real "export via self-mov" idiom → a real register-to-output wiring.
- Replace the hardcoded placeholder `MTLRenderPipelineState` (sub-project
  2) with one built from real generated MSL, for shaders that translate
  successfully.
- Cache translated/compiled shaders by a hash of their raw microcode —
  this project's real shader reloads are byte-identical every frame, and
  Metal pipeline-state creation is too expensive to redo every frame for
  content that hasn't changed.

## Non-goals

- **No general Xenos ALU-opcode-to-MSL compiler.** Any instruction
  outside the two recognized patterns (real arithmetic, a real non-mov
  `AluVectorOpcode`, a real non-zero constant reference, real branching
  beyond simple sequential `EXEC` blocks) fails translation for that
  shader — this sub-project does not attempt to handle it.
- **No texture sampling.** This project's real shaders have no real
  texture fetches; 3a already stops at recognizing (not decoding) a real
  `TFETCH`, and this sub-project doesn't change that.
- No change to sub-project 2's draw-list/draw-submission logic, or to
  the headless regression path.

## Architecture

**Extend `host/shader_decode.h/.cpp`** (3a's own files, not a new file —
swizzle decoding is a natural extension of the instruction-field decode
3a already owns): add `src1Swizzle`/`src2Swizzle`/`src3Swizzle` (raw
8-bit values) to `AluInstructionFields`, with a helper
`uint32_t ResolveAluSwizzleComponent(uint32_t rawSwizzle, uint32_t destComponent)`
implementing the real component-relative formula (confirmed exact against
Xenia's `GetSwizzledComponentIndex`:
`((rawSwizzle >> (2 * destComponent)) + destComponent) & 3`, yielding
which source component — 0=x,1=y,2=z,3=w — feeds a given destination
component). Add `destSwizzle` (raw 12-bit value) to
`VertexFetchInstructionFields`, with a helper
`FetchDestinationSwizzle GetFetchSwizzleComponent(uint32_t rawSwizzle, uint32_t component)`
implementing the real absolute per-component formula
(`(rawSwizzle >> (3 * component)) & 0b111`, where 0-3 select source
X/Y/Z/W, 4/5 are constant 0/1, 7 means "keep current value").

**New files: `host/shader_translate.h/.cpp`** — the real translator.
Does **not** parse 3a's disassembly text; it walks the raw microcode
directly using 3a's existing low-level functions (`UnpackControlFlowPair`,
`DecodeAluInstruction`, `DecodeVertexFetchInstruction`), the same
EXEC-family-block walk 3a's own `DecodeShaderMicrocode` already performs
(with 3a's own final-review fix for where the real control-flow program
ends). For each instruction found:

- A real vertex fetch (`fetchOpcode == 0`) records a real attribute
  binding: fetch-constant slot, real `MTLVertexFormat` (mapped from the
  real decoded `format` value), destination register, destination
  swizzle.
- A real ALU instruction matching the export-via-self-mov pattern
  (`vectorDest` is a real export register per `ExportRegister`,
  `src1Reg == src2Reg`, both real `TEMP` registers, `vector_opc == 2`
  /*kMax*/, write mask non-zero) records a real register-to-export
  wiring with its real resolved swizzle.
- Anything else (a real write mask of `0`, meaning a genuine no-op, is
  skipped — not a failure; anything with actual effect that isn't one of
  the two recognized patterns) marks the whole shader as **unsupported**.

One entry point: `TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType)`
— `TranslationResult` holds either real generated MSL source text (a
`vertex_main`/`fragment_main` pair wired per the recognized real
bindings) or a failure flag with a human-readable reason (logged, not
silently dropped).

**Cross-thread handoff:** a new small struct, `ShaderTranslationCache`,
owns the latest successfully-translated real VS+PS MSL source pair plus
a hash of the raw microcode it came from, guarded by its own mutex —
same double-buffer-adjacent pattern as sub-project 2's `FrameDrawList`,
but simpler (one current value, not a per-frame accumulating list, since
a shader stays loaded across many frames). `GpuCommandTracer` gets a
member of this type. The `PM4_IM_LOAD_IMMEDIATE` handling in
`gpu_trace.cpp` (pump thread) hashes the raw microcode bytes; if the hash
differs from what's already cached, it calls `TranslateShader` and
updates the cache only on success — on failure, the cache is left
untouched (keeps showing whatever last successfully compiled, or the
original placeholder if nothing ever has).

**Renderer integration:** `renderer_metal.mm`'s `drawInMTKView:` (render
thread) checks the cache once per tick; if its hash differs from the
currently-built `MTLRenderPipelineState`'s own tracked hash, it compiles
the new real MSL into a new pipeline state and swaps it in, same
`newLibraryWithSource:`/`newRenderPipelineStateWithDescriptor:` pattern
sub-project 2 already uses for the placeholder — this sub-project adds a
second, real pipeline state that takes over when available, falling back
to the original placeholder pipeline state if the cache has never
produced anything.

## Error handling

- Any instruction outside the two recognized patterns: log the specific
  reason (opcode, register, or field that didn't match) and mark
  translation as failed for that shader — the existing placeholder stays
  in use, this is never a hard failure or a crash.
- A real vertex-fetch `format` value with no real `MTLVertexFormat`
  equivalent: translation fails for that shader (same graceful fallback).
- Metal shader compilation failure (`newLibraryWithSource:` returning an
  error) on real generated MSL: log the real compiler error, keep the
  previously-working pipeline state (or the placeholder), never crash the
  render loop — matches this project's established posture (Milestone
  1's own `newLibraryWithSource:` error handling for the placeholder
  shader itself).

## Testing

1. **Headless regression unchanged** — no behavior change outside
   `gpu_trace.log` and the `--window` rendering path.
2. **Live `--window` run, structural verification** — confirm the real
   shader pair actually translates successfully this time (not falling
   back to the placeholder): log whether `TranslateShader` succeeded for
   each real shader load, and if it failed, log why (a reasonable,
   diagnosable failure reason, not a silent skip).
3. **Visual confirmation (manual, by the user)** — same established
   pattern as every prior rendering sub-project (no display access in
   this sandboxed environment). The real payoff this sub-project can
   actually show: ask the user to confirm the rendered point's color
   changed from the placeholder's hardcoded white to whatever the real
   vertex data's second attribute carries — the first real, end-to-end
   confirmation that an actual game-authored shader (not a stand-in) now
   drives this project's rendering.
