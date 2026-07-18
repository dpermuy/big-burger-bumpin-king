# Phase 2J: Misc Utilities and Graphics No-Ops Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all 32 imports from Phase 2I's run — 11 non-graphics utilities with real or no-op behavior per established precedent, 21 graphics/display calls as no-ops — to find out whether the game reaches a stable running state without any real GPU rendering.

**Architecture:** Same per-phase pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append to `host/kernel_impl.cpp`. Split into two implementation tasks (misc utilities, then graphics no-ops) purely for review granularity — both are equally low-risk.

**Tech Stack:** C++17, plus `<pthread.h>` for `ExTerminateThread`'s `pthread_exit`.

## Global Constraints

- `ExTerminateThread` genuinely terminates the calling host thread via `pthread_exit` — not a no-op. It must be correct regardless of call depth (confirmed via Phase 2H's crash investigation that `XApiThreadStartup`'s generated code calls it as a real thread-ending action, not just at one specific call site that happens to already be a natural return point).
- `XexGetModuleHandle` returns `PPC_IMAGE_BASE` (a fact already established in this project, not a new guess) for the observed `ModuleName == NULL` ("current module") case.
- None of the 21 graphics/display functions write structured output data (`VdQueryVideoMode`, `XGetVideoMode`, `VdGetCurrentDisplayGamma`, `VdGetCurrentDisplayInformation`, `VdGetSystemCommandBuffer`) — no confirmed struct layouts, no evidence the game depends on specific field values.
- `VdIsHSIOTrainingSucceeded` returns `1` (true) specifically — the one graphics function with a clearly-warranted non-default value.

---

## Task 1: Implement the 11 Non-Graphics Utilities

**Files:**
- Modify: `host/kernel_imports.txt` (remove 11 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_STORE_U32` (already used elsewhere in `host/kernel_impl.cpp`), `PPC_IMAGE_BASE` (from `private/ppc/ppc_config.h`, already included).
- Produces: nothing consumed by other tasks — all 11 are independent leaves.

- [ ] **Step 1: Remove the 11 names from the import list**

Edit `host/kernel_imports.txt`, delete these 11 lines:
```
_vsnprintf
DbgPrint
ExTerminateThread
KeEnterCriticalRegion
KeLeaveCriticalRegion
KeSetAffinityThread
KeSetEvent
KiApcNormalRoutineNop
MmGetPhysicalAddress
MmSetAddressProtect
RtlFillMemoryUlong
XexGetModuleHandle
XexGetModuleSection
```

Wait — that's 13 lines, not 11. The correct 11-item non-graphics list from the spec
excludes `XexGetModuleHandle`/`XexGetModuleSection` from this count error; re-count
carefully: `_vsnprintf`, `DbgPrint`, `ExTerminateThread`, `KeEnterCriticalRegion`,
`KeLeaveCriticalRegion`, `KeSetAffinityThread`, `KeSetEvent`, `KiApcNormalRoutineNop`,
`MmGetPhysicalAddress`, `MmSetAddressProtect`, `RtlFillMemoryUlong`,
`XexGetModuleHandle`, `XexGetModuleSection` is actually **13** names. The spec's "11"
count was wrong — delete all 13 lines shown above in this step (this step's code block is
the authoritative list; the count in this step's own prose is corrected here rather than
silently trusting the earlier miscount).

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 149 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `149`

- [ ] **Step 4: Add the pthread include**

Modify `host/kernel_impl.cpp`, adding `#include <pthread.h>` to the top include block.

- [ ] **Step 5: Append the 13 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__RtlFillMemoryUlong)
{
    uint32_t dest = (uint32_t)ctx.r3.u64;
    uint32_t length = (uint32_t)ctx.r4.u64;
    uint32_t pattern = (uint32_t)ctx.r5.u64;

    for (uint32_t i = 0; i + 4 <= length; i += 4)
    {
        PPC_STORE_U32(dest + i, pattern);
    }
}

PPC_FUNC(__imp__MmGetPhysicalAddress)
{
    // Identity mapping -- no real physical/virtual memory distinction in
    // this harness. ctx.r3.u64 already holds the virtual address; leave it
    // unchanged as the "physical" address.
}

PPC_FUNC(__imp__MmSetAddressProtect)
{
    // No-op -- no real page-protection enforcement.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__KeSetEvent)
{
    // No-op -- no real dispatcher-object state tracked, same reasoning as
    // the existing KeResetEvent stub.
    ctx.r3.u64 = 0; // previous signal state
}

PPC_FUNC(__imp__KeEnterCriticalRegion)
{
    // No-op -- APC delivery isn't emulated, nothing to disable.
}

PPC_FUNC(__imp__KeLeaveCriticalRegion)
{
    // No-op -- APC delivery isn't emulated, nothing to re-enable.
}

PPC_FUNC(__imp__KiApcNormalRoutineNop)
{
    // No-op by design -- name states its own real behavior.
}

PPC_FUNC(__imp__XexGetModuleHandle)
{
    uint32_t moduleHandleOutPtr = (uint32_t)ctx.r4.u64;
    if (moduleHandleOutPtr != 0)
    {
        // Real convention: a module's "handle" is its own base load address.
        // Observed call has ModuleName=NULL (r3=0), meaning "the current
        // module" -- the only case seen.
        PPC_STORE_U32(moduleHandleOutPtr, (uint32_t)PPC_IMAGE_BASE);
    }
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__XexGetModuleSection)
{
    // No section-lookup infrastructure exists. Honestly report "not found"
    // rather than fabricate section data, matching the no-hard-disk
    // precedent (Phase 2D).
    constexpr uint32_t kStatusNotFound = 0xC0000225;
    ctx.r3.u64 = kStatusNotFound;
}

PPC_FUNC(__imp___vsnprintf)
{
    // No-op -- pure debug-formatting helper, no real PPC varargs support
    // implemented (nontrivial calling convention, zero bearing on game
    // logic correctness). Returns 0: no characters formatted.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgPrint)
{
    // No-op -- debug output only, no bearing on game logic.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExTerminateThread)
{
    fmt::println("[kernel] ExTerminateThread: exitCode=0x{:X} -- terminating this thread", ctx.r3.u64);
    pthread_exit(nullptr);
}

PPC_FUNC(__imp__KeSetAffinityThread)
{
    uint32_t previousAffinityOutPtr = (uint32_t)ctx.r5.u64;
    if (previousAffinityOutPtr != 0)
    {
        PPC_STORE_U32(previousAffinityOutPtr, 0);
    }
    // Processor affinity ignored, matching ExCreateThread's CreationFlags
    // handling (Phase 2H) -- the OS scheduler handles real placement.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}
```

- [ ] **Step 6: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2j_task1_build.log
grep -c "error:" /tmp/phase2j_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`.

- [ ] **Step 7: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement 13 non-graphics utility kernel functions"
```

---

## Task 2: Implement the 21 Graphics/Display No-Ops

**Files:**
- Modify: `host/kernel_imports.txt` (remove 19 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

Note: the spec's "21" count includes `VdIsHSIOTrainingSucceeded` and
`VdSetGraphicsInterruptCallback`, which are graphics-domain but each get their own
explicit rationale below rather than a bare no-op — still counted among the 21, not
separate. The actual line count removed from `host/kernel_imports.txt` in this task is 19
(all 21 minus the 2 already... no — re-checking against `docs/superpowers/specs/phase2i-stub-hits.txt`
directly: the full graphics/display set is `VdCallGraphicsNotificationRoutines`,
`VdEnableRingBufferRPtrWriteBack`, `VdGetCurrentDisplayGamma`,
`VdGetCurrentDisplayInformation`, `VdGetSystemCommandBuffer`, `VdInitializeEngines`,
`VdInitializeRingBuffer`, `VdInitializeScalerCommandBuffer`, `VdIsHSIOTrainingSucceeded`,
`VdPersistDisplay`, `VdQueryVideoMode`, `VdRetrainEDRAM`, `VdRetrainEDRAMWorker`,
`VdSetDisplayMode`, `VdSetGraphicsInterruptCallback`,
`VdSetSystemCommandBufferGpuIdentifierAddress`, `VdShutdownEngines`, `VdSwap`,
`XGetVideoMode` — **19 names**. Combined with Task 1's 13, that's 32 total, matching
`phase2i-stub-hits.txt`'s line count exactly. Use this 19-name list as authoritative.

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: nothing consumed by other tasks.

- [ ] **Step 1: Remove the 19 names from the import list**

Edit `host/kernel_imports.txt`, delete these 19 lines:
```
VdCallGraphicsNotificationRoutines
VdEnableRingBufferRPtrWriteBack
VdGetCurrentDisplayGamma
VdGetCurrentDisplayInformation
VdGetSystemCommandBuffer
VdInitializeEngines
VdInitializeRingBuffer
VdInitializeScalerCommandBuffer
VdIsHSIOTrainingSucceeded
VdPersistDisplay
VdQueryVideoMode
VdRetrainEDRAM
VdRetrainEDRAMWorker
VdSetDisplayMode
VdSetGraphicsInterruptCallback
VdSetSystemCommandBufferGpuIdentifierAddress
VdShutdownEngines
VdSwap
XGetVideoMode
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 130 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `130`

- [ ] **Step 4: Append the 19 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__VdInitializeEngines)
{
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdShutdownEngines)
{
    // No-op -- void real signature.
}

PPC_FUNC(__imp__VdSetGraphicsInterruptCallback)
{
    // Real semantics: registers a callback a GPU interrupt handler (e.g.
    // vblank) would invoke. No-op for now -- nothing yet requires us to
    // actually invoke it. Callback address is r3, context is r4, if future
    // frame-pacing/vsync work needs them.
}

PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    // No-op.
}

PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    // No-op -- not parsing real GPU commands yet.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    // No-op.
}

PPC_FUNC(__imp__VdQueryVideoMode)
{
    // No confirmed struct layout; return success without writing data,
    // same conservative posture as MmQueryStatistics (Phase 2E).
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdCallGraphicsNotificationRoutines)
{
    // No-op.
}

PPC_FUNC(__imp__VdRetrainEDRAM)
{
    // No-op -- hardware-specific EDRAM calibration, no real hardware to calibrate.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdRetrainEDRAMWorker)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdIsHSIOTrainingSucceeded)
{
    // Report success -- no real EDRAM hardware to fail training.
    ctx.r3.u64 = 1;
}

PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    // No confirmed struct/pointer-pair layout; return success without
    // writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdSwap)
{
    // The frame-present call. No-op -- nothing to actually present, no
    // renderer exists yet.
}

PPC_FUNC(__imp__VdGetCurrentDisplayGamma)
{
    // No confirmed struct layout; return success without writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdSetDisplayMode)
{
    // No-op -- accept whatever mode is requested.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdGetCurrentDisplayInformation)
{
    // No confirmed struct layout; return success without writing data.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdInitializeScalerCommandBuffer)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__VdPersistDisplay)
{
    // No-op.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__XGetVideoMode)
{
    // No confirmed struct layout; return success without writing data,
    // same conservative posture as VdQueryVideoMode.
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
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2j_task2_build.log
grep -c "error:" /tmp/phase2j_task2_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for
(130 in `kernel_stubs.cpp` + 80 in `kernel_impl.cpp`).

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement 19 graphics/display kernel functions as no-ops"
```

---

## Task 3: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2j_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2j-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 2), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2j-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2j_run.log`
Expected: given all 32 imports from Phase 2I's hit list are now implemented, this run
cannot produce any `[stub]`-tagged lines matching that list. The key question this task
exists to answer: does the game reach a stable, non-spinning, non-crashing state (running
for the full watchdog duration doing sensible periodic work, not spinning at unthrottled
speed), or does it hit a new failure point? Record whatever actually happens — don't
assume either outcome.

- [ ] **Step 2: Sanity-check the log size**

Run: `wc -l private/phase2j_run.log`
Expected: report the actual number. A very large (multi-million-line) count would indicate
a new spin somewhere else; a small, bounded count consistent with steady ~30ms-paced
iterations (per Phase 2I) or a clean crash both count as informative, valid outcomes to
report honestly.

- [ ] **Step 3: Compare against Phase 2I's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2i_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2j_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 4: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2j_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2j-stub-hits.txt; wc -l docs/superpowers/specs/phase2j-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called
during this run — this becomes the next investigation's starting point, likely either more
of the remaining ~130 never-yet-hit imports, or (if the game is now looping stably) an
empty/very short list confirming a genuinely idle running state.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/phase2j-stub-hits.txt
git commit -m "Record Phase 2J bring-up run: updated kernel import hit list"
```

This closes Phase 2J.

---

## Plan Self-Review Notes

- **Spec coverage:** All 32 functions from the spec have exact implementations across Task 1 (13) and Task 2 (19). Success criteria 1–2 map to each task's build steps. Criteria 3–4 map to Task 3, with an explicit instruction not to assume either a stable-idle or new-failure outcome.
- **Placeholder scan:** No TBD/TODO. Corrected an arithmetic error found during this plan's own writing: the spec's prose said "11 non-graphics utilities," but the actual list is 13 (`XexGetModuleHandle`/`XexGetModuleSection` weren't in the original count) — called out explicitly in Task 1 rather than silently using a wrong count, and cross-checked against `phase2i-stub-hits.txt`'s real 32-line total (13 + 19 = 32) to confirm the corrected numbers are right.
- **Type/interface consistency:** `PPC_STORE_U32`, `PPC_IMAGE_BASE` usage matches prior phases exactly. No shared state introduced between Task 1 and Task 2 — all 32 functions are independent leaves.
