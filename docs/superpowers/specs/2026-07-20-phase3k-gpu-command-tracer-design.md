# Phase 3K: Real GPU Command-Buffer Plumbing + PM4 Tracer

## Context

Phase 3H–3J (merged to master) fixed the real bug behind the original "physical memory
allocation stall": missing APC delivery, missing spinlocks, and a stubbed `sprintf`. With
those fixed, the game now runs its real per-frame update loop continuously and indefinitely
(verified: 45–60s runs with a genuinely evolving call stack, not frozen or busy-polling).

The loop never produces visible output because `VdSwap` (the frame-present call) and the
rest of the `Vd*` display/video family were implemented as no-ops back in Phase 2J,
deliberately deferring "the real, large Phase 3 work" until the game was confirmed to reach
a stable state without it. That confirmation has now happened.

Reading the game's real call sites for the ring-buffer setup functions
(`private/ppc/ppc_recomp.4.cpp:14830-14990`, `ppc_recomp.6.cpp:24580-24630`) found:

- `VdInitializeRingBuffer(r3=physAddr, r4=sizeLog2-3)` — `r3` is the result of
  `MmGetPhysicalAddress` on a buffer the *game itself* allocated via
  `MmAllocatePhysicalMemoryEx` (already real in this project). `MmGetPhysicalAddress` is
  already an identity mapping (Phase 2J), so `r3` is directly a valid, already-allocated
  guest address in our existing mmap'd memory. The kernel doesn't allocate the ring buffer —
  it's told where one already is. The call's return value is never read by the caller.
- `VdEnableRingBufferRPtrWriteBack(r3=addr)` — `addr` is derived from the same buffer's base
  with address-aliasing bit tricks the game applies itself before the call; the value that
  arrives in `r3` is already a clean, low-29-bit guest address (no further masking needed on
  our end, verified against the exact bit ops in the call site).
- `VdSetSystemCommandBufferGpuIdentifierAddress(r3=addr-or-0)` — called twice: once with
  `r3=0` (startup reset) and once with a real address (offset+8 into the same buffer struct
  `VdInitializeRingBuffer` used). Looks like a fence/identifier slot, not the ring buffer
  itself.
- `VdGetSystemCommandBuffer(r3=&addrOut, r4=&sizeOut)` — writes an address and size back to
  two caller-stack out-pointers; the caller reads both back immediately after. A second,
  separate, smaller kernel-owned buffer, not the game's main command stream.

No kernel-imported function anywhere in `host/kernel_imports.txt` corresponds to a real
hardware "write pointer" register (`CP_RB_WPTR` in real Xenos terms) — on real hardware
that's a raw MMIO register write, which this project's flat mmap'd guest memory model has no
way to trap. We cannot observe "the CPU just told the GPU commands are ready up to offset X"
as a discrete event.

## Goal

Get real Xenos PM4 command data flowing into a buffer we control, and produce a
human-readable trace of what's actually in it — packet types, opcodes, sizes — without
attempting any real rendering. This is evidence-gathering for the renderer design that comes
after, matching how every prior phase in this project worked: real behavior first, informed
by what's actually observed, not guessed from generic Xbox 360 documentation.

## Design

### Ring buffer registration (real, not stubbed)

`VdInitializeRingBuffer` stores `(physAddr, sizeLog2Raw)` from `r3`/`r4` in host-side
globals (`g_gpuRingBufferBase`, `g_gpuRingBufferSizeLog2Raw`). No allocation — the address is
already valid, already-owned guest memory.

Size decoding is not fully reverse-engineered from public docs alone; rather than guess the
exact bit formula, store the raw log2 value and derive a conservative size (`8u << (raw +
3)`, matching the field's construction in the call site — `r4 = 31 - clz(originalSize) - 3`,
so `originalSize ≈ 8 << (r4 + 3)` inverts that). Log the decoded size on first call so it's
visible and correctable once real trace output confirms or contradicts it, rather than
silently trusting an unverified formula.

`VdEnableRingBufferRPtrWriteBack` stores the given address as `g_gpuRptrWriteBackAddr`. Real
semantics: the GPU writes its "consumed up to here" read pointer to this address so the CPU
can tell how much room is free. Since there's no real GPU catching up, write a "fully
caught up" value here — the write pointer we'll compute below — every time we scan the
buffer (see next section), so the game never blocks waiting for a GPU that doesn't exist.
This mirrors the Phase 3H lesson directly: an unanswered completion signal is exactly the
shape of bug that produces a silent infinite stall, so this gets closed out from the start
rather than left as a no-op.

`VdSetSystemCommandBufferGpuIdentifierAddress` stores the given address (when non-zero) as
`g_gpuIdentifierAddr`, no other behavior — not enough evidence yet to know what should be
written there, and nothing observed depending on its value so far.

`VdGetSystemCommandBuffer` gets a small (64 KiB) dedicated guest memory region from the
existing bump allocator (matching `ExCreateThread`'s established allocation pattern) the
first time it's called, and writes that region's `(address, size)` back through the two
out-pointers plus returns success in `r3` — real, non-garbage values, low implementation
risk, avoids leaving the caller with an uninitialized-pointer crash risk. Not treated as a
primary command stream for Milestone 1; revisit if trace evidence shows real traffic here.

### Finding "what's new" without a write-pointer event

Since there's no discrete "write pointer updated" event to hook, and PM4 Type-3 packets are
self-describing (a header word encodes a `count` field giving the packet's total length in
dwords), the tracer parses forward from a `g_gpuLastParsedOffset` (starting at 0) through
consecutive well-formed packets, advancing the offset past each one, and stopping the moment
it hits something that doesn't parse as a valid packet header (garbage, zero-fill, or a
count that would run past the buffer). This finds exactly the real newly-written command
data without needing to know the true write pointer, at the cost of stopping early if the
game ever leaves a deliberate gap — acceptable for a first-look evidence pass.

This scan runs once per `VdSwap` call (the existing real frame-boundary hook) rather than on
every single command-buffer write, keeping overhead low and output readable (one summarized
block per frame instead of a flood).

### PM4 packet parsing

PM4 packet headers are a stable, publicly documented format shared across the ATI/AMD R500
GPU family Xenos belongs to — implementing the header-level parser doesn't require
game-specific guessing, only the *payload* semantics of specific opcodes do. Minimum viable
parser:

- Read one header dword. Top 2 bits select packet type (0, 1, 2, or 3).
- Type 0 (register write): next field is a starting register index and a count; log
  `"TYPE0 reg=0x{index} count={n}"` and skip `count` dwords of payload.
- Type 2 (no-op/padding): zero-length, skip.
- Type 3 (opcode packet): extract the opcode field and count; log
  `"TYPE3 opcode=0x{opcode} count={n}"` and skip `count` dwords of payload. This is where
  draw calls, state-block sets, and shader binds live — decoding specific opcodes is
  explicitly deferred to the next phase, once trace output shows which opcodes this game
  actually issues.
- Anything else (type 1, or a header that doesn't decode to a sane count): stop the scan,
  log the raw remaining bytes as a hex dump for manual inspection, matching the project's
  established "capture raw evidence when a parser doesn't yet understand the data" pattern
  (same posture `xdvdfs.cpp` takes toward unparsed BST padding).

### Output

A dedicated trace log file (`gpu_trace.log` in the working directory, opened once at
startup, flushed per frame) rather than stdout — this is expected to be high-volume
per-frame data, and stdout is already used for the existing `[kernel]`/`[stub]` call log.
Each frame's block is header-stamped with a frame counter so packet sequences are easy to
correlate across frames while reading the file.

## Success Criteria

1. `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`,
   `VdSetSystemCommandBufferGpuIdentifierAddress`, and `VdGetSystemCommandBuffer` implemented
   for real (no longer no-ops), removed from `host/kernel_imports.txt` /
   `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean.
3. A real run shows the ring buffer receiving genuinely non-zero data (previously
   impossible — no real address ever reached the game).
4. `gpu_trace.log` shows real parsed PM4 packet headers (type + opcode + count) for at least
   one frame, with a concrete list of which Type-3 opcodes actually appear — the real
   evidence the next phase's renderer design will be built from.
5. No regression: the game still reaches the same stable, continuously-running per-frame
   state as before (verified the same way as Phase 3I/3J — extended watchdog + lldb
   sampling showing a genuinely evolving call stack).

## Explicitly Out of Scope

- Any real rendering (Metal/Vulkan/OpenGL) — this phase produces a readable trace, nothing
  is drawn.
- Decoding specific Type-3 opcode payloads (draw call parameters, shader tokens, texture
  state) — deferred to the renderer-design phase that follows, once this phase's trace shows
  which opcodes are actually real for this game.
- Exact hardware-accurate ring-buffer size decoding or wraparound handling — the conservative
  formula above is a starting point, correctable once real evidence contradicts it.
- `VdGetSystemCommandBuffer`'s buffer treated as a real second command stream — given a
  real, non-crashing region and left at that.
