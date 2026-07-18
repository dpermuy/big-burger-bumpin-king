# Phase 2G: IRQL, Spinlocks, and Object References Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 11 non-`ExCreateThread` imports Phase 2F's run hit, so a fresh `BigBumpinHost` run's behavior around the watchdog timeout can be re-confirmed with these no longer stubbed.

**Architecture:** Same pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append to `host/kernel_impl.cpp`.

**Tech Stack:** C++17, same toolchain as prior phases.

## Global Constraints

- `ExCreateThread` is NOT part of this phase — stays in `host/kernel_imports.txt`/`host/kernel_stubs.cpp` as-is.
- `ObReferenceObjectByHandle` writes a fixed sentinel (`0xDEAD0001`) to its object-out pointer (`r5`) — this value is read back and reused by `KeSetBasePriorityThread`/`KeResumeThread`/`ObDereferenceObject` per traced log data flow, so it must be written consistently, not omitted.
- Every other function in this phase is a no-op (or returns a fixed trivial value) — none of them are on the suspected `ExCreateThread`-related hang path, so this phase is not expected to change the watchdog-timeout outcome, but that must be confirmed by running, not assumed.

---

## Task 1: Implement the 11 Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 11 lines — NOT `ExCreateThread`)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_STORE_U32` (already used in `host/kernel_impl.cpp`).
- Produces: nothing consumed by other tasks.

- [ ] **Step 1: Remove the 11 names from the import list**

Edit `host/kernel_imports.txt`, delete these 11 lines (leave `ExCreateThread` in place):
```
ExRegisterTitleTerminateNotification
KeAcquireSpinLockAtRaisedIrql
KeInitializeSemaphore
KeRaiseIrqlToDpcLevel
KeReleaseSpinLockFromRaisedIrql
KeResumeThread
KeSetBasePriorityThread
KfLowerIrql
ObDereferenceObject
ObReferenceObjectByHandle
XAudioRegisterRenderDriverClient
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 164 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `164`

- [ ] **Step 4: Append the 11 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__KeRaiseIrqlToDpcLevel)
{
    ctx.r3.u64 = 0; // old IRQL; no real interrupt masking under single-execution-context
}

PPC_FUNC(__imp__KfLowerIrql)
{
    // No-op -- argument (new IRQL) ignored, no real IRQL state tracked.
}

PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql)
{
    // No-op -- nothing can ever contend under single-execution-context, same
    // reasoning as critical sections (Phase 2B).
}

PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql)
{
    // No-op.
}

PPC_FUNC(__imp__ObReferenceObjectByHandle)
{
    uint32_t objectOutPtr = (uint32_t)ctx.r5.u64;

    // Fixed sentinel value -- the log shows this gets read back and reused
    // as the "thread object" argument by KeSetBasePriorityThread/
    // KeResumeThread/ObDereferenceObject, so it needs to be consistent, not
    // that it needs to point at a real object (nothing dereferences it).
    constexpr uint32_t kFakeObjectSentinel = 0xDEAD0001;

    if (objectOutPtr != 0)
    {
        PPC_STORE_U32(objectOutPtr, kFakeObjectSentinel);
    }

    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__ObDereferenceObject)
{
    // No-op -- no real reference counting exists.
}

PPC_FUNC(__imp__KeSetBasePriorityThread)
{
    // No-op -- no real thread scheduling exists.
}

PPC_FUNC(__imp__KeResumeThread)
{
    ctx.r3.u64 = 0; // previous suspend count
}

PPC_FUNC(__imp__KeInitializeSemaphore)
{
    // No-op -- nothing observed waits on or releases this semaphore.
}

PPC_FUNC(__imp__ExRegisterTitleTerminateNotification)
{
    // No-op -- no evidence the registered notification needs to fire.
}

PPC_FUNC(__imp__XAudioRegisterRenderDriverClient)
{
    // No real audio driver semantics -- just unblocks the boot sequence.
    // Real audio is its own future phase.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2g_task1_build.log
grep -c "error:" /tmp/phase2g_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for (164 in `kernel_stubs.cpp` + 46 in `kernel_impl.cpp`).

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement IRQL, spinlock, and object-reference kernel stubs"
```

---

## Task 2: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2g_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2g-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2g-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2g_run.log`
Expected: per the spec, most likely the same watchdog timeout as Phase 2F (these 11
functions aren't on the suspected `ExCreateThread` hang path) — but record whatever
actually happens. If it progresses further or fails differently, that's valuable evidence
to bring into Phase 2H's investigation, not something to explain away.

- [ ] **Step 2: Compare against Phase 2F's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2f_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2g_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2g_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2g-stub-hits.txt; wc -l docs/superpowers/specs/phase2g-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called
during this run. `ExCreateThread` alone is the expected (not guaranteed) outcome.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2g-stub-hits.txt
git commit -m "Record Phase 2G bring-up run: updated kernel import hit list"
```

This closes Phase 2G.

---

## Plan Self-Review Notes

- **Spec coverage:** All 11 functions from the spec map to Task 1. `ExCreateThread`'s continued exclusion is explicit in the removal list. Success criteria 1–2 map to Task 1's build steps. Criteria 3–4 map to Task 2, including the explicit instruction not to assume the timeout outcome.
- **Placeholder scan:** No TBD/TODO.
- **Type/interface consistency:** `ObReferenceObjectByHandle`'s sentinel value and `PPC_STORE_U32` usage match established macro definitions from prior phases. No new shared state introduced.
