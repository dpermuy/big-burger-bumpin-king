# Phase 2R: Initialize r13 (Small-Data-Area Base Register) — Big Bumpin' Recomp

## Context

Phase 2Q traced the persistent post-2P stall to `sub_820B9A58`, which busy-polls (over
1.1 billion iterations in 30 real seconds) comparing a "global tick" value against a
fixed threshold of `5000`. The tick value, read via `PPC_LOAD_U32(ctx.r13.u32 + 88...)`-
style small-data-area-relative addressing, never advances.

Phase 2R's investigation found the real cause: `ctx.r13` -- the PowerPC EABI's dedicated
small-data-area (SDA) base register, used throughout compiled PPC code for
`offset(r13)`-style near-global-variable access -- is **never initialized** anywhere in
this project. `host/main.cpp:79-80` constructs a zero-initialized `PPCContext` and sets
only `ctx.r1` (the stack pointer); `r13` stays at its zero default. `ExCreateThread`'s
thread-spawning lambda (`host/kernel_impl.cpp`) has the same gap for every subsequently
spawned thread. Neither `_xstart` nor any other recompiled game code initializes `r13`
itself -- per standard PPC EABI convention, that's the CRT/loader's job, done once before
any program code runs, and this project's host harness never took on that role.

Since `r13`-relative addressing is used pervasively (not just by the one stall-causing
function) for accessing global data throughout the entire compiled program, this single
gap plausibly explains a wide class of latent correctness issues beyond just this stall --
every global read/write done through `offset(r13)` has been reading/writing near address
`0x0` instead of the intended location.

### Confirmed correct value

`0x82670000`, established via cross-referencing, not guesswork: `sub_820B9A58`'s
`r13`-relative access at offset `256`, chased into the region it points to, uses the same
`+9960` field offset that a *different*, independently-analyzed function
(`sub_8212D9A8`, from the Phase 2L/2Q `KeQueryPerformanceFrequency` investigation) reaches
via absolute `lis`-based addressing at literal address `0x826726E8`. The only value of
`r13` consistent with both access paths pointing at the same underlying data is
`0x82670000` (`0x82670000 + 9960 = 0x826726E8`, exact match).

### r2 (SDA2 base) explicitly checked, found unused

Real PPC EABI also defines `r2` as a second small-data-area base (typically for read-only
data). Searched the entire generated `private/ppc/*.cpp` for any `offset(r2)`-style
addressing: zero matches anywhere in this codebase. `r2` doesn't need initialization --
this game's compiled code doesn't use it as a base register, so scope stays limited to
`r13`.

## Goal

Initialize `r13` to `0x82670000` for the main thread and for every `ExCreateThread`-spawned
thread, so small-data-area-relative global variable access throughout the whole program
reads and writes real data instead of near-null addresses.

## Design

Two call sites need the same value, matching the existing pattern for `r1` (each already
sets up its own initial `PPCContext`):

- **`host/main.cpp`** (main thread): alongside the existing `ctx.r1.u64 = ...` line, add
  `ctx.r13.u64 = 0x82670000;`.
- **`ExCreateThread`'s lambda** (`host/kernel_impl.cpp`): alongside the existing
  `threadCtx.r1.u64 = ...` line, add `threadCtx.r13.u64 = 0x82670000;`. Every spawned
  thread runs the same compiled code and needs the same SDA base -- this is a
  process-wide constant, not per-thread state (confirmed: nothing in game code ever
  writes to the `r13`-relative region, consistent with it being fixed, kernel/loader-
  established data, not something threads individually set up).

No other files change. This is a two-line fix, but a foundational one -- the value must
be exactly right (confirmed via cross-reference above, not assumed).

## Success Criteria

1. `ctx.r13`/`threadCtx.r13` initialized to `0x82670000` in both call sites.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. Re-run `private/default.xex` with an adequate watchdog and report honestly what
   happens. Given the scale of this fix (a pervasively-used addressing mode, not one
   function), do not assume it resolves everything -- report exactly what changes:
   whether the `sub_820B9A58` busy-poll stops, whether execution reaches new territory,
   whether a new and different failure appears (plausible, since large amounts of
   previously-near-null global data will suddenly read as real, possibly
   differently-structured data), or whether nothing observable changes.
4. If new territory is reached, capture the fresh run log and a distinct stub-hit list,
   per the established phase pattern.

## Explicitly Out of Scope

- `r2` (SDA2 base) -- confirmed unused anywhere in the compiled codebase, no
  initialization needed.
- Auditing every other `PPCContext` field for similar uninitialized-register gaps beyond
  what's already evidenced (`r1`, and now `r13`) -- no evidence yet that any other
  register has this problem; would be speculative to "fix" unconfirmed cases.
- Verifying the *contents* of the `0x82670000`-based small-data region are correct beyond
  the one cross-referenced field (`+9960`) -- if a run after this fix reveals a specific
  field reads wrong/unexpected data, that's a new, separate investigation, not assumed to
  be covered by this phase.
