# Phase 2K: Real Blocking Critical Sections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bookkeeping-only `RtlEnterCriticalSection`/`RtlLeaveCriticalSection`
(`host/kernel_impl.cpp:57-91`) with real blocking `std::recursive_mutex`-backed locks, so a
thread waiting on a critical section actually blocks instead of busy-spinning — fixing the
priority-inversion livelock found during the physical-memory-stall investigation.

**Architecture:** Single-file change to `host/kernel_impl.cpp`. `g_criticalSections` changes
from `unordered_map<uint32_t, CriticalSectionState>` (manual recursion counter) to
`unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>>` (real OS-backed recursive
lock). `g_stateMutex` continues to protect only the map's structure (lookup/insert), never
the actual lock/unlock call.

**Tech Stack:** C++17 `<mutex>` (`std::recursive_mutex`, already available — `<mutex>` is
already included in `host/kernel_impl.cpp:9`).

## Global Constraints

- `g_stateMutex` must never be held across the real `->lock()`/`->unlock()` call — only
  across the map lookup/insert. Holding it across a blocking wait would stall every other
  `g_stateMutex` consumer (handle table, bump allocator, TLS) while one thread waits on an
  unrelated critical section.
- `RtlTryEnterCriticalSection`, `RtlDeleteCriticalSection`, `KeEnterCriticalRegion`,
  `KeLeaveCriticalRegion` are not touched — out of scope per the Phase 2K spec.

---

## Task 1: Replace Critical Section Storage and Implementation

**Files:**
- Modify: `host/kernel_impl.cpp:57-91`

**Interfaces:**
- Consumes: `std::recursive_mutex`, `std::unique_ptr` (both already available via existing
  `<mutex>` and standard headers).
- Produces: nothing consumed by other tasks — self-contained.

- [ ] **Step 1: Replace `CriticalSectionState` and the three functions**

Replace lines 57-91 of `host/kernel_impl.cpp` (currently the `CriticalSectionState` struct,
the `g_criticalSections` map declaration, and the three `PPC_FUNC` bodies for
`RtlInitializeCriticalSection`, `RtlEnterCriticalSection`, `RtlLeaveCriticalSection`) with:

```cpp
static std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> g_criticalSections;

static std::recursive_mutex& GetCriticalSection(uint32_t csAddr)
{
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto& slot = g_criticalSections[csAddr];
    if (!slot)
    {
        slot = std::make_unique<std::recursive_mutex>();
    }
    return *slot;
}

PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr);
}

PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr).lock();
}

PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    uint32_t csAddr = (uint32_t)ctx.r3.u64;
    GetCriticalSection(csAddr).unlock();
}
```

Note: `GetCriticalSection` takes and releases `g_stateMutex` internally before returning —
callers never hold `g_stateMutex` while calling `.lock()`/`.unlock()` on the real mutex,
matching the spec's global constraint.

- [ ] **Step 2: Rebuild**

Run:
```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase2k_task1_build.log
grep -c "error:" /tmp/phase2k_task1_build.log
```
Expected: build succeeds, `grep -c "error:"` prints `0`.

- [ ] **Step 3: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Implement real blocking recursive-mutex critical sections"
```

---

## Task 2: Verify the Fix Against the Original Repro

**Files:**
- Create: `private/phase2k_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase2k-stub-hits.txt` (committed, only if the run reaches
  new territory — see Step 4)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Task 1), `private/default.xex`.
- Produces: `docs/superpowers/specs/phase2k-stub-hits.txt` (conditional).

- [ ] **Step 1: Run with the standard 10-second watchdog**

Run: `./build/BigBumpinHost private/default.xex > private/phase2k_run.log 2>&1; echo "exit=$?"`

- [ ] **Step 2: Compare the last logged address against the old freeze point**

The previous freeze (pre-fix) always stopped at the same final line regardless of watchdog
length: `MmAllocatePhysicalMemoryEx: 65536 bytes -> 0xA08E0000`. Check what this run's last
line is:

Run: `tail -5 private/phase2k_run.log`

Three possible honest outcomes — report whichever actually happens, do not assume success:
- **Progress**: last address is past `0xA08E0000`, or the log shows different/new activity
  (new kernel calls, a crash, or a clean return) — the fix changed behavior. Continue to
  Step 3.
- **Same freeze point**: log is identical to the pre-fix run, still stops at exactly
  `0xA08E0000` — the livelock either wasn't fully addressed or there's a second, distinct
  blocker. Do not paper over this — stop and report the exact diff, do not proceed to
  Step 3/4.
- **New, different failure**: a crash or a different stall point — also valid evidence,
  report it exactly (address, signal if any).

- [ ] **Step 3: If progress was made, confirm no thread is livelocked**

Only run this step if Step 2 showed progress or timeout at a new point (skip if the process
exited cleanly with status 0 well before 10 seconds — no need to inspect a process that
already finished).

```bash
./build/BigBumpinHost private/default.xex > private/phase2k_run2.log 2>&1 &
sleep 5
PID=$!
lldb -p $PID -b -o "thread list" -o "quit" > /tmp/phase2k_threads1.txt 2>&1
sleep 3
lldb -p $PID -b -o "thread list" -o "quit" > /tmp/phase2k_threads2.txt 2>&1
diff /tmp/phase2k_threads1.txt /tmp/phase2k_threads2.txt
kill -9 $PID 2>/dev/null
```
Expected: the `diff` shows at least some threads' PCs changing between samples (real
progress), and no thread pinned at an identical PC across both samples the way
`sub_82128110`'s time-base read was before the fix. Report whatever the diff actually
shows — this is a sanity check, not a step that can silently "pass" without being read.

- [ ] **Step 4: Extract a fresh stub-hit list, if the run reached new territory**

Only if Step 2/3 showed genuine new progress (not just a repeat of the old freeze):

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase2k_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase2k-stub-hits.txt; wc -l docs/superpowers/specs/phase2k-stub-hits.txt`

- [ ] **Step 5: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase2k-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 2K verification run: critical-section fix outcome" --allow-empty
```

Use the commit message body (via `-m` a second time, or edit before committing) to state
the actual outcome honestly: progress made (and how far), no change, or a new failure —
whichever Step 2 actually found. This closes Phase 2K.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's entire design section (real
  `recursive_mutex` per section, `g_stateMutex` only guarding the map, no change to
  `RtlTryEnterCriticalSection`/`RtlDeleteCriticalSection`/`KeEnterCriticalRegion`/
  `KeLeaveCriticalRegion`). Task 2 covers success criteria 2-4 (clean build already covered
  by Task 1 Step 2; re-run and honest reporting in Task 2 Steps 1-2; livelock sanity check
  in Step 3 goes beyond the spec's minimum bar since this bug's symptom was specifically a
  CPU-spin livelock, not just "did it stall" — worth confirming the mechanism, not just the
  symptom).
- **Placeholder scan:** No TBD/TODO. Step 2's three possible outcomes are each given
  concrete, explicit handling instead of a vague "check if it worked."
- **Type/interface consistency:** `GetCriticalSection` returns `std::recursive_mutex&`,
  used identically by all three call sites. No other task/file depends on this interface.
