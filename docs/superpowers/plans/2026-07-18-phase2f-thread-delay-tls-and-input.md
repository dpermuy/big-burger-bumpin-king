# Phase 2F: Thread Delay, TLS Read, and Input Enumeration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `KeDelayExecutionThread`, `KeTlsGetValue`, `XamInputGetCapabilities`, `XamInputGetState`, and `XamNotifyCreateListener` (5 of the 6 hit imports — `ExCreateThread` stays deferred), so a fresh `BigBumpinHost` run progresses further than Phase 2E's run or fails at a new point.

**Architecture:** Same pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append to `host/kernel_impl.cpp`. `KeTlsGetValue` reads the existing `g_tlsSlots` array (Phase 2B/2C); `XamNotifyCreateListener` uses the existing `g_handleTable`/`g_nextHandle` (Phase 2C), extending `HandleObjectType` with a third `Generic` value.

**Tech Stack:** C++17, same toolchain as prior phases.

## Global Constraints

- `ExCreateThread` is NOT part of this phase — remove it from the removal list, it stays in `host/kernel_imports.txt`/`host/kernel_stubs.cpp` as-is.
- `ERROR_DEVICE_NOT_CONNECTED = 0x48F` — the return value for both `XamInputGetCapabilities` and `XamInputGetState`, for every user index.
- `KeDelayExecutionThread` returns success immediately, no real sleep — burning the harness's 10-second watchdog budget on an unproductive wait is strictly worse than resolving it instantly under the single-execution-context model.

---

## Task 1: Implement the 5 Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 5 lines — NOT `ExCreateThread`)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (extend `HandleObjectType`, append 5 functions)

**Interfaces:**
- Consumes: `g_tlsSlots` (Phase 2B), `g_handleTable`/`g_nextHandle`/`HandleObject` (Phase 2C).
- Produces: `HandleObjectType::Generic` — a new enum value, used only by `XamNotifyCreateListener` in this phase.

- [ ] **Step 1: Remove the 5 names from the import list**

Edit `host/kernel_imports.txt`, delete these 5 lines (leave `ExCreateThread` in place):
```
KeDelayExecutionThread
KeTlsGetValue
XamInputGetCapabilities
XamInputGetState
XamNotifyCreateListener
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 175 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `175`

- [ ] **Step 4: Extend HandleObjectType**

Modify `host/kernel_impl.cpp`, changing:
```cpp
enum class HandleObjectType { Mutant, Event };
```
to:
```cpp
enum class HandleObjectType { Mutant, Event, Generic };
```

- [ ] **Step 5: Append the 5 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__KeDelayExecutionThread)
{
    // Returns immediately without sleeping -- a real sleep here would burn
    // the harness's 10s watchdog budget for no benefit, since nothing else
    // runs concurrently to change state while we'd otherwise wait. Same
    // reasoning as NtWaitForSingleObjectEx (Phase 2C).
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__KeTlsGetValue)
{
    uint32_t index = (uint32_t)ctx.r3.u64;
    ctx.r3.u64 = (index < 64) ? g_tlsSlots[index] : 0;
}

constexpr uint32_t kErrorDeviceNotConnected = 0x48F;

PPC_FUNC(__imp__XamInputGetCapabilities)
{
    ctx.r3.u64 = kErrorDeviceNotConnected;
}

PPC_FUNC(__imp__XamInputGetState)
{
    ctx.r3.u64 = kErrorDeviceNotConnected;
}

PPC_FUNC(__imp__XamNotifyCreateListener)
{
    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Generic, false };
    ctx.r3.u64 = handle;
}
```

- [ ] **Step 6: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2f_task1_build.log
grep -c "error:" /tmp/phase2f_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for (175 in `kernel_stubs.cpp` + 35 in `kernel_impl.cpp`).

- [ ] **Step 7: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement KeDelayExecutionThread, KeTlsGetValue, and input enumeration stubs"
```

---

## Task 2: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2f_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2f-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2f-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2f_run.log`
Expected: the log runs further than Phase 2E's 23 lines, or fails at a demonstrably different point.

- [ ] **Step 2: Compare against Phase 2E's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2e_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2f_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2f_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2f-stub-hits.txt; wc -l docs/superpowers/specs/phase2f-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called during this run.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2f-stub-hits.txt
git commit -m "Record Phase 2F bring-up run: updated kernel import hit list"
```

This closes Phase 2F.

---

## Plan Self-Review Notes

- **Spec coverage:** All 5 implemented functions from the spec map to Task 1. `ExCreateThread`'s continued deferral is reflected by its explicit exclusion from the removal list. Success criteria 1–2 map to Task 1's build steps. Criteria 3–4 map to Task 2.
- **Placeholder scan:** No TBD/TODO.
- **Type/interface consistency:** `HandleObjectType::Generic` is added once (Step 4) and used once (Step 5) — no other task references it. `g_tlsSlots` and `g_handleTable`/`g_nextHandle` referenced exactly as declared in Phases 2B/2C, no signature drift.
