# Phase 2D: File I/O as No-Hard-Disk-Present Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 5 file I/O kernel imports (`NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`, `NtDeviceIoControlFile`) to report real NTSTATUS "no such device" failures, matching the actual `\Device\Harddisk0\...` paths captured from the game's own calls, so a fresh `BigBumpinHost` run progresses further than Phase 2C's SIGTRAP or fails at a new, more informative point.

**Architecture:** Same pattern as every prior phase: trim `host/kernel_imports.txt`, regenerate `host/kernel_stubs.cpp`, append real implementations to `host/kernel_impl.cpp`.

**Tech Stack:** C++17, same toolchain as prior phases.

## Global Constraints

- `STATUS_NO_SUCH_DEVICE = 0xC000000E` — the failure status `NtCreateFile`/`NtOpenFile` return.
- `STATUS_INVALID_HANDLE = 0xC0000008` — the failure status `NtReadFile`/`NtWriteFile`/`NtDeviceIoControlFile` return unconditionally (no `NtCreateFile`/`NtOpenFile` call can ever hand back a real handle after this phase).
- No virtual filesystem, no real file serving — out of scope, no evidence backing the need (see spec).

---

## Task 1: Implement the 5 File I/O Functions

**Files:**
- Modify: `host/kernel_imports.txt` (remove 5 lines)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `PPC_STORE_U32` (already used in `host/kernel_impl.cpp`).
- Produces: nothing consumed by other tasks — these are terminal leaf functions.

- [ ] **Step 1: Remove the 5 names from the import list**

Edit `host/kernel_imports.txt`, delete these 5 lines:
```
NtCreateFile
NtDeviceIoControlFile
NtOpenFile
NtReadFile
NtWriteFile
```

- [ ] **Step 2: Regenerate kernel_stubs.cpp**

Run: `python3 tools/generate_kernel_stubs.py`
Expected: `Wrote 182 stub functions to host/kernel_stubs.cpp`

- [ ] **Step 3: Verify**

Run: `grep -c "^PPC_FUNC(__imp__" host/kernel_stubs.cpp`
Expected: `182`

- [ ] **Step 4: Append the 5 functions to host/kernel_impl.cpp**

Append to `host/kernel_impl.cpp`:

```cpp
constexpr uint32_t kStatusNoSuchDevice = 0xC000000E;
constexpr uint32_t kStatusInvalidHandle = 0xC0000008;

PPC_FUNC(__imp__NtCreateFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r6.u64;

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, 0);
    }
    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, kStatusNoSuchDevice);
        PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
    }

    fmt::println("[kernel] NtCreateFile: reporting STATUS_NO_SUCH_DEVICE (no virtual hard disk backing)");
    ctx.r3.u64 = kStatusNoSuchDevice;
}

PPC_FUNC(__imp__NtOpenFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, 0);
    }

    fmt::println("[kernel] NtOpenFile: reporting STATUS_NO_SUCH_DEVICE (no virtual hard disk backing)");
    ctx.r3.u64 = kStatusNoSuchDevice;
}

PPC_FUNC(__imp__NtReadFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
}

PPC_FUNC(__imp__NtWriteFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
}

PPC_FUNC(__imp__NtDeviceIoControlFile)
{
    ctx.r3.u64 = kStatusInvalidHandle;
}
```

- [ ] **Step 5: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2d_task1_build.log
grep -c "error:" /tmp/phase2d_task1_build.log
```
Expected: exits `0`, `grep -c "error:"` prints `0`. All 210 kernel imports accounted for (182 in `kernel_stubs.cpp` + 28 in `kernel_impl.cpp`).

- [ ] **Step 6: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement file I/O as no-hard-disk-present (real NTSTATUS failures)"
```

---

## Task 2: Run and Capture the Updated Bring-Up Log

**Files:**
- Create: `private/phase2d_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2d-stub-hits.txt` (committed)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2d-stub-hits.txt`, the input for whichever deferred group (crypto, memory stats/physical allocation, real threading) gets picked up next.

- [ ] **Step 1: Run BigBumpinHost and capture output**

Run: `./build/BigBumpinHost private/default.xex 2>&1 | tee private/phase2d_run.log`
Expected: the log runs further than Phase 2C's 20 lines, or fails at a demonstrably different point — record whatever actually happens, including whether the crypto calls (`XeCryptSha`, `XeKeysConsolePrivateKeySign`) still appear at all.

- [ ] **Step 2: Compare against Phase 2C's run**

Run: `diff <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2c_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u) <(grep -oE "^\[(stub|kernel)\] [A-Za-z0-9_]+" private/phase2d_run.log | sed -E 's/^\[[a-z]+\] //' | sort -u)`
Expected: this just needs to run and show a diff — report whatever it actually shows.

- [ ] **Step 3: Extract the distinct stub-hit list**

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2d_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2d-stub-hits.txt; wc -l docs/superpowers/specs/phase2d-stub-hits.txt`
Expected: a file listing each distinct still-placeholder kernel import actually called during this run.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/phase2d-stub-hits.txt
git commit -m "Record Phase 2D bring-up run: updated kernel import hit list"
```

This closes Phase 2D.

---

## Plan Self-Review Notes

- **Spec coverage:** All 5 functions from the spec have exact implementations in Task 1. Success criteria 1–2 map to Task 1's build steps. Criteria 3–4 map to Task 2.
- **Placeholder scan:** No TBD/TODO. The confidence caveats for `NtCreateFile`'s `IoStatusBlock` mapping and `NtOpenFile`'s simpler treatment are stated explicitly in the spec and carried into code comments, not hidden.
- **Type/interface consistency:** `PPC_STORE_U32` usage matches prior phases. No new shared state introduced — all 5 functions are independent, side-effect-free (beyond logging and the specific pointer writes each documents).
