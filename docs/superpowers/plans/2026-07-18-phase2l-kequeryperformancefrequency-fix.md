# Phase 2L: Fix KeQueryPerformanceFrequency Calling Convention Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix `KeQueryPerformanceFrequency` (`host/kernel_impl.cpp:266-270`) to return its
64-bit result by value in `r3`, matching the real XDK calling convention, instead of
writing through a bogus output pointer — resolving the root cause of the ~400+ second
`sub_82128110` busy-wait stall found during the Phase 2L investigation.

**Architecture:** Single-function change to `host/kernel_impl.cpp`, no other files touched.

**Tech Stack:** C++17, no new dependencies.

## Global Constraints

- Only `KeQueryPerformanceFrequency`'s body changes. No other kernel function's signature
  is audited or touched in this phase (per spec's explicit out-of-scope).
- The watchdog in `host/main.cpp` must be temporarily raised for the verification run
  (Task 2) to give the now-bounded-but-still-~8s loop room to complete, then reverted
  before this phase closes — matching this project's established practice of never
  leaving diagnostic changes in `host/main.cpp` permanently.

---

## Task 1: Fix KeQueryPerformanceFrequency

**Files:**
- Modify: `host/kernel_impl.cpp:266-270`

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing consumed by other tasks — self-contained.

- [ ] **Step 1: Replace the pointer-write with a return-by-value**

Replace:
```cpp
PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    uint32_t outPtr = (uint32_t)ctx.r3.u64;
    PPC_STORE_U64(outPtr, 50000000ULL); // Xbox 360's documented hardware timebase frequency
}
```
with:
```cpp
PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    ctx.r3.u64 = 50000000ULL; // Xbox 360's documented hardware timebase frequency
}
```

- [ ] **Step 2: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2l_task1_build.log
grep -c "error:" /tmp/phase2l_task1_build.log
```
Expected: build succeeds, `grep -c "error:"` prints `0`.

- [ ] **Step 3: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Fix KeQueryPerformanceFrequency to return by value instead of via pointer"
```

---

## Task 2: Verify Against the Original Repro

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase2l_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2l-stub-hits.txt` (committed, only if new territory
  is reached — see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2l-stub-hits.txt` (conditional).

- [ ] **Step 1: Temporarily raise the watchdog to 30 seconds**

Modify `host/main.cpp`, changing:
```cpp
    auto status = future.wait_for(std::chrono::seconds(10));
```
to:
```cpp
    auto status = future.wait_for(std::chrono::seconds(30));
```

This is diagnostic-only — the spec's expected loop duration is ~8.3s, and 30s gives
comfortable margin to confirm it actually completes rather than merely running long.

- [ ] **Step 2: Rebuild and run**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tail -5
time ./build/BigBumpinHost private/default.xex > private/phase2l_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check whether the loop completed and report honestly**

Run: `tail -20 private/phase2l_run.log`

Three possible honest outcomes — report whichever actually happens:
- **Loop completed, new territory reached**: log shows activity past where
  `MmAllocatePhysicalMemoryEx` previously froze (`0xA08E0000`), or a clean return, or a
  new distinct failure point. This is the expected outcome per the spec. Continue to
  Step 4.
- **Loop still times out at 30s**: the `real` time reported by `time` was ~30s and the
  log still ends at the same old freeze point, or a new but still-incomplete point. The
  fix didn't fully resolve it as expected — do not paper over this. Stop, report the
  exact timing and log tail, do not proceed to Step 4/5's "new territory" assumptions.
- **Process exits well before 30s** (clean return or crash): also valid evidence, not a
  failure of this step — report the exact exit status and log tail.

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

Do this regardless of Step 3's outcome — the watchdog bump was diagnostic-only and must
not remain in the committed state.

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress past the old freeze point:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2l_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2l-stub-hits.txt; wc -l docs/superpowers/specs/phase2l-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2l-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2L verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body — loop completed and how far
execution got, or it didn't and exactly what was observed. This closes Phase 2L.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's entire design section (return-by-value
  fix, no other call sites touched). Task 2 covers success criteria 2-4: clean build
  (Task 1 Step 2), honest re-run report with adequate watchdog margin (Task 2 Steps 1-3),
  and conditional stub-hit capture (Step 5) matching the spec's explicit "don't assume
  complete resolution" caveat.
- **Placeholder scan:** No TBD/TODO. Step 3's three outcomes are each given concrete,
  explicit handling.
- **Type/interface consistency:** N/A — single self-contained function change, no
  cross-task interface to keep in sync.
