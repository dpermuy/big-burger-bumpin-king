# GPU Register State Tracking — Design Spec

## Context

This is sub-project 1 of 3 toward real PM4 draw-call rendering (the next
milestone after the Metal renderer Milestone 1, which added a real
window/device/swapchain with a fixed clear-color present and no real
draw content). The three sub-projects, each its own spec/plan/implementation
cycle:

1. **GPU register state tracking** (this spec) — track real `TYPE0`/`SET_CONSTANT`
   register writes into a real, inspectable state object. No rendering.
2. **Untextured geometry draw path** — resolve real vertex/index buffers using
   the state tracked here, execute `PM4_DRAW_INDX_2`, render with a hardcoded
   placeholder Metal shader (passthrough position transform, solid fill).
3. **Real shader translation** — Xenos microcode → MSL, replacing the
   placeholder shader from sub-project 2.

`host/gpu_trace.cpp`'s `GpuCommandTracer::ScanBuffer` currently parses the
real PM4 command stream and already real-executes two TYPE3 opcodes
(`PM4_EVENT_WRITE_SHD`, `PM4_INTERRUPT`) for synchronization (Findings 35-37
of the prior loading-screen investigation). It also already recognizes
`TYPE0` register-write packets, but only logs their register index and
count — it never reads the payload dwords or stores them anywhere. This
spec adds that missing storage step.

## Goals

- Track real GPU register writes (`TYPE0` packets and the `PM4_SET_CONSTANT`
  TYPE3 opcode, `0x2D`) into a real, queryable register-state object.
- Decode the specific subset of registers sub-project 2 will need first:
  vertex fetch constants (type/address/endian/size) and the draw-initiator
  fields (primitive type, source select, index format, index count).
- Store every other real register write too, undecoded, so later milestones
  don't need to re-plumb storage to add more decoded fields.
- Live-verify the tracked values are real and correct by cross-checking
  against values already independently confirmed in the prior investigation
  (known-valid guest address ranges, the known real `0x2000-0x2312` TYPE0
  range).

## Non-goals

- No draw execution (`PM4_DRAW_INDX_2` stays unhandled by this sub-project).
- No vertex/index buffer memory access — this only decodes register
  *metadata* (addresses, sizes), it does not read guest memory at those
  addresses.
- No shader parsing or translation.
- No change to existing opcode handling (`EVENT_WRITE_SHD`, `INTERRUPT`,
  `INDIRECT_BUFFER`) beyond adding the new `SET_CONSTANT` case alongside them.

## Architecture

**New files:** `host/gpu_state.h`, `host/gpu_state.cpp` — a single-purpose
class, `GpuRegisterState`, with no dependency on `GpuCommandTracer` or PM4
parsing. It is a plain register bank: given an index and a value, it stores
it; given an index, it returns the last stored value. Decoded accessors sit
on top of that same storage and are just bit-layout views onto it — adding
a new decoded register later means adding one more small accessor method,
never touching storage.

**Storage:** `uint32_t regs_[kRegisterCount]` where `kRegisterCount =
0x5000` (20480 entries, 80KB) — large enough to cover every real sub-bank
confirmed in Xenia's own source: `REGISTERS` (0x2000+), `ALU` (0x4000+),
`FETCH` (0x4800+, 96 slots × 2 dwords = 192 dwords), `BOOL` (0x4900+),
`LOOP` (0x4908+), with margin. A plain flat array, not a map — this matches
Xenia's own real approach and keeps writes O(1) with no allocation.

**Integration point:** `GpuCommandTracer` gets one new member, a
`GpuRegisterState gpuState_`, and one new read-only accessor,
`const GpuRegisterState& RegisterState() const`. `ScanBuffer`
(`host/gpu_trace.cpp:184-197`, the `type == 0x0` / TYPE0 branch) is extended
to read each of the `count` payload dwords and call
`gpuState_.WriteRegister(baseIndex + i, value)` — currently it computes
`payloadBytes` and skips over them without reading. A new TYPE3 case for
`PM4_SET_CONSTANT` (`0x2D`, currently unhandled — falls through to the
default "didn't decode above" path) is added alongside the existing
`EVENT_WRITE_SHD`/`INTERRUPT` cases, decoding the real sub-bank offset
formula (confirmed from Xenia's `ExecutePacketType3_SET_CONSTANT`: offset
dword's low 11 bits are the index, bits 16-23 select ALU(+0x4000)/
FETCH(+0x4800)/BOOL(+0x4900)/LOOP(+0x4908)/REGISTERS(+0x2000)) and calling
the same `WriteRegister` for each remaining payload dword.

## Decoded accessors

Two decoded views, matching real Xenia bit layouts exactly (cross-checked
against `xenia/gpu/registers.h` and `xenia/gpu/xenos.h`):

```cpp
struct VertexFetchConstant {
    uint32_t type;      // 2 bits: 0=invalid tex, 1=invalid vertex, 2=texture, 3=vertex
    uint32_t address;   // 30 bits, in dwords (real byte address = address << 2)
    uint32_t endian;    // 2 bits
    uint32_t size;      // 24 bits, in 32-bit words
};
VertexFetchConstant GetVertexFetchConstant(uint32_t slot) const; // slot 0-95, bounds-checked

struct DrawInitiator {
    uint32_t primType;      // 6 bits
    uint32_t sourceSelect;  // 2 bits: 0=DMA(indexed), 1=immediate(unsupported), 2=auto-index
    uint32_t indexSize;     // 1 bit: 0=16-bit, 1=32-bit
    uint32_t numIndices;    // 16 bits
};
DrawInitiator GetDrawInitiator() const; // reads real register 0x21FC (VGT_DRAW_INITIATOR)
```

`kVertexFetchConstantBase = 0x4800` (real, confirmed); each slot occupies
2 consecutive dwords. `kDrawInitiatorRegister = 0x21FC` (real, confirmed —
falls inside the already-observed real `0x2000-0x2312` TYPE0 range from
Finding 35, consistent with real hardware mirroring `VGT_DRAW_INITIATOR`
into plain register state even though `PM4_DRAW_INDX_2`'s own packet
payload is the authoritative source when that opcode actually runs).

## Data flow

1. `ScanBuffer` sees a `TYPE0` packet: reads `baseIndex`/`count` as today,
   then reads each payload dword and calls
   `gpuState_.WriteRegister(baseIndex + i, value)`.
2. `ScanBuffer` sees a `TYPE3` `PM4_SET_CONSTANT` (`0x2D`) packet: decodes
   the sub-bank offset from the first payload dword, then calls
   `WriteRegister` for each remaining dword at the resolved absolute index.
3. `GpuRegisterState::WriteRegister` bounds-checks the index and stores the
   value (see Error handling).
4. Later code (sub-project 2 onward, or this sub-project's own live
   verification logging) calls `GetVertexFetchConstant`/`GetDrawInitiator`
   to read back decoded views.

## Error handling

`WriteRegister`/`ReadRegister` bounds-check the index against
`kRegisterCount`. An out-of-range index is logged once (rate-limited, not
per-call) and ignored rather than crashing or growing the array — a game
writing to a real register this project hasn't allocated margin for is a
real, informative signal (matches this project's established posture:
don't guess at unparsed data, surface it instead), not a reason to corrupt
memory or abort a build whose whole value is tolerating the unexpected.

## Testing

1. **Headless regression unchanged.** `GpuRegisterState` writes are purely
   additive bookkeeping alongside existing control flow — the default
   headless run must still show the exact byte-for-byte-unchanged baseline
   (153 `NtReadFile` lines, same watchdog-timeout tail message, same exit
   code) already established and re-verified through every step of
   Milestone 1.
2. **Live decoded-value verification.** Add temporary logging (removed or
   reduced to a single confirmation line before considering the sub-project
   done, per this project's established TEMP-instrumentation discipline)
   that prints `GetDrawInitiator()` whenever register `0x21FC` is written,
   and `GetVertexFetchConstant(0)` whenever slot 0's registers are written.
   Run live, inspect `gpu_trace.log`, and confirm the decoded values are
   plausible: `primType` within the real enum's valid range (0-21),
   `sourceSelect` one of the three valid values, `numIndices` non-zero,
   and the vertex fetch constant's `address` (converted to a byte address
   and OR'd with the `0xA0000000` segment, matching this project's own
   established guest physical-address convention) falling inside a range
   already known to be valid guest memory from the prior investigation.
3. **Out-of-range write path.** Confirm (by inspection or by observing a
   real out-of-range write in a live run, if one occurs) that the bounds
   check logs and continues rather than crashing.

No `--window` or rendering-path testing is needed for this sub-project —
it touches no code on that path.
