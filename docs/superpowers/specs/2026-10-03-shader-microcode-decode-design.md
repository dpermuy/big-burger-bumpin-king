# Shader Microcode Decode — Design Spec

## Context

This is sub-project 3a — the first of two stages toward real shader
translation (sub-project 3, the final and hardest piece toward real PM4
draw-call rendering). Sub-projects 1 and 2 (merged, visually confirmed)
gave this project real GPU register tracking and a real Metal draw path,
but the draw path uses a **hardcoded placeholder shader** (passthrough
position, solid white) and an explicit, documented guess about vertex
format (fetch-constant slot 0, tightly-packed float3). Real shader
translation — parsing the game's actual compiled Xenos shader microcode
and generating real MSL from it — is confirmed, via direct inspection of
Xenia's own real shader-translation source, to be one of that project's
largest subsystems (its shared, target-agnostic parser alone is 1,506
lines; instruction-format definitions span 2,101 lines; two separate
~40-50-opcode ALU instruction enums; 16 distinct real control-flow
instruction types). Attempting this as one sub-project risks exactly the
open-ended scope creep this whole investigation has avoided everywhere
else.

Sub-project 3 is therefore split into two stages:

- **3a (this spec):** decode real shader microcode into an inspectable
  instruction list. No MSL generation, no rendering change.
- **3b (future, separate spec):** generate real MSL for a narrow, real,
  observed slice of what 3a decodes (sequential `EXEC` blocks, a small
  set of real ALU vector ops, no branching/looping/predication),
  replacing the placeholder shader for that case.

This project's real captured trace already confirms real shader-load
traffic exists to decode: 52 `PM4_IM_LOAD_IMMEDIATE` (opcode `0x2B`)
packets, alternating between two real payload sizes (29 and 11 total
dwords) — almost certainly a vertex+pixel shader pair, loaded repeatedly
alongside the real draw calls sub-project 2 already renders.

## Goals

- Parse `PM4_IM_LOAD_IMMEDIATE`'s real packet format (shader type,
  embedded microcode dwords) and byte-swap the embedded microcode,
  matching this project's own established big-endian-guest convention.
- Unpack the real control-flow instruction format: 3 raw dwords encode 2
  logical control-flow instructions (confirmed via Xenia's own
  `UnpackControlFlowInstructions`), each a tagged union over the real 16
  `ControlFlowOpcode` values.
- For each of the 8 real `EXEC`-family control-flow opcodes (`kExec`,
  `kExecEnd`, `kCondExec`, `kCondExecEnd`, `kCondExecPred`,
  `kCondExecPredEnd`, `kCondExecPredClean`, `kCondExecPredCleanEnd`),
  decode the ALU/fetch instructions its `address`/`count` fields point to
  — confirmed to be a direct slot index into the same flat, 3-dwords-per-
  slot microcode array (no graph-walking or branch simulation required
  for this).
- Decode real `AluInstruction` fields (vector/scalar opcode, destination
  register + write mask, 3 source register indices) and real
  `VertexFetchInstruction` fields (opcode, **real fetch-constant slot** —
  `const_index * 3 + const_index_sel`, replacing sub-project 2's
  hardcoded "always slot 0" guess — plus format, stride, swizzles).
- Produce a real, human-readable disassembly-style log of the decoded
  program, live-verified against this project's own real captured shader
  loads.

## Non-goals

- No MSL generation (sub-project 3b).
- No execution/simulation of control flow — `kLoopStart`/`kLoopEnd`,
  `kCondCall`/`kReturn`, `kCondJmp` get their own raw fields decoded and
  logged, but their branching/looping *semantics* are not resolved. 3a
  disassembles a static instruction stream; it does not run one.
- No `TextureFetchInstruction` decode beyond recognizing the opcode (no
  texture sampling exists yet in this project's rendering path at all).
- No change to rendering, `GpuRegisterState`, or `FrameDrawList` — this
  sub-project produces zero visual change, matching sub-project 1's own
  precedent.
- No `PM4_IM_LOAD` (opcode `0x27`, pointer-based shader load) handling —
  confirmed zero real occurrences in this project's own captured trace;
  only the embedded-immediate variant (`0x2B`) is observed.

## Architecture

**New files:** `host/shader_decode.h/.cpp` — plain data structures and
decode logic, no Metal or PM4-parsing dependency, matching this project's
established file-separation discipline. Real bit-layout structs (written
independently from the verified real encoding, cross-checked against
Xenia's own `ucode.h` as ground truth — Xenia's license is a permissive
BSD variant, and these structs describe real, factual hardware encoding
rather than creative content either way):

- `ControlFlowInstruction` — tagged union over the 16 real
  `ControlFlowOpcode` values, with the real 3-dwords-to-2-instructions
  unpack function.
- `AluInstruction` — real 3-dword layout: word 0 (`vector_dest`,
  `scalar_dest`, write masks, `scalar_opc`), word 1 (swizzles, negate
  flags, predication), word 2 (source registers, `vector_opc`, source
  select flags) — all bit positions confirmed against Xenia's real
  `AluInstruction::Data` layout.
- `VertexFetchInstruction` — real 3-dword layout confirmed against
  Xenia's real `VertexFetchInstruction::Data`, including the real
  fetch-constant-slot formula.

One function: `DecodedShaderProgram DecodeShaderMicrocode(const
uint32_t* dwords, uint32_t dwordCount, ShaderType type)` — `dwords` are
already host-byte-order (byte-swapped by the caller before this function
sees them, matching this project's established convention of doing
endian conversion at the point of reading guest memory, not inside
decode logic). Unpacks every control-flow pair the dword count allows
(matching Xenia's own `ucode_data_.size() / 3` bound), then for each
`EXEC`-family instruction found, decodes the ALU/fetch instructions at
its `address`/`count` slot range.

## Integration

`host/gpu_trace.cpp`'s `type == 0x3` branch gets a new case for
`kOpcodeImLoadImmediate = 0x2B` (currently falls through to the generic,
unnamed `TYPE3 opcode=0x2B` log line). Real packet format (confirmed
against Xenia's `ExecutePacketType3_IM_LOAD_IMMEDIATE`): payload dword 0
= shader type (`0` = vertex, `1` = pixel), payload dword 1 = `start_size`
(bits 16-31 = start, expected `0`; bits 0-15 = `size_dwords`), followed
by `size_dwords` raw big-endian microcode dwords embedded directly in
the packet. Read and byte-swap each microcode dword via `LoadU32` (the
same helper already used for every other PM4 dword in this file), call
`DecodeShaderMicrocode`, and log the decoded program.

No new member is added to `GpuCommandTracer` for this sub-project — the
decoded program is logged, not stored, since nothing downstream consumes
it yet (that's 3b's job). A future 3b would add storage at that point.

## Error handling

- `start != 0`: log and skip the packet (Xenia itself asserts this is
  always true for real traffic; a non-zero `start` would mean a partial
  microcode update this project doesn't yet support).
- `count - 2 < size_dwords` (packet too short for the claimed microcode
  length): log and skip, matching this project's established posture
  toward malformed-but-well-formed-looking packets elsewhere.
- An unrecognized `shader_type` value (anything but 0 or 1): log and
  skip.
- A control-flow `address`/`count` range that extends past the real
  dword count: clamp to what's actually present and log a warning,
  rather than reading out of bounds.

## Testing

1. **Headless regression unchanged** — this sub-project only adds
   logging inside an already-unconditionally-executing code path
   (`ScanBuffer`'s `TYPE3` dispatch), but produces no behavior change
   observable outside `gpu_trace.log`; the established headless baseline
   (153 `NtReadFile` lines, same watchdog-timeout tail message, same
   exit code) must stay byte-for-byte identical.
2. **Live `--window` run, structural verification** — inspect the real
   decoded output for both observed real shader loads (29 and 11 total
   dwords). Confirm: real `ControlFlowOpcode` values appear (not garbage
   enum values), at least one `EXEC`-family instruction's `address` +
   `count` land inside the real dword range, decoded `AluInstruction`
   vector/scalar opcodes are valid enum values, and if a
   `VertexFetchInstruction` is present, its decoded fetch-constant slot
   is a plausible value (0-95) — ideally cross-checked against slot 0 if
   it matches sub-project 2's own already-live-verified placeholder
   assumption, which would be a strong, independent confirmation that
   both the register-tracking (sub-project 1) and this new microcode
   decode agree on the real vertex buffer's location.
3. No rendering-path testing is needed — this sub-project touches no
   code on that path, and produces no visual change.
