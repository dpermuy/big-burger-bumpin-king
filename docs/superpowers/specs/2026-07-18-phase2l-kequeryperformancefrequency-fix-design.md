# Phase 2L: Fix KeQueryPerformanceFrequency Calling Convention — Big Bumpin' Recomp

## Context

Phase 2K's real critical-section fix didn't resolve the physical-memory-stall symptom.
Live instrumentation of the actual freeze point (`sub_82128110`'s busy-wait loop,
`private/ppc/ppc_recomp.11.cpp:38044-38076`) showed it isn't a deadlock or livelock at
all — it's a bounded timing loop that was going to take ~400+ real seconds to complete,
comfortably outlasting both the 10s and 120s watchdog windows tested so far.

Tracing the loop's threshold value back to its source (rather than assuming a timer-
frequency scaling issue) found the real bug: the threshold is computed from a "frequency"
value stored in guest memory by `sub_820AA058`, which itself just forwards the result of a
call to `__imp__KeQueryPerformanceFrequency` (`host/kernel_impl.cpp:266-270`):

```cpp
PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    uint32_t outPtr = (uint32_t)ctx.r3.u64;
    PPC_STORE_U64(outPtr, 50000000ULL);
}
```

This implementation assumes an output-pointer calling convention (`r3` = pointer to write
the result into). But the real XDK signature is `LARGE_INTEGER KeQueryPerformanceFrequency(void)`
— no arguments, 64-bit return value directly in `r3`. Reading the actual call site
(`private/ppc/ppc_recomp.2.cpp:4829-4837`) confirms this: the caller sets up no argument
before the call and reads the result via `mr r11,r3` immediately after returning. The `r3`
value present at call time is just leftover register content from the caller's own
unrelated incoming argument — never intended as an argument to this call.

Net effect of the bug: our stub writes `50000000` to `*(garbage address)` (a wild write
nobody reads) and leaves `r3` unchanged on return, so the caller stores that same garbage
pointer value (measured at runtime: `0x900FFBA0`) into guest memory, believing it's the
performance-counter frequency. Downstream, `sub_82128110`'s busy-wait loop computes its
threshold as `freqConst * 4` using that garbage value, inflating what should be a short,
bounded wait into one requiring ~400+ real seconds at this host's actual tick rate.

## Goal

Fix `KeQueryPerformanceFrequency` to use the real return-by-value calling convention, so
the guest's own timing computations use the intended `50,000,000` constant instead of a
stray pointer value.

## Design

Change `host/kernel_impl.cpp:266-270` from a pointer-write to a direct return value:

```cpp
PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    ctx.r3.u64 = 50000000ULL; // Xbox 360's documented hardware timebase frequency
}
```

No other call sites, structs, or callers need to change — this is a single-function
signature correction, not a new feature.

### Expected effect, not guaranteed complete resolution

With the fix, `sub_82128110`'s busy-wait loop will compute `threshold = 50,000,000 * 4 =
200,000,000` ticks. This project's ARM64 host measures `CNTVCT_EL0` (backing `__rdtsc()`,
used for the guest's `mftb` reads) incrementing at approximately 24,000,000 ticks/second —
so the loop should now take roughly `200,000,000 / 24,000,000 ≈ 8.3` real seconds, a
bounded and much more tractable wait than the previous ~400+ seconds. This is still
roughly 2x longer than the loop would take on real Xbox 360 hardware (a separate, known,
out-of-scope issue: `__rdtsc()`'s ARM64 implementation in vendored
`thirdparty/XenonRecomp/XenonUtils/ppc_context.h:694-701` returns raw, unscaled
`CNTVCT_EL0` ticks with no conversion to the real console's timebase rate). Success
criteria below call for an honest report of what actually happens, not an assumption that
this single fix resolves the stall completely — there may be more busy-wait loops or other
issues downstream once this one clears.

## Success Criteria

1. `KeQueryPerformanceFrequency` returns the frequency by value in `r3`, matching the real
   XDK signature.
2. `BigBumpinHost` builds clean (zero compiler errors).
3. Re-run `private/default.xex` with a watchdog long enough to observe the
   `sub_82128110` loop's actual new behavior (at least 15-20 seconds, given the ~8.3s
   expected duration plus margin). Report honestly whether the loop now completes, how
   long it took, and whether execution reaches new territory afterward — not just whether
   the process exits before some fixed cutoff.
4. If new territory is reached, capture the fresh run log and a distinct stub-hit list,
   per the established phase pattern.

## Explicitly Out of Scope

- The `__rdtsc()` ARM64 unscaled-tick issue in vendored `ppc_context.h` — real, causes
  roughly 2x timing inflation across all guest timing code, but does not by itself cause
  indefinite/multi-hundred-second stalls the way the `KeQueryPerformanceFrequency` bug did.
  Deserves its own scoped investigation and spec if it turns out to matter after this fix
  lands (e.g., if some other loop's threshold is timing-sensitive enough that a 2x
  slowdown alone causes a practical problem).
- Auditing other kernel functions for the same "output pointer vs. return-by-value"
  calling-convention mistake — no evidence yet that this specific error pattern recurs
  elsewhere; would be speculative to fix unconfirmed cases.
- Any change to the critical-section implementation (Phase 2K, already correct and
  unrelated to this bug).
