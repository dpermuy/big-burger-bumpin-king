# Phase 2R: Initialize r13 (SDA Base Register) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize `r13` to `0x82670000` (the cross-referenced-confirmed small-data-area
base) for the main thread and every `ExCreateThread`-spawned thread, so pervasive
`offset(r13)`-relative global variable access throughout the compiled program reads real
data instead of near-null addresses.

**Architecture:** Two-line fix across two files -- `host/main.cpp` (main thread's initial
`PPCContext`) and `host/kernel_impl.cpp` (`ExCreateThread`'s spawned-thread `PPCContext`)
-- followed by careful, honest verification given the scale of what this touches.

**Tech Stack:** No new dependencies.

## Global Constraints

- `r13 = 0x82670000` exactly -- confirmed via cross-reference in the spec, not a
  placeholder to tune later.
- `r2` is not touched -- confirmed unused anywhere in the compiled codebase.
- Verification must not assume success; this fix touches pervasive addressing, so a wider
  range of outcomes (including new, different failures) is plausible and must be reported
  exactly as observed.

---

## Task 1: Initialize r13 in Both PPCContext Setup Sites

**Files:**
- Modify: `host/main.cpp:79-80`
- Modify: `host/kernel_impl.cpp` (`ExCreateThread`'s lambda, currently around line 548-563)

**Interfaces:**
- No new interfaces -- both are self-contained context-setup call sites.

- [ ] **Step 1: Set r13 for the main thread**

In `host/main.cpp`, find:
```cpp
    PPCContext ctx{};
    ctx.r1.u64 = kStackBase + kStackSize - 0x10;
```
Replace with:
```cpp
    PPCContext ctx{};
    ctx.r1.u64 = kStackBase + kStackSize - 0x10;
    ctx.r13.u64 = 0x82670000; // small-data-area base (confirmed via cross-reference, Phase 2R)
```

- [ ] **Step 2: Set r13 for every ExCreateThread-spawned thread**

In `host/kernel_impl.cpp`, find the lambda inside `ExCreateThread`:
```cpp
        g_currentThreadHandle = handle;

        PPCContext threadCtx{};
        threadCtx.r1.u64 = stackBase + kStackSize - 0x10;
```
Replace with:
```cpp
        g_currentThreadHandle = handle;

        PPCContext threadCtx{};
        threadCtx.r1.u64 = stackBase + kStackSize - 0x10;
        threadCtx.r13.u64 = 0x82670000; // small-data-area base, same as main thread (Phase 2R)
```

- [ ] **Step 3: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2r_task1_build.log
grep -c "error:" /tmp/phase2r_task1_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 4: Commit**

```bash
git add host/main.cpp host/kernel_impl.cpp
git commit -m "Initialize r13 small-data-area base register for all threads"
```

---

## Task 2: Verify Against the Original Repro

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase2r_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2r-stub-hits.txt` (committed, only if new territory
  is reached -- see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2r-stub-hits.txt` (conditional).

- [ ] **Step 1: Temporarily raise the watchdog to 25 seconds**

Modify `host/main.cpp`, changing:
```cpp
    auto status = future.wait_for(std::chrono::seconds(10));
```
to:
```cpp
    auto status = future.wait_for(std::chrono::seconds(25));
```

- [ ] **Step 2: Rebuild and run**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tail -5
./build/BigBumpinHost private/default.xex > private/phase2r_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check outcome and report honestly**

Run: `tail -40 private/phase2r_run.log; wc -l private/phase2r_run.log`

Given this fix's pervasive scope, report exactly what happened -- do not assume the best
or worst case:
- **New territory / the busy-poll is gone**: log shows activity past the Phase 2P freeze
  point (last `KfReleaseSpinLock` line), or the busy-poll pattern from Phase 2Q no longer
  appears, or a clean return. Continue to Step 4.
- **A new, different failure**: a crash, a different stall point, or unexpected behavior
  caused by previously-near-null data suddenly reading as real (but possibly
  differently-laid-out) data. This is valid, useful evidence, not a failure of this
  step -- report the exact symptom.
- **No observable change**: still stuck at exactly the same point as before. Report the
  exact log tail; this would mean the r13 fix, while plausibly correct in isolation,
  wasn't sufficient on its own.

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

Do this regardless of Step 3's outcome.

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2r_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2r-stub-hits.txt; wc -l docs/superpowers/specs/phase2r-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2r-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2R verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body. This closes Phase 2R.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's entire design section (both call sites,
  exact confirmed value, `r2` correctly left untouched). Task 2 covers success criteria
  2-4: clean build (Task 1 Step 3), honest re-run report acknowledging the wider range of
  plausible outcomes this fix's scope implies (Task 2 Steps 1-3), conditional stub-hit
  capture (Step 5).
- **Placeholder scan:** No TBD/TODO. Step 3's three outcomes are each given concrete,
  explicit handling, including the "new different failure" case this fix's scope makes
  newly plausible.
- **Type/interface consistency:** Both call sites use the identical literal
  `0x82670000` and identical field name (`r13`) -- no drift between main-thread and
  spawned-thread setup.
