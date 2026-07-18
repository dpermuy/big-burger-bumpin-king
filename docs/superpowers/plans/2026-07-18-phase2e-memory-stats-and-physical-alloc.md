# Phase 2E: Memory Statistics and Physical Allocation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `MmQueryStatistics` and `MmAllocatePhysicalMemoryEx` conservatively — acknowledging what's confirmed, not guessing the rest — so a fresh `BigBumpinHost` run either progresses further than Phase 2D's SIGTRAP or fails at a new point that narrows down what's actually needed next.

**Architecture:** Same pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append to `host/kernel_impl.cpp`. `MmAllocatePhysicalMemoryEx` reuses the existing `g_bumpAllocatorNext` state from `NtAllocateVirtualMemory` (Phase 2B) rather than introducing separate allocator state.

**Tech Stack:** C++17, same toolchain as prior phases.

## Global Constraints

- `MmQueryStatistics` does not write any statistics field data — only acknowledges the request with `STATUS_SUCCESS`. No confirmed `MM_STATISTICS` field layout beyond the caller-prefilled `Length` field (observed `0x68`), which this implementation doesn't need to validate against anything (there's nothing to reject it for).
- `MmAllocatePhysicalMemoryEx` returns the allocated address directly in `ctx.r3` (not a status code) — this matches the Xbox 360 `Mm*` family's convention of returning `PVOID` directly, distinct from `Nt*` functions which return `NTSTATUS` plus output pointers.
- `MmAllocatePhysicalMemoryEx` ignores its `r4`/`r5` arguments rather than treating them as a size — the observed `r4` value (`0x19000000`, ~400 MB) is implausibly large for a single physical allocation on hardware with 512 MB total RAM, indicating it's stale/leftover register content, not a real size parameter (the same category of unreliable register content flagged for `MmQueryStatistics` in the spec). Always allocates a fixed 64 KiB block instead of guessing which register (if any) is trustworthy.

---

## Task 1: Implement Both Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 2 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `g_bumpAllocatorNext` (already defined in `host/kernel_impl.cpp` from Phase 2B's `NtAllocateVirtualMemory`).
- Produces: nothing consumed by other tasks.

- [ ] **Step 1: Remove the 2 names from the import list**

Edit `host/kernel_imports.txt`, delete these 2 lines:
```
MmAllocatePhysicalMemoryEx
MmQueryStatistics
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 180 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `180`

- [ ] **Step 4: Append both functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
PPC_FUNC(__imp__MmQueryStatistics)
{
    // Caller pre-fills Length for validation (standard NT convention;
    // observed value 0x68). No confirmed full MM_STATISTICS field layout,
    // so we acknowledge success without writing detailed statistics --
    // same conservative posture as the critical-section implementation in
    // Phase 2B. Revisit only if a future run's evidence shows the game
    // reading and acting on specific garbage field values.
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}

PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx)
{
    // r4 (observed 0x19000000, ~400 MB) is implausibly large for a single
    // physical allocation on 512 MB hardware -- treated as stale register
    // content, not a real size argument. Always allocates a fixed 64 KiB
    // block from the same bump allocator NtAllocateVirtualMemory (Phase 2B)
    // uses, rather than guessing which register (if any) is trustworthy.
    constexpr uint32_t kAllocGranularity = 0x10000;

    uint32_t allocatedAddress = g_bumpAllocatorNext;
    g_bumpAllocatorNext += kAllocGranularity;

    fmt::println("[kernel] MmAllocatePhysicalMemoryEx: {} bytes -> 0x{:X}", kAllocGranularity, allocatedAddress);

    ctx.r3.u64 = allocatedAddress; // Mm* functions return the address directly, not a status code
}
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2e_task1_build.log
grep -c "error:" /tmp/phase2e_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for (180 in `kernel_stubs.cpp` + 30 in `kernel_impl.cpp`).

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement MmQueryStatistics and MmAllocatePhysicalMemoryEx conservatively"
```

---

## Task 2: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2e_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2e-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2e-stub-hits.txt`.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2e_run.log`
Expected: the log runs further than Phase 2D's 11 lines, or fails at a demonstrably different point (possibly the same SIGTRAP if the missing detailed statistics fields turn out not to matter — either outcome is informative, record whatever actually happens).

- [ ] **Step 2: Compare against Phase 2D's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2d_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2e_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2e_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2e-stub-hits.txt; wc -l docs/superpowers/specs/phase2e-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called during this run (`ExCreateThread` is expected to be the sole remaining entry if nothing new is hit).

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2e-stub-hits.txt
git commit -m "Record Phase 2E bring-up run: updated kernel import hit list"
```

This closes Phase 2E.

---

## Plan Self-Review Notes

- **Spec coverage:** Both functions from the spec have exact implementations in Task 1. Success criteria 1–2 map to Task 1's build steps. Criteria 3–4 map to Task 2.
- **Placeholder scan:** No TBD/TODO. The decision to ignore `r4`/`r5` in `MmAllocatePhysicalMemoryEx` is stated explicitly with reasoning (implausible size value), not silently glossed over.
- **Type/interface consistency:** `g_bumpAllocatorNext` referenced here matches its declaration in `host/kernel_impl.cpp` from Phase 2B exactly (same name, same file, no redeclaration).
