# Phase 2I: Real Wait Timeout Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `KeWaitForSingleObject` real timeout semantics for the one confirmed case (finite relative timeout), so a fresh `BigBumpinHost` run paces itself at the guest code's designed rate instead of spinning at full CPU speed for the entire watchdog budget.

**Architecture:** Same per-phase pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append the real implementation to `host/kernel_impl.cpp`.

**Tech Stack:** C++17 (`<chrono>`, `<thread>` — the latter already included from Phase 2H).

## Global Constraints

- Only the confirmed case is handled: `TimeoutPtr` (`r7`) non-null and pointing to a negative (relative) 100ns-tick value. Positive (absolute) values and a null `TimeoutPtr` (wait forever) keep the existing immediate-success behavior — no evidence either occurs yet.
- 100ns-tick-to-millisecond conversion: `milliseconds = ticks / 10000` (1ms = 10,000 × 100ns).
- Return `258` (`STATUS_TIMEOUT`) after the sleep in the handled case — this is the value the guest code's own `if (r3 == 258)` check expects (confirmed by reading `sub_820C8D28` directly).
- No changes to any other wait-family function (`NtWaitForSingleObjectEx`, `KeWaitForMultipleObjects`, etc.) — out of scope, no evidence they need it.

---

## Task 1: Implement Real KeWaitForSingleObject Timeout Handling

**Files:**
- Modify: `host/kernel_imports.txt` (remove 1 line)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_LOAD_U64` (already used elsewhere in `host/kernel_impl.cpp`).
- Produces: nothing consumed by other tasks — this is a self-contained leaf function.

- [ ] **Step 1: Remove KeWaitForSingleObject from the import list**

Edit `host/kernel_imports.txt`, delete the line `KeWaitForSingleObject`.

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 162 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `162`

- [ ] **Step 4: Add the chrono include**

Modify `host/kernel_impl.cpp`, adding `#include <chrono>` to the top include block (alongside the existing `<cstdint>`, `<cstdlib>`, `<ctime>`, `<mutex>`, `<thread>`, `<unordered_map>`).

- [ ] **Step 5: Implement KeWaitForSingleObject**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__KeWaitForSingleObject)
{
    uint32_t timeoutPtr = (uint32_t)ctx.r7.u64;

    if (timeoutPtr != 0)
    {
        int64_t timeoutValue = (int64_t)PPC_LOAD_U64(timeoutPtr);
        if (timeoutValue < 0)
        {
            // Negative = relative time, in 100ns units (standard NT convention).
            // Confirmed via captured runtime value from a real guest
            // timeout-polling loop: -300000 = -30ms, a legitimate bounded poll,
            // not an infinite/forever wait (see Phase 2I design spec).
            int64_t hundredNsUnits = -timeoutValue;
            auto durationMs = std::chrono::milliseconds(hundredNsUnits / 10000);
            std::this_thread::sleep_for(durationMs);

            ctx.r3.u64 = 258; // STATUS_TIMEOUT
            return;
        }
        // Positive (absolute time) -- not observed yet, fall through to the
        // existing immediate-success behavior rather than guess at handling.
    }

    // TimeoutPtr null (wait forever) or an unobserved absolute-time case:
    // keep existing behavior.
    ctx.r3.u64 = 0;
}
```

- [ ] **Step 6: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2i_task1_build.log
grep -c "error:" /tmp/phase2i_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`.

- [ ] **Step 7: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement real KeWaitForSingleObject timeout semantics"
```

---

## Task 2: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2i_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2i-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2i-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2i_run.log`
Expected: no multi-million-line spin on `KeWaitForSingleObject`/`KeResetEvent` — the loop
should now pace itself at roughly 30ms per iteration instead of unthrottled. Given the
watchdog is 10 seconds, expect on the order of ~300 iterations of that specific loop if it
keeps hitting the same wait, not millions. Record whatever the run actually does; if it's
still spinning heavily (just slower), that's still useful information, not a success to
claim regardless.

- [ ] **Step 2: Check the log size is sane before proceeding**

Run: `wc -l private/phase2i_run.log`
Expected: a number in the hundreds-to-low-thousands range, not millions. If it's still in
the millions, stop and report that the fix didn't resolve the spin as expected — don't
proceed to extract a stub-hit list from a multi-million-line file.

- [ ] **Step 3: Compare against the post-2H-fix run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2h_fix_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2i_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.
(`private/phase2h_fix_run.log` was saved during the post-2H bugfix investigation; if it's
not present, compare against `private/phase2h_run.log` instead and note the substitution.)

- [ ] **Step 4: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2i_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2i-stub-hits.txt; wc -l docs/superpowers/specs/phase2i-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called
during this run.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/phase2i-stub-hits.txt
git commit -m "Record Phase 2I bring-up run: updated kernel import hit list"
```

This closes Phase 2I.

---

## Plan Self-Review Notes

- **Spec coverage:** The implementation matches the spec's design exactly (handled case, explicit fallback for unhandled cases, scoped to this one function). Success criteria 1–2 map to Task 1. Criteria 3–4 map to Task 2, including an explicit sanity check (Task 2 Step 2) against the specific failure mode this phase exists to fix, rather than assuming success.
- **Placeholder scan:** No TBD/TODO. The unhandled cases (positive/absolute timeout, null/wait-forever) are explicit code paths with comments explaining why they're deliberately unhandled, not silently missing.
- **Type/interface consistency:** `PPC_LOAD_U64` usage matches its existing use in `RtlTimeToTimeFields` (Phase 2C) in the same file. `std::chrono`/`std::this_thread::sleep_for` are standard library, no project-specific interface to keep in sync.
