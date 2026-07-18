# Phase 2C: Handle Table and Simple System Stubs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 12 of the 22 kernel imports Phase 2B's run hit — a host-side handle table backing 5 sync-related functions, plus 7 low-risk system-info stubs — so a fresh `BigBumpinHost` run progresses further than Phase 2B's SIGTRAP or fails at a new, more informative point.

**Architecture:** Same pattern as Phase 2B: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, add real implementations to the existing `host/kernel_impl.cpp`. The 5 handle-related functions share a single new host-side handle table declared in that file.

**Tech Stack:** C++17, same toolchain as prior phases.

## Global Constraints

- `ExCreateThread` stays untouched (still Phase 2A's do-nothing stub) — not part of this phase.
- `NtWaitForSingleObjectEx` returns success immediately and unconditionally, without checking the handle's signaled state — correct under the current single-execution-context model, not a shortcut. This assumption must be revisited when real thread spawning is eventually implemented.
- `RtlNtStatusToDosError`'s mapping only needs to cover `STATUS_OBJECT_NAME_NOT_FOUND` (`0xC0000034`) → `ERROR_FILE_NOT_FOUND` (`2`), the one value actually observed; everything else falls back to `ERROR_GEN_FAILURE` (`31`). Don't pre-build a larger mapping table.
- `KeQueryPerformanceFrequency` returns `50000000` (Xbox 360's documented hardware timebase frequency).
- `RtlTimeToTimeFields`'s `TIME_FIELDS` output layout: 8 packed `int16` fields in this order — `Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday` (offsets 0, 2, 4, 6, 8, 10, 12, 14).

---

## Task 1: Handle Table and 5 Sync Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 5 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_STORE_U32` (already used in `host/kernel_impl.cpp` from Phase 2B).
- Produces: `g_handleTable`, `g_nextHandle`, `HandleObject`, `HandleObjectType` — private to `host/kernel_impl.cpp`, not referenced by any other task or file.

- [ ] **Step 1: Remove the 5 handle-related names from the import list**

Edit `host/kernel_imports.txt`, delete these 5 lines:
```
NtClose
NtCreateEvent
NtCreateMutant
NtReleaseMutant
NtWaitForSingleObjectEx
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 194 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `194`

- [ ] **Step 4: Append the handle table and 5 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp` (after the existing `NtAllocateVirtualMemory` function from Phase 2B):

```cpp
enum class HandleObjectType { Mutant, Event };

struct HandleObject
{
    HandleObjectType type;
    bool signaled;
};

static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels

PPC_FUNC(__imp__NtCreateMutant)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialOwner = ctx.r5.u64 != 0;

    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Mutant, !initialOwner };

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__NtCreateEvent)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    bool initialState = ctx.r6.u64 != 0;

    uint32_t handle = g_nextHandle++;
    g_handleTable[handle] = HandleObject{ HandleObjectType::Event, initialState };

    PPC_STORE_U32(handleOutPtr, handle);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtReleaseMutant)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t previousCountOutPtr = (uint32_t)ctx.r4.u64;

    auto it = g_handleTable.find(handle);
    if (it != g_handleTable.end())
    {
        it->second.signaled = true;
    }

    if (previousCountOutPtr != 0)
    {
        PPC_STORE_U32(previousCountOutPtr, 0);
    }

    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    // Always succeeds immediately -- correct, not just convenient, under a
    // single-execution-context model: nothing else can run concurrently to
    // change an object's state while we'd otherwise block. Revisit once
    // ExCreateThread spawns real host threads.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtClose)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    g_handleTable.erase(handle);
    ctx.r3.u64 = 0;
}
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2c_task1_build.log
grep -c "error:" /tmp/phase2c_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`.

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement handle table and 5 sync-related kernel functions"
```

---

## Task 2: 7 Simple System-Info Stubs

**Files:**
- Modify: `host/kernel_imports.txt` (remove 7 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_STORE_U8`, `PPC_STORE_U16`, `PPC_STORE_U64`, `PPC_LOAD_U64` macros; `<ctime>` (`time`, `gmtime_r`, `std::tm`).
- Produces: nothing consumed by other tasks — these 7 functions are independent leaves.

- [ ] **Step 1: Remove the 7 names from the import list**

Edit `host/kernel_imports.txt`, delete these 7 lines:
```
ExGetXConfigSetting
KeEnableFpuExceptions
KeQueryPerformanceFrequency
KeQuerySystemTime
RtlNtStatusToDosError
RtlTimeToTimeFields
XGetAVPack
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 187 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `187`

- [ ] **Step 4: Add the ctime include**

Modify `host/kernel_impl.cpp`, adding `#include <ctime>` to the top include block (alongside the existing `<cstdint>`, `<cstdlib>`, `<unordered_map>`).

- [ ] **Step 5: Append the 7 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__XGetAVPack)
{
    // Real AV-pack enum value not independently confirmed; 0 is a
    // deliberate low-confidence default, not an asserted-correct constant.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExGetXConfigSetting)
{
    uint32_t bufferPtr = (uint32_t)ctx.r5.u64;
    uint32_t bufferSize = (uint32_t)ctx.r6.u64;

    for (uint32_t i = 0; i < bufferSize; i++)
    {
        PPC_STORE_U8(bufferPtr + i, 0);
    }

    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeQuerySystemTime)
{
    uint32_t outPtr = (uint32_t)ctx.r3.u64;

    // Windows FILETIME: 100ns ticks since 1601-01-01. Unix epoch (1970-01-01)
    // is 11644473600 seconds after that.
    constexpr uint64_t kFiletimeEpochOffsetSeconds = 11644473600ULL;
    uint64_t unixSeconds = (uint64_t)time(nullptr);
    uint64_t filetime = (unixSeconds + kFiletimeEpochOffsetSeconds) * 10000000ULL;

    PPC_STORE_U64(outPtr, filetime);
}

PPC_FUNC(__imp__KeQueryPerformanceFrequency)
{
    uint32_t outPtr = (uint32_t)ctx.r3.u64;
    PPC_STORE_U64(outPtr, 50000000ULL); // Xbox 360's documented hardware timebase frequency
}

PPC_FUNC(__imp__KeEnableFpuExceptions)
{
    // No-op -- FPU exception trapping isn't emulated.
}

PPC_FUNC(__imp__RtlNtStatusToDosError)
{
    uint32_t status = (uint32_t)ctx.r3.u64;

    constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;
    constexpr uint32_t kErrorFileNotFound = 2;
    constexpr uint32_t kErrorGenFailure = 31;

    if (status == kStatusObjectNameNotFound)
    {
        ctx.r3.u64 = kErrorFileNotFound;
    }
    else
    {
        ctx.r3.u64 = kErrorGenFailure;
    }
}

PPC_FUNC(__imp__RtlTimeToTimeFields)
{
    uint32_t inputTimePtr = (uint32_t)ctx.r3.u64;
    uint32_t outputFieldsPtr = (uint32_t)ctx.r4.u64;

    uint64_t filetime = PPC_LOAD_U64(inputTimePtr);

    constexpr uint64_t kFiletimeEpochOffsetSeconds = 11644473600ULL;
    time_t unixSeconds = (time_t)(filetime / 10000000ULL - kFiletimeEpochOffsetSeconds);
    uint32_t milliseconds = (uint32_t)((filetime / 10000ULL) % 1000ULL);

    std::tm tmValue{};
    gmtime_r(&unixSeconds, &tmValue);

    // TIME_FIELDS: Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday (8x int16)
    PPC_STORE_U16(outputFieldsPtr + 0, (uint16_t)(tmValue.tm_year + 1900));
    PPC_STORE_U16(outputFieldsPtr + 2, (uint16_t)(tmValue.tm_mon + 1));
    PPC_STORE_U16(outputFieldsPtr + 4, (uint16_t)tmValue.tm_mday);
    PPC_STORE_U16(outputFieldsPtr + 6, (uint16_t)tmValue.tm_hour);
    PPC_STORE_U16(outputFieldsPtr + 8, (uint16_t)tmValue.tm_min);
    PPC_STORE_U16(outputFieldsPtr + 10, (uint16_t)tmValue.tm_sec);
    PPC_STORE_U16(outputFieldsPtr + 12, (uint16_t)milliseconds);
    PPC_STORE_U16(outputFieldsPtr + 14, (uint16_t)tmValue.tm_wday);
}
```

- [ ] **Step 6: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2c_task2_build.log
grep -c "error:" /tmp/phase2c_task2_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for (187 in `kernel_stubs.cpp` + 23 in `kernel_impl.cpp`).

- [ ] **Step 7: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement 7 simple system-info kernel stubs"
```

---

## Task 3: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2c_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2c-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 2), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2c-stub-hits.txt`, the input for whichever deferred group (file I/O, crypto, memory stats/physical allocation, real threading) gets picked up next.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2c_run.log`
Expected: the log runs further than Phase 2B's 49 lines, or fails at a demonstrably different point — record whatever actually happens.

- [ ] **Step 2: Compare against Phase 2B's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2b_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2c_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2c_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2c-stub-hits.txt; wc -l docs/superpowers/specs/phase2c-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called during this run.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2c-stub-hits.txt
git commit -m "Record Phase 2C bring-up run: updated kernel import hit list"
```

This closes Phase 2C.

---

## Plan Self-Review Notes

- **Spec coverage:** All 12 functions from the spec have exact implementations in Task 1 or Task 2. Success criteria 1–2 map to Task 1/2's build steps. Criteria 3–4 map to Task 3.
- **Placeholder scan:** No TBD/TODO. Confidence levels for `XGetAVPack`'s return value and `RtlTimeToTimeFields`'s struct layout are stated honestly in comments, not hidden.
- **Type/interface consistency:** `g_handleTable`/`HandleObject` are defined once in Task 1 and not referenced by Task 2's independent leaf functions — no cross-task naming to keep in sync. `PPC_STORE_U8/U16/U64`, `PPC_LOAD_U64` usage matches the macro definitions already verified in `ppc_context.h` during Phase 2A's plan.
