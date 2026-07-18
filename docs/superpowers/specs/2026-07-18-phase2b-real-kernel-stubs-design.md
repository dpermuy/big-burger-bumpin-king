# Phase 2B: Real Implementations for Hit Kernel Imports — Big Bumpin' Recomp

## Context

Phase 2A ([spec](2026-07-18-phase2a-host-bootstrap-harness-design.md)) got `BigBumpinHost`
linking and running `_xstart` against 210 mechanically-stubbed kernel imports. The run
terminated in a crash, an accepted Phase 2A outcome — but the log gave a real, evidence-based
worklist: only 11 of the 210 stubbed imports were actually called before termination
(`docs/superpowers/specs/phase2a-stub-hits.txt`):

```
HalReturnToFirmware
KeBugCheck
KeGetCurrentProcessType
KeTlsAlloc
KeTlsSetValue
NtAllocateVirtualMemory
RtlEnterCriticalSection
RtlInitAnsiString
RtlInitializeCriticalSection
RtlLeaveCriticalSection
XexCheckExecutablePrivilege
```

The 2A run log's tail showed a repeating `RtlInitAnsiString` ×3 → `KeBugCheck` →
`RtlEnterCriticalSection` → `RtlLeaveCriticalSection` loop with a decrementing counter in
`r6` (`0xD7B` → `0xD78` → `0xD75` → ...), ending in a crash (SIGBUS). Root cause: the real
`KeBugCheck` never returns (it halts the system); our 2A stub returns like every other stub,
so the caller — which does not expect to regain control after a bugcheck — kept executing
into unintended territory until it faulted.

## Goal

Replace all 11 hit stubs in `host/kernel_stubs.cpp` with real implementations, close enough
to actual Xbox 360 kernel semantics that the run either progresses further (ideally past
this crash loop) or fails at a *new*, more informative point — extending the evidence trail
rather than statically implementing all 210 blind.

## Design Decisions (confirmed)

- **`ExCreateThread` still does not spawn a real thread** (2A's scope cut carries forward
  unchanged). Every implementation in this phase assumes a single logical execution
  context — no real mutex/synchronization needed for critical sections or TLS, just
  correct-looking guest-visible state.
- **`NtAllocateVirtualMemory` uses a bump allocator**: hand out sequential, never-freed
  regions from unused space in the 4 GiB guest buffer. `NtFreeVirtualMemory` isn't in the
  hit list — building free-list logic now would be solving a problem not yet observed.
- **`KeBugCheck` and `HalReturnToFirmware` terminate the process.** Both are real
  "never returns" kernel calls; the fix for 2A's crash loop is making our stubs behave that
  way instead of returning control to code that doesn't expect it.

## Per-Function Design

Confidence varies per function. Grouped by how well-established the semantics are.

### High confidence (well-established Windows/Xbox 360 kernel conventions)

- **`KeBugCheck(r3=code)`** — log the bugcheck code, then terminate via `std::_Exit` with a
  distinct exit code (e.g. `3`) so it's distinguishable from the watchdog timeout (`2`) and
  normal exit (`0`) in future automated runs.
- **`HalReturnToFirmware(r3=routine)`** — log the routine argument, terminate via
  `std::_Exit(4)`.
- **`KeGetCurrentProcessType()`** — no meaningful arguments; return the "title process"
  constant (`1`) in `ctx.r3`. No guest-memory writes needed.
- **`RtlInitAnsiString(r3=dest, r4=source)`** — classic `ANSI_STRING` structure
  (`uint16 Length, uint16 MaximumLength, uint32 Buffer` — 8 bytes, matching this XEX's
  32-bit pointer convention). If `source` (r4) is non-null: read the guest C-string at
  `base + source` byte-by-byte until a null terminator to compute `Length`, set
  `MaximumLength = Length + 1`, `Buffer = source`. If `source` is null: `Length = 0`,
  `MaximumLength = 0`, `Buffer = 0`. Write all three fields through `PPC_STORE_U16`/
  `PPC_STORE_U32` (big-endian guest byte order) at `dest`.
- **`RtlInitializeCriticalSection(r3=cs)` / `RtlEnterCriticalSection(r3=cs)` /
  `RtlLeaveCriticalSection(r3=cs)`** — standard `RTL_CRITICAL_SECTION` shape
  (`LockCount, RecursionCount, OwningThread, LockSemaphore, SpinCount`). Since there is one
  logical execution context in this phase, these don't need real locking: Initialize writes
  `RecursionCount = 0`, `OwningThread = 0`; Enter increments `RecursionCount` and sets
  `OwningThread` to a fixed non-zero sentinel value (there's no real guest thread ID to use
  yet); Leave decrements `RecursionCount` and clears `OwningThread` back to `0` once it
  reaches `0`. This is enough for guest code that checks "do I already hold this lock"
  (recursive-acquire patterns) to behave correctly, without needing an actual mutex.
- **`KeTlsAlloc()` / `KeTlsSetValue(r3=index, r4=value)`** — a fixed-size host-side array
  (e.g. 64 slots — Windows' own `TLS_MINIMUM_AVAILABLE` is 64, a reasonable, well-precedented
  ceiling), a static counter handing out the next free index. `KeTlsAlloc` returns the next
  index in `ctx.r3` (or a sentinel "out of slots" value past the array if exhausted — not
  expected to matter at this scale, but not silently overflowing either). `KeTlsSetValue`
  stores `value` into the slot and returns success. (`KeTlsGetValue`/`KeTlsFree` aren't in
  the hit list — not implemented this phase.)
- **`XexCheckExecutablePrivilege(r3=privilege)`** — real semantics check a privilege bit
  against the XEX's signed privilege flags. We don't have a privilege-flag parser, and this
  is a personal single-player backup, not a scenario with real DRM/multiplayer stakes.
  Simplification: always return "granted" (`ctx.r3 = 1`). Documented here as a deliberate
  permissiveness simplification, not an oversight — revisit only if a specific privilege
  check turns out to gate behavior we actually want disabled.

### Needs empirical verification during implementation

- **`NtAllocateVirtualMemory`** — the real NT-style signature is
  `(PPVOID BaseAddress, SIZE_T ZeroBits, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect)`,
  but Xbox 360's kernel is known to sometimes trim unused parameters from the stock NT
  signature, which would shift the register mapping. The captured call
  (`r3=0x900FFC40, r4=0x900FFD24, r5=0x60002000, r6=0x4`) has `r3` and `r4` both looking
  like guest stack addresses (plausible in/out pointers for `BaseAddress` and `RegionSize`),
  which is consistent with the standard mapping *if* `ZeroBits` was simply never passed
  (i.e. this XEX's compiled call site only supplies 4 of the 5 typical arguments) — but
  this is a hypothesis, not confirmed. The implementation task must log all of `r3`–`r7` on
  first hit and reason from the actual pointer/size-shaped values before committing to a
  register mapping, the same evidence-first approach used to root-cause the `.reloc` bug in
  Phase 2A. Once the mapping is confirmed: read the in/out `RegionSize` value, hand back a
  guest address from the bump allocator's current offset, advance the offset by the
  (page-aligned) requested size, write the allocated address back through the `BaseAddress`
  out-pointer, return success in `ctx.r3`.

  Bump allocator region: reserve guest addresses starting at `0xA0000000` (comfortably clear
  of the image region `0x82000000`–`0x82740000`, the indirect-call table that follows it,
  and the `0x90000000` stack region reserved in Phase 2A) growing upward within the 4 GiB
  mapped buffer.

## Success Criteria

Phase 2B is complete when:

1. All 11 functions in `host/kernel_stubs.cpp` are replaced with the implementations above
   (moved out of that file into a new `host/kernel_impl.cpp`, since they're no longer
   placeholders — `kernel_stubs.cpp` keeps only the remaining 199 untouched placeholders).
2. `BigBumpinHost` builds clean (zero compiler errors) with the new implementations.
3. A fresh run either progresses further than Phase 2A's run (different, later log content —
   ideally past the `KeBugCheck` loop entirely, since it no longer returns) or fails at a
   demonstrably different point, with a clear explanation of what new stub or condition it
   hit.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted, the same way
   Phase 2A did — becoming Phase 2C's evidence-based worklist.

## Explicitly Out of Scope

- Real thread creation (`ExCreateThread` still a no-op success stub).
- Any of the other 199 never-hit kernel imports.
- `NtFreeVirtualMemory` / a real free-list allocator.
- Graphics, audio, input (unchanged from prior phases).
