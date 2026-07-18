# Phase 2C: Handle Table and Simple System Stubs — Big Bumpin' Recomp

## Context

Phase 2B ([spec](2026-07-18-phase2b-real-kernel-stubs-design.md)) replaced the 11 kernel
imports Phase 2A's run actually hit. The result was a large qualitative improvement: 2A's
run ended in a 55,700-line `KeBugCheck` crash loop; 2B's run reaches real game
initialization (config queries, file I/O attempts, crypto/key signing, mutex/event
creation, a real `ExCreateThread` call, physical memory allocation) across 49 log lines
before terminating via SIGTRAP — a new, more informative failure point.

That run produced 22 distinct newly-hit stubbed imports
(`docs/superpowers/specs/phase2b-stub-hits.txt`). Reading the full run log
(`private/phase2b_run.log`) to understand what these calls are actually doing showed this
is not one bounded problem — it's several independent subsystems: object handles and
synchronization, simple system-info queries, file I/O, crypto, physical memory
statistics/allocation, and real thread spawning. Cramming all 22 into one spec would
repeat the mistake the original project-wide scope check exists to prevent.

This spec covers only the first slice: **the handle table and 12 low-risk functions**
(5 handle/sync functions + 7 simple system-info stubs). File I/O, crypto, memory
statistics/physical allocation, and real thread spawning are each their own future spec.

## Goal

Implement 12 of the 22 hit imports with real behavior, informed directly by the actual
register values captured in Phase 2B's run log — not blind implementation of the full
list. `ExCreateThread` is explicitly excluded from this phase (see Explicitly Out of
Scope).

## Design

### Handle table

A single host-side table backs all handle-returning/consuming functions in this phase:

```cpp
enum class HandleObjectType { Mutant, Event };

struct HandleObject
{
    HandleObjectType type;
    bool signaled;
};

static std::unordered_map<uint32_t, HandleObject> g_handleTable;
static uint32_t g_nextHandle = 0x1000; // avoid colliding with 0 / 0xFFFFFFFF sentinels
```

- **`NtCreateMutant(r3=handleOutPtr, r4=objectAttributes, r5=initialOwner)`** — allocate a
  new handle, `signaled = !initialOwner` (a mutant held by its creator starts unsignaled;
  one not initially owned starts signaled/free). Write the handle value to `*handleOutPtr`,
  return `STATUS_SUCCESS` (`0`).
- **`NtCreateEvent(r3=handleOutPtr, r4=objectAttributes, r5=eventType, r6=initialState)`** —
  allocate a new handle, `signaled = initialState`. Same write/return pattern.
- **`NtReleaseMutant(r3=handle, r4=previousCountOutPtr)`** — set `signaled = true`. If
  `previousCountOutPtr` is non-null, write `0` (previous release count — not tracked
  precisely, low stakes for a single-execution-context bring-up).
- **`NtWaitForSingleObjectEx(r3=handle, r4=waitMode, r5=alertable, r6=timeoutPtr)`** —
  return `STATUS_SUCCESS` immediately, unconditionally, without checking the handle's
  signaled state. This isn't a shortcut — it's the *correct* behavior under a
  single-execution-context model: nothing else can run concurrently to change an object's
  state while we'd otherwise block, so waiting can never accomplish anything a real
  scheduler would. This assumption breaks only once real thread spawning exists — call out
  explicitly when `ExCreateThread` gets implemented for real.
- **`NtClose(r3=handle)`** — erase the entry from `g_handleTable` if present (no error if
  it wasn't — matches the observed log showing `NtClose` called on values like `0` and
  `0xFFFFFFFF` that were never real handles from this table). Return `STATUS_SUCCESS`.

### Simple system-info stubs

- **`XGetAVPack()`** — no real arguments (the logged `r3` value is leftover register
  content from a prior call, not a real parameter — this function takes none). Return `0`.
  The real AV-pack enum value isn't independently confirmed; `0` is a deliberate
  low-confidence default, not an asserted-correct constant — revisit if it visibly affects
  behavior (e.g. wrong aspect ratio/resolution assumptions downstream).
- **`ExGetXConfigSetting(r3=category, r4=setting, r5=bufferPtr, r6=bufferSize)`** —
  zero-fill `bufferSize` bytes at `bufferPtr`, return `STATUS_SUCCESS`. Uniform default
  across every category/setting combination observed in the log (all requesting 4-byte
  values) — not modeling the real Xbox 360 config-setting semantics, just returning
  well-formed zeroed data so callers don't choke on uninitialized memory.
- **`KeQuerySystemTime(r3=outPtr)`** — write the current host time converted to Windows
  `FILETIME` convention (100ns ticks since 1601-01-01) to `*outPtr` (8 bytes, big-endian
  guest order via `PPC_STORE_U64`).
- **`KeQueryPerformanceFrequency(r3=outPtr)`** — write `50000000` (Xbox 360's documented
  hardware timebase frequency — a real hardware constant, not a struct-layout guess) to
  `*outPtr`.
- **`KeEnableFpuExceptions(r3=enable)`** — no-op. We don't emulate FPU exception trapping;
  nothing downstream depends on this actually doing anything yet.
- **`RtlNtStatusToDosError(r3=status)`** — the observed call passed `0xC0000034`
  (`STATUS_OBJECT_NAME_NOT_FOUND`, a real documented NTSTATUS value — good evidence the
  register mapping understanding is correct here). Minimal mapping: that specific code
  returns `ERROR_FILE_NOT_FOUND` (`2`); anything else returns a generic non-zero fallback
  (`ERROR_GEN_FAILURE`, `31`). Expand the mapping only when a new status code is actually
  observed, not preemptively.
- **`RtlTimeToTimeFields(r3=inputTimePtr, r4=outputFieldsPtr)`** — read the 64-bit
  `FILETIME`-convention input, convert to a Unix `time_t` (subtract the epoch offset,
  divide by the 100ns-per-tick factor), use the host C library's `gmtime` for the actual
  calendar math (not hand-rolled leap-year logic), then write the eight `int16` fields
  (`Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday`) of the standard NT
  `TIME_FIELDS` structure to `outputFieldsPtr`. This structure's layout is well-documented
  and stable across NT versions, including on Xbox 360 — higher confidence than the
  critical-section structure Phase 2B deliberately avoided writing into, but still not
  independently instrumented-verified the way `NtAllocateVirtualMemory`'s register mapping
  was in Phase 2B. Flag it as a candidate for verification if anything reads back the
  written fields in a way that causes visibly wrong behavior.

## Success Criteria

Phase 2C is complete when:

1. All 12 functions above are implemented in `host/kernel_impl.cpp`, removed from
   `host/kernel_imports.txt`/regenerated out of `host/kernel_stubs.cpp` (same pattern as
   Phase 2B).
2. `BigBumpinHost` builds clean (zero compiler errors).
3. A fresh run either progresses further than Phase 2B's run or fails at a new,
   demonstrably different point.
4. The new run's log is saved and a fresh distinct-stub-hit list extracted, seeding
   whichever of the deferred groups (file I/O, crypto, memory stats/physical allocation,
   real threading) turns out to matter next.

## Explicitly Out of Scope

- **`ExCreateThread`** — the log shows a real worker-thread spawn request (`r6` is a
  genuine guest code address within the `.text` region), but implementing real thread
  spawning (a host thread per guest thread, its own `PPCContext`, stack allocation, and
  revisiting `NtWaitForSingleObjectEx`'s "always succeeds immediately" assumption once
  concurrency is real) is a distinct architectural jump, not a same-shape stub
  replacement. Deferred to its own future phase, taken on once evidence forces it rather
  than preemptively.
- File I/O (`NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`,
  `NtDeviceIoControlFile`) — needs a real decision about mapping guest file paths to
  extracted ISO content or a virtual filesystem. Own future spec.
- Crypto (`XeCryptSha`, `XeKeysConsolePrivateKeySign`) — own future spec.
- Memory statistics/physical allocation (`MmQueryStatistics`,
  `MmAllocatePhysicalMemoryEx`) — real struct layouts not independently confirmed, same
  risk category Phase 2B avoided for critical sections. Own future spec once/if actually
  blocking.
