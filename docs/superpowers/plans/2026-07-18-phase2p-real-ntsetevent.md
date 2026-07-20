# Phase 2P: Real NtSetEvent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `NtSetEvent` for real -- mark the target handle's `signaled` state and
wake waiters -- symmetric with the existing `NtReleaseMutant` implementation, resolving
the "always no-op" gap found during the Phase 2O investigation.

**Architecture:** Same per-phase pattern as every prior phase: trim
`host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append the real
implementation to `host/kernel_impl.cpp`.

**Tech Stack:** C++17, no new dependencies (`g_stateMutex`/`g_handleTable`/
`g_handleSignalCv` already exist from Phase 2C/2N).

## Global Constraints

- Only `NtSetEvent`'s behavior changes. No fix attempted for handle `0x1003` specifically,
  no change to `KeSetEvent`, no other event-family function touched -- all explicitly out
  of scope per the spec.

---

## Task 1: Implement Real NtSetEvent

**Files:**
- Modify: `host/kernel_imports.txt` (remove 1 line)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `g_stateMutex`, `g_handleTable`, `g_handleSignalCv` (all already declared,
  Phase 2C/2N).
- Produces: nothing consumed by other tasks -- self-contained leaf function.

- [ ] **Step 1: Remove NtSetEvent from the import list**

Edit `host/kernel_imports.txt`, delete the line `NtSetEvent`.

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 129 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `129`

- [ ] **Step 4: Implement NtSetEvent**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__NtSetEvent)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t previousStateOutPtr = (uint32_t)ctx.r4.u64;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_handleTable.find(handle);
        if (it != g_handleTable.end())
        {
            it->second.signaled = true;
        }
    }
    g_handleSignalCv.notify_all();

    if (previousStateOutPtr != 0)
    {
        PPC_STORE_U32(previousStateOutPtr, 0);
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2p_task1_build.log
grep -c "error:" /tmp/phase2p_task1_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement real NtSetEvent"
```

---

## Task 2: Verify Against the Original Repro

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase2p_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2p-stub-hits.txt` (committed, only if new territory
  is reached -- see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2p-stub-hits.txt` (conditional).

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
./build/BigBumpinHost private/default.xex > private/phase2p_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check outcome and report honestly**

Run: `tail -30 private/phase2p_run.log; wc -l private/phase2p_run.log`

Report exactly what happened -- three possible honest outcomes, do not assume success:
- **New territory reached**: log shows activity past the Phase 2N freeze point (the last
  `KfReleaseSpinLock` line), or a clean return, or a new distinct failure. Continue to
  Step 4.
- **Still stuck at the same point**: identical final state to the Phase 2N run. Report the
  exact log tail, do not proceed to Step 4/5's "new territory" assumptions.
- **A different busy-poll or crash**: also valid evidence -- report it exactly.

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

Do this regardless of Step 3's outcome.

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2p_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2p-stub-hits.txt; wc -l docs/superpowers/specs/phase2p-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2p-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2P verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body. This closes Phase 2P.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's entire design section (real
  handle-signal-and-notify, `r5`/`r6` untouched per the "no unevidenced register meaning"
  posture). Task 2 covers success criteria 2-4: clean build (Task 1 Step 5), honest re-run
  report (Task 2 Steps 1-3), conditional stub-hit capture (Step 5).
- **Placeholder scan:** No TBD/TODO. Step 3's three outcomes are each given concrete,
  explicit handling.
- **Type/interface consistency:** `NtSetEvent`'s use of `g_stateMutex`/`g_handleTable`/
  `g_handleSignalCv` matches their existing declarations and usage in `NtReleaseMutant`
  and `NtWaitForSingleObjectEx` exactly -- same names, same types, no new interface.
