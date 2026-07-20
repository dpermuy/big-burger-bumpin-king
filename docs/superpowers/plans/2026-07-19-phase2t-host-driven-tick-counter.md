# Phase 2T: Host-Driven Kernel Tick Counter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make guest address `0x82670100` (the pointer slot `sub_820B9A58` reads via
`r13+256`) point at a small host-owned guest-memory structure whose `+88` tick field
genuinely advances in real time, resolving the 1.1B+-iteration busy-poll found in the
Phase 2Q investigation.

**Architecture:** Single-file change to `host/main.cpp`: write the pointer during
`SetupMemoryImage`, start one detached background thread in `main()` that periodically
updates the tick field using real elapsed time.

**Tech Stack:** C++17 `<thread>` (new to this file), existing `<chrono>`.

## Global Constraints

- Only the `+88` tick field is implemented in the reserved structure -- no other offset,
  per the spec's explicit scope limit.
- The reserved guest address (`0x7FFF0000`) must not collide with the loaded XEX image
  (`0x82000600`-`0x8271A5FC`), the stack (`0x90000000`+), or the kernel bump allocator
  (`0xA0000000`+) -- confirmed clear in the spec.
- The tick thread is detached, never joined, matching the existing `ExCreateThread`
  precedent (`host/kernel_impl.cpp`).

---

## Task 1: Implement the Tick Structure, Pointer Write, and Updater Thread

**Files:**
- Modify: `host/main.cpp`

**Interfaces:**
- Produces: guest memory at `0x82670100` holding `0x7FFF0000`, and `0x7FFF0088` updating
  in real time. Not consumed by any other task -- self-contained.

- [ ] **Step 1: Add the `<thread>` include**

In `host/main.cpp`, add to the include block:
```cpp
#include <thread>
```

- [ ] **Step 2: Write the pointer slot at the end of SetupMemoryImage**

Find:
```cpp
    fmt::println("Guest memory image ready: base={}, {} sections loaded, {} function mappings installed",
        static_cast<void*>(base), image.sections.size(), mappingCount);

    return base;
}
```
Replace with:
```cpp
    fmt::println("Guest memory image ready: base={}, {} sections loaded, {} function mappings installed",
        static_cast<void*>(base), image.sections.size(), mappingCount);

    // Real Xbox 360 kernel/HAL boot code (outside any title's own executable) populates
    // this slot with a pointer to a kernel-shared tick structure before the title's
    // entry point runs. This project never emulates that boot sequence, and the raw
    // XEX's .data section genuinely contains zero here (confirmed, Phase 2S) -- so the
    // host writes it directly, pointing at a small host-owned structure (Phase 2T).
    constexpr uint32_t kKernelTickStructAddr = 0x7FFF0000;
    constexpr uint32_t kKernelTickPointerSlot = 0x82670100; // r13(0x82670000) + 256
    PPC_STORE_U32(kKernelTickPointerSlot, kKernelTickStructAddr);

    return base;
}
```

- [ ] **Step 3: Start the tick-updater thread in main(), after SetupMemoryImage**

Find:
```cpp
    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    uint8_t* base = SetupMemoryImage(xexPath);

    constexpr uint32_t kStackBase = 0x90000000;
```
Replace with:
```cpp
    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    uint8_t* base = SetupMemoryImage(xexPath);

    // Advances the tick field the guest reads via the pointer SetupMemoryImage wrote
    // at 0x82670100. Detached, matching ExCreateThread's precedent (host/kernel_impl.cpp)
    // -- never joined, runs harmlessly for the process's lifetime.
    std::thread tickThread([base]()
    {
        constexpr uint32_t kTickFieldAddr = 0x7FFF0000 + 88;
        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            PPC_STORE_U32(kTickFieldAddr, (uint32_t)elapsedMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    tickThread.detach();

    constexpr uint32_t kStackBase = 0x90000000;
```

- [ ] **Step 4: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2t_task1_build.log
grep -c "error:" /tmp/phase2t_task1_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 5: Commit**

```bash
git add host/main.cpp
git commit -m "Add host-driven kernel tick counter for r13+256 pointer slot"
```

---

## Task 2: Verify Against the Original Repro

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase2t_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2t-stub-hits.txt` (committed, only if new territory
  is reached -- see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2t-stub-hits.txt` (conditional).

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
./build/BigBumpinHost private/default.xex > private/phase2t_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check outcome and report honestly**

Run: `tail -40 private/phase2t_run.log; wc -l private/phase2t_run.log`

Report exactly what happened -- three possible honest outcomes:
- **New territory reached / busy-poll gone**: log shows activity past the Phase 2P/2R
  freeze point (last `KfReleaseSpinLock` line), or a clean return, or a new distinct
  failure. Continue to Step 4.
- **No observable change**: identical final state to the Phase 2R run. Report the exact
  log tail -- this would mean the tick-structure fix, while addressing the confirmed
  root cause of `sub_820B9A58`'s specific busy-poll, wasn't the whole story either.
- **A different busy-poll or new failure elsewhere**: also valid evidence -- report it
  exactly (which function, what pattern).

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
```

This reverts ONLY the watchdog line -- Task 1's tick-counter changes are in a separate
commit and are unaffected. Rebuild after:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2t_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2t-stub-hits.txt; wc -l docs/superpowers/specs/phase2t-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2t-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2T verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body. This closes Phase 2T.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's entire design section (fixed guest
  address, single `+88` field, pointer write during image setup, detached real-time
  updater thread at 1ms granularity). Task 2 covers success criteria 3-5: clean build
  (Task 1 Step 4), honest re-run report (Task 2 Steps 1-3), conditional stub-hit capture
  (Step 5). Success criteria 1-2 (pointer/tick correctness) are implicitly verified by
  Task 2's re-run -- if `sub_820B9A58` stops spinning, the chain worked end-to-end.
- **Placeholder scan:** No TBD/TODO. Step 3's three outcomes are each given concrete,
  explicit handling.
- **Type/interface consistency:** `kKernelTickStructAddr` (`SetupMemoryImage`) and
  `kTickFieldAddr`'s base (`main`'s lambda) both use the literal `0x7FFF0000` --
  no drift between the two.
- **Note on Step 4 (Task 2):** `git checkout -- host/main.cpp` after Task 1's commit
  reverts the file to Task 1's committed state (which already includes the tick-counter
  code), not to pre-Phase-2T state -- only the Task 2 Step 1 watchdog edit is undone.
