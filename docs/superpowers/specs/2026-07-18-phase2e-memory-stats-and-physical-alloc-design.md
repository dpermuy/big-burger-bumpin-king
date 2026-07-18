# Phase 2E: Memory Statistics and Physical Allocation — Big Bumpin' Recomp

## Context

Phase 2D ([spec](2026-07-18-phase2d-file-io-no-hard-disk-design.md)) resolved file I/O as
"no hard disk present," which turned out to cause the game's own fallback path to skip
crypto and the rest of the file operations entirely. A fresh run left only 3 imports hit
(`docs/superpowers/specs/phase2d-stub-hits.txt`): `ExCreateThread`,
`MmAllocatePhysicalMemoryEx`, `MmQueryStatistics`. The run's terminal SIGTRAP immediately
follows the `MmQueryStatistics`/`MmAllocatePhysicalMemoryEx` sequence — `ExCreateThread`'s
lack of real thread spawning has not been shown to be the actual blocker, since the main
thread continues normally past it.

Before designing, `MmQueryStatistics`'s buffer was investigated the same way prior phases
investigated register mappings and file paths: temporarily dumping the raw bytes of the
caller-supplied buffer before the call. This produced a genuinely mixed result — one clear,
confident finding (the buffer's first 4 bytes are `0x00000068`, matching the standard NT
convention of the caller pre-filling a `Length` field for validation before the kernel
fills in statistics — a real `MM_STATISTICS` structure is plausibly 0x68 (104) bytes), but
the rest of the dumped window did not yield a confident full field layout, and other
observed register values (`r4=0x19000000` appearing identically across multiple unrelated
calls) look like stale/leftover stack content rather than real per-call arguments. Unlike
`NtAllocateVirtualMemory`'s register mapping or the `NtCreateFile` device path, this
investigation didn't produce a clean, independently-confirmable picture of the full
structure.

## Goal

Implement `MmQueryStatistics` and `MmAllocatePhysicalMemoryEx` conservatively, matching
the same risk posture Phase 2B used for critical sections: acknowledge what's confidently
known, don't guess the rest, and let a future run's evidence — not speculation — drive
further correctness work if needed.

## Design

- **`MmQueryStatistics(r3=statsBufferPtr)`** — read the `Length` field the caller
  pre-filled (confirmed present at offset 0, observed value `0x68`). Return
  `STATUS_SUCCESS` without writing any other bytes into the buffer. This deliberately
  leaves the actual statistics fields (available memory, page counts, etc.) unwritten,
  matching Phase 2B's choice not to write into `RTL_CRITICAL_SECTION` without a confirmed
  layout — the risk of writing plausible-looking-but-wrong values into an under-verified
  structure outweighs the benefit until there's real evidence the game reads and acts on
  specific fields.
- **`MmAllocatePhysicalMemoryEx(r3=flags, ...)`** — reuse the same bump allocator
  `NtAllocateVirtualMemory` (Phase 2B) already established (`g_bumpAllocatorNext`). Our
  harness has no real physical/virtual memory distinction — a valid, exclusive guest
  address that the game can write to without crashing is what matters, not real physical
  memory semantics (contiguity, DMA-visibility, etc., none of which exist in this harness
  anyway). Return that address with success. The exact size/alignment argument mapping is
  in the same uncertain-register category as `MmQueryStatistics`'s extended fields — reuse
  the existing 64 KiB granularity rounding from `NtAllocateVirtualMemory` rather than
  inventing new unverified parameter handling.

## Success Criteria

Phase 2E is complete when:

1. Both functions are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp`.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than Phase 2D's run or fails at a new,
   demonstrably different point — this is a genuine open question given the conservative
   (not fully detailed) `MmQueryStatistics` implementation; if the crash persists
   unchanged, that's itself useful evidence that the missing statistics fields aren't the
   direct cause, narrowing where to look next.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted.

## Explicitly Out of Scope

- Real, fully-detailed `MM_STATISTICS` field values — no confirmed layout beyond the
  `Length` field. Revisit only if a future run's evidence (e.g. a crash traceable to a
  specific garbage memory-size value) forces it.
- `ExCreateThread` real thread spawning — still not shown to be the actual blocker,
  remains deferred.
- `NtFreeVirtualMemory` / freeing physical allocations — no evidence either is needed yet.
