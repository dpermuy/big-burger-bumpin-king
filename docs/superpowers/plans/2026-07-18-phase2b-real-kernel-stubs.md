# Phase 2B: Real Implementations for Hit Kernel Imports Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 11 kernel-import stubs Phase 2A's run actually exercised with real implementations, so a fresh `BigBumpinHost` run progresses further than the `KeBugCheck` crash loop (or fails at a new, more informative point) and produces a fresh evidence-based worklist for Phase 2C.

**Architecture:** `host/kernel_imports.txt` (the source-of-truth list Phase 2A's generator reads) loses the 11 now-implemented names; regenerating from it shrinks `host/kernel_stubs.cpp` accordingly. The 11 real implementations move to a new `host/kernel_impl.cpp`, keeping "still a mechanical placeholder" and "real hand-written behavior" in separate files.

**Tech Stack:** C++17, same toolchain as prior phases (CMake+Ninja, Homebrew LLVM Clang).

## Global Constraints

- `ExCreateThread` still does not spawn a real thread (Phase 2A's scope cut carries forward unchanged) — every implementation here assumes one logical execution context. No real mutex/synchronization needed anywhere in this phase.
- `KeBugCheck` and `HalReturnToFirmware` are real "never returns" kernel calls — both must terminate the process, not return control to guest code.
- `NtAllocateVirtualMemory`'s real signature on this XEX, confirmed by direct instrumented capture (not the 5-argument desktop NT signature): `BaseAddressPtr = r3` (in/out guest pointer), `RegionSizePtr = r4` (in/out guest pointer, observed value `0x100000`), `AllocationType = r5` (flags value, not a pointer — observed `0x60002000`), `Protect = r6` (observed `0x4`, matching `PAGE_READWRITE`). `r7` unused.
- `ANSI_STRING` layout (verified-by-convention, standard and unchanged since NT): `uint16 Length` at offset 0, `uint16 MaximumLength` at offset 2, `uint32 Buffer` at offset 4 — 8 bytes total.
- Critical section state (`RtlInitializeCriticalSection`/`RtlEnterCriticalSection`/`RtlLeaveCriticalSection`) is tracked in a host-side map keyed by the guest struct's address, **not** by writing into guest-memory struct fields — the exact byte layout of Xbox 360's `RTL_CRITICAL_SECTION` isn't independently confirmed the way `ANSI_STRING` and the `NtAllocateVirtualMemory` signature are, and guessing wrong would silently corrupt guest memory instead of failing loudly. Revisit only if a future run shows guest code inspecting the struct fields directly (no evidence of that yet).

---

## Task 1: Implement the 10 High-Confidence Kernel Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 10 lines)
- Create: `host/kernel_impl.cpp`
- Modify: `host/kernel_stubs.cpp` (regenerated, not hand-edited)
- Modify: `CMakeLists.txt` (add `host/kernel_impl.cpp` to `BigBumpinHost`'s sources)

**Interfaces:**
- Consumes: `PPC_FUNC`, `PPC_LOAD_U8`, `PPC_STORE_U16`, `PPC_STORE_U32` macros (`thirdparty/XenonRecomp/XenonUtils/ppc_context.h`); `PPCContext.r3`/`r4` register fields.
- Produces: `PPC_FUNC(__imp__X)` definitions for `KeBugCheck`, `HalReturnToFirmware`, `KeGetCurrentProcessType`, `RtlInitAnsiString`, `RtlInitializeCriticalSection`, `RtlEnterCriticalSection`, `RtlLeaveCriticalSection`, `KeTlsAlloc`, `KeTlsSetValue`, `XexCheckExecutablePrivilege` — consumed by the linker when building `BigBumpinHost`, no other task depends on their internals.

- [ ] **Step 1: Remove the 10 implemented names from the import list**

Edit `host/kernel_imports.txt`, deleting these 10 lines (leave `NtAllocateVirtualMemory` — that's Task 2):
```
HalReturnToFirmware
KeBugCheck
KeGetCurrentProcessType
KeTlsAlloc
KeTlsSetValue
RtlEnterCriticalSection
RtlInitAnsiString
RtlInitializeCriticalSection
RtlLeaveCriticalSection
XexCheckExecutablePrivilege
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 200 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify the 10 names are gone from the stub file**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `200`

- [ ] **Step 4: Write host/kernel_impl.cpp**

```cpp
#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

#include <cstdint>
#include <cstdlib>
#include <unordered_map>

PPC_FUNC(__imp__KeBugCheck)
{
    fmt::println("[kernel] KeBugCheck: code=0x{:X} -- halting (real kernel never returns from this)", ctx.r3.u64);
    std::_Exit(3);
}

PPC_FUNC(__imp__HalReturnToFirmware)
{
    fmt::println("[kernel] HalReturnToFirmware: routine=0x{:X} -- halting (real kernel never returns from this)", ctx.r3.u64);
    std::_Exit(4);
}

PPC_FUNC(__imp__KeGetCurrentProcessType)
{
    ctx.r3.u64 = 1; // PROCTYPE_TITLE
}

PPC_FUNC(__imp__RtlInitAnsiString)
{
    uint32_t destAddr = (uint32_t)ctx.r3.u64;
    uint32_t sourceAddr = (uint32_t)ctx.r4.u64;

    uint16_t length = 0;
    if (sourceAddr != 0)
    {
        while (PPC_LOAD_U8(sourceAddr + length) != 0)
        {
            length++;
        }
    }

    PPC_STORE_U16(destAddr + 0, length);
    PPC_STORE_U16(destAddr + 2, sourceAddr != 0 ? (uint16_t)(length + 1) : (uint16_t)0);
    PPC_STORE_U32(destAddr + 4, sourceAddr);
}

struct CriticalSectionState
{
    int32_t recursionCount = 0;
    uint32_t owningThread = 0;
};

static std::unordered_map<uint32_t, CriticalSectionState> g_criticalSections;

PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    g_criticalSections[csAddr] = CriticalSectionState{};
}

PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount++;
    state.owningThread = 1; // single logical execution context; no real guest thread ID yet
}

PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    auto& state = g_criticalSections[csAddr];
    state.recursionCount--;
    if (state.recursionCount <= 0)
    {
        state.owningThread = 0;
    }
}

static uint64_t g_tlsSlots[64] = {};
static int g_tlsNextSlot = 0;

PPC_FUNC(__imp__KeTlsAlloc)
{
    if (g_tlsNextSlot >= 64)
    {
        fmt::println("[kernel] KeTlsAlloc: out of TLS slots (64 max)");
        ctx.r3.u64 = 0xFFFFFFFF; // TLS_OUT_OF_INDEXES
        return;
    }
    ctx.r3.u64 = (uint64_t)g_tlsNextSlot++;
}

PPC_FUNC(__imp__KeTlsSetValue)
{
    uint32_t index = (uint32_t)ctx.r3.u64;
    uint64_t value = ctx.r4.u64;
    if (index < 64)
    {
        g_tlsSlots[index] = value;
    }
    ctx.r3.u64 = 1; // success
}

PPC_FUNC(__imp__XexCheckExecutablePrivilege)
{
    // Always grant -- no privilege-flag parser yet, and this is a personal
    // single-player backup, not a scenario with real DRM/multiplayer stakes.
    // Revisit only if a specific privilege check turns out to gate behavior
    // we actually want disabled.
    ctx.r3.u64 = 1;
}
```

- [ ] **Step 5: Wire kernel_impl.cpp into the BigBumpinHost target**

Modify `CMakeLists.txt`, changing:
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp)
```
to:
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp)
```

- [ ] **Step 6: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2b_task1_build.log
grep -c "error:" /tmp/phase2b_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. (`NtAllocateVirtualMemory` is still an undefined symbol at this point if the linker fully resolves — it isn't, because it's still present in `host/kernel_imports.txt` and thus still generated into `host/kernel_stubs.cpp` until Task 2 removes it. Confirm no unexpected undefined symbols beyond what Task 2 will address.)

- [ ] **Step 7: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp CMakeLists.txt
git commit -m "Implement 10 high-confidence kernel stubs with real behavior"
```

---

## Task 2: Implement NtAllocateVirtualMemory

**Files:**
- Modify: `host/kernel_imports.txt` (remove 1 line)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (add the function)

**Interfaces:**
- Consumes: `PPC_LOAD_U32`, `PPC_STORE_U32` macros; the confirmed register mapping from Global Constraints.
- Produces: `PPC_FUNC(__imp__NtAllocateVirtualMemory)`, a bump allocator whose state (`g_bumpAllocatorNext`) is private to this translation unit — no other task touches it.

- [ ] **Step 1: Remove NtAllocateVirtualMemory from the import list**

Edit `host/kernel_imports.txt`, delete the line `NtAllocateVirtualMemory`.

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 199 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `199`

- [ ] **Step 4: Add the bump allocator to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
static uint32_t g_bumpAllocatorNext = 0xA0000000;

PPC_FUNC(__imp__NtAllocateVirtualMemory)
{
    uint32_t baseAddressPtr = (uint32_t)ctx.r3.u64;
    uint32_t regionSizePtr = (uint32_t)ctx.r4.u64;
    uint32_t regionSize = PPC_LOAD_U32(regionSizePtr);

    constexpr uint32_t kAllocGranularity = 0x10000; // 64 KiB, Xbox 360 allocation granularity
    uint32_t alignedSize = (regionSize + (kAllocGranularity - 1)) & ~(kAllocGranularity - 1);

    uint32_t allocatedAddress = g_bumpAllocatorNext;
    g_bumpAllocatorNext += alignedSize;

    PPC_STORE_U32(baseAddressPtr, allocatedAddress);
    PPC_STORE_U32(regionSizePtr, alignedSize);

    fmt::println("[kernel] NtAllocateVirtualMemory: {} bytes -> 0x{:X}", alignedSize, allocatedAddress);

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
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2b_task2_build.log
grep -c "error:" /tmp/phase2b_task2_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports are now accounted for (199 in `kernel_stubs.cpp` + 11 in `kernel_impl.cpp`) — this is the first build in Phase 2B with zero undefined-symbol risk from either file.

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement NtAllocateVirtualMemory as a bump allocator"
```

---

## Task 3: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2b_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2b-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 2), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2b-stub-hits.txt`, Phase 2C's evidence-based worklist.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2b_run.log`
Expected: the log contains `Guest memory image ready: ...`, `Calling _xstart...`, at least one `[kernel] NtAllocateVirtualMemory: ...` line (confirming the allocator actually gets exercised), and terminates via clean return, watchdog timeout, `[kernel] KeBugCheck: ...`/`[kernel] HalReturnToFirmware: ...` (both now genuinely halting instead of returning), or a crash.

- [ ] **Step 2: Compare against Phase 2A's run**

Run: `diff <(grep -oE "^\[(stub|kernel|investigate)\] [A-Za-z0-9_]+" private/phase2a_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2b_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — record what changed (new stub names appearing that Phase 2A never reached is the signal this task exists to produce; report whatever the actual diff shows, don't assume a specific outcome).

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2b_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2b-stub-hits.txt; wc -l docs/superpowers/specs/phase2b-stub-hits.txt`
Expected: a file listing each distinct *still-placeholder* kernel import actually called during this run (the 11 now-implemented functions won't appear here since they're no longer `[stub]`-tagged — they log as `[kernel]` now).

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2b-stub-hits.txt
git commit -m "Record Phase 2B bring-up run: updated kernel import hit list"
```

This closes Phase 2B. `docs/superpowers/specs/phase2b-stub-hits.txt` is Phase 2C's starting input.

---

## Plan Self-Review Notes

- **Spec coverage:** All 11 functions from the spec's per-function design section have exact implementations in Task 1 or Task 2. Success criteria 1–2 (replace all 11, build clean) map to Task 1 Step 6/7 + Task 2 Step 5/6. Criterion 3 (progress or new informative failure) maps to Task 3 Steps 1–2. Criterion 4 (log saved, new worklist extracted) maps to Task 3 Steps 3–4.
- **Placeholder scan:** No TBD/TODO. The `NtAllocateVirtualMemory` register mapping — the one piece of real uncertainty going into this plan — was resolved empirically before writing this plan (two independent pointer dereferences captured: `r3`→`0x0`, `r4`→`0x100000`), not left as an in-plan investigation step, so Task 2 is a normal implement-and-verify task like every other.
- **Type/interface consistency:** `PPC_LOAD_U8`, `PPC_LOAD_U32`, `PPC_STORE_U16`, `PPC_STORE_U32` usage matches their macro definitions in `ppc_context.h` (checked during Phase 2A's plan and reused identically here — no new macro assumptions introduced). `g_criticalSections`, `g_tlsSlots`/`g_tlsNextSlot`, and `g_bumpAllocatorNext` are each scoped to a single file/task and not referenced elsewhere, so no cross-task naming mismatch is possible.
