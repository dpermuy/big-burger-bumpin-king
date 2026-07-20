# Phase 3A: Real XDVDFS-Backed File I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real, read-only disc file access via `NtCreateFile`/`NtOpenFile`/`NtReadFile`,
backed by a direct XDVDFS reader against the real ISO, replacing the permanent
`STATUS_NO_SUCH_DEVICE` every file open has returned since Phase 2D.

**Architecture:** New self-contained `XdvdfsImage` module (`host/xdvdfs.h`/`.cpp`) handles
volume parsing, path resolution, and byte reads against the ISO file directly -- no
extraction, no caching. `host/kernel_impl.cpp` gets a `File` handle type and real
`NtCreateFile`/`NtOpenFile`/`NtReadFile` implementations that dispatch through it.
`host/main.cpp` opens the ISO once at startup.

**Tech Stack:** C++17 `<fstream>` for the ISO reader (new dependency for this file, no new
external libraries).

## Global Constraints

- Read-only. No write support, no `CreateDisposition` handling beyond "open if it exists."
- Path resolution is case-insensitive (confirmed needed: real requests use `packages`
  against the ISO's `Packages` entry).
- Only `d:\` (disc) and `\Device\...` (generic success) paths get real dispatch; everything
  else (e.g. `cache:\`) keeps today's `STATUS_NO_SUCH_DEVICE` behavior unchanged.
- `NtReadFile` must copy real bytes into guest memory -- this is the feature, not a stub.

---

## Task 1: XdvdfsImage Reader Module

**Files:**
- Create: `host/xdvdfs.h`
- Create: `host/xdvdfs.cpp`
- Modify: `CMakeLists.txt:28` (add `host/xdvdfs.cpp` to `BigBumpinHost`'s sources)

**Interfaces:**
- Produces: `struct XdvdfsEntry { uint32_t startSector; uint32_t fileSize; bool
  isDirectory; }`, `class XdvdfsImage` with `bool Open(const std::string&)`, `bool
  ResolvePath(const std::string&, XdvdfsEntry&)`, `void ReadBytes(const XdvdfsEntry&,
  uint64_t, uint32_t, uint8_t*)`, and `extern XdvdfsImage g_xdvdfsImage;` -- consumed by
  Task 3/4 in `host/kernel_impl.cpp` and Task 2 in `host/main.cpp`.

- [ ] **Step 1: Write `host/xdvdfs.h`**

```cpp
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

struct XdvdfsEntry
{
    uint32_t startSector = 0;
    uint32_t fileSize = 0;
    bool isDirectory = false;
};

class XdvdfsImage
{
public:
    bool Open(const std::string& isoPath);
    bool ResolvePath(const std::string& path, XdvdfsEntry& outEntry);
    void ReadBytes(const XdvdfsEntry& entry, uint64_t offsetInFile, uint32_t length, uint8_t* dest);

private:
    bool SearchDirectory(uint32_t tableSector, uint32_t tableSize, const std::string& name, XdvdfsEntry& outEntry);

    std::mutex fileMutex_;
    std::ifstream file_;
    uint32_t rootSector_ = 0;
    uint32_t rootSize_ = 0;
};

extern XdvdfsImage g_xdvdfsImage;
```

- [ ] **Step 2: Write `host/xdvdfs.cpp`**

```cpp
#include "xdvdfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

XdvdfsImage g_xdvdfsImage;

namespace
{
    constexpr uint64_t kSectorSize = 2048;
    constexpr uint64_t kVolumeDescriptorOffset = 0x10000;
    constexpr char kMagic[20] = { 'M','I','C','R','O','S','O','F','T','*','X','B','O','X','*','M','E','D','I','A' };

    std::string ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return result;
    }

    std::vector<std::string> SplitPath(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string current;
        for (char c : path)
        {
            if (c == '\\' || c == '/')
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                    current.clear();
                }
            }
            else
            {
                current.push_back(c);
            }
        }
        if (!current.empty())
        {
            parts.push_back(current);
        }
        return parts;
    }
}

bool XdvdfsImage::Open(const std::string& isoPath)
{
    file_.open(isoPath, std::ios::binary);
    if (!file_.is_open())
    {
        return false;
    }

    file_.seekg((std::streamoff)kVolumeDescriptorOffset);
    char magic[20];
    file_.read(magic, 20);
    if (std::memcmp(magic, kMagic, 20) != 0)
    {
        return false;
    }

    uint32_t rootSector = 0, rootSize = 0;
    file_.read(reinterpret_cast<char*>(&rootSector), 4);
    file_.read(reinterpret_cast<char*>(&rootSize), 4);
    rootSector_ = rootSector;
    rootSize_ = rootSize;

    return true;
}

bool XdvdfsImage::SearchDirectory(uint32_t tableSector, uint32_t tableSize, const std::string& name, XdvdfsEntry& outEntry)
{
    std::vector<uint8_t> table(tableSize);
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        file_.seekg((std::streamoff)((uint64_t)tableSector * kSectorSize));
        file_.read(reinterpret_cast<char*>(table.data()), tableSize);
    }

    std::string targetLower = ToLower(name);
    uint32_t entryOffset = 0; // BST root is the entry at offset 0 of the table

    while (true)
    {
        // Header is 14 bytes (left:u16, right:u16, start_sector:u32, file_size:u32,
        // attributes:u8, name_length:u8); bail out if that alone doesn't fit.
        if ((uint64_t)entryOffset + 14 > tableSize)
        {
            return false;
        }

        uint16_t leftOffset, rightOffset;
        uint32_t startSector, fileSize;
        uint8_t attributes, nameLength;

        std::memcpy(&leftOffset, &table[entryOffset + 0], 2);
        std::memcpy(&rightOffset, &table[entryOffset + 2], 2);
        std::memcpy(&startSector, &table[entryOffset + 4], 4);
        std::memcpy(&fileSize, &table[entryOffset + 8], 4);
        attributes = table[entryOffset + 12];
        nameLength = table[entryOffset + 13];

        // Confirmed via real captured directory bytes: inter-entry padding is 0xFF-filled.
        // An empty directory table (or a read that landed entirely in padding) shows up
        // as an all-0xFF "entry" -- nameLength 0xFF is not a real name length.
        if (nameLength == 0xFF)
        {
            return false;
        }

        if ((uint64_t)entryOffset + 14 + nameLength > tableSize)
        {
            return false;
        }

        std::string entryName(reinterpret_cast<char*>(&table[entryOffset + 14]), nameLength);
        std::string entryNameLower = ToLower(entryName);

        if (targetLower == entryNameLower)
        {
            outEntry.startSector = startSector;
            outEntry.fileSize = fileSize;
            outEntry.isDirectory = (attributes & 0x10) != 0;
            return true;
        }

        if (targetLower < entryNameLower)
        {
            if (leftOffset == 0xFFFF) return false;
            entryOffset = (uint32_t)leftOffset * 4;
        }
        else
        {
            if (rightOffset == 0xFFFF) return false;
            entryOffset = (uint32_t)rightOffset * 4;
        }
    }
}

bool XdvdfsImage::ResolvePath(const std::string& path, XdvdfsEntry& outEntry)
{
    std::vector<std::string> parts = SplitPath(path);
    if (parts.empty())
    {
        return false;
    }

    uint32_t tableSector = rootSector_;
    uint32_t tableSize = rootSize_;
    XdvdfsEntry current{};

    for (size_t i = 0; i < parts.size(); i++)
    {
        if (!SearchDirectory(tableSector, tableSize, parts[i], current))
        {
            return false;
        }

        if (i + 1 < parts.size())
        {
            if (!current.isDirectory)
            {
                return false; // path continues past a plain file
            }
            tableSector = current.startSector;
            tableSize = current.fileSize;
        }
    }

    outEntry = current;
    return true;
}

void XdvdfsImage::ReadBytes(const XdvdfsEntry& entry, uint64_t offsetInFile, uint32_t length, uint8_t* dest)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    file_.seekg((std::streamoff)((uint64_t)entry.startSector * kSectorSize + offsetInFile));
    file_.read(reinterpret_cast<char*>(dest), length);
}
```

- [ ] **Step 3: Add the new source file to CMakeLists.txt**

Find (`CMakeLists.txt:28`):
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp)
```
Replace with:
```cmake
    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp)
```

- [ ] **Step 4: Reconfigure and build**

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase3a_task1_build.log
grep -c "error:" /tmp/phase3a_task1_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 5: Verify path resolution against the real ISO with a throwaway test**

This is new, previously-untested code -- verify it actually resolves a real path before
wiring it into the kernel functions. Create a temporary standalone test (not committed):

```bash
cat > /tmp/phase3a_xdvdfs_test.cpp << 'EOF'
#include "host/xdvdfs.h"
#include <cstdio>
int main() {
    if (!g_xdvdfsImage.Open("Big Bumpin' (USA).iso")) { printf("FAILED to open ISO\n"); return 1; }
    XdvdfsEntry entry;
    if (g_xdvdfsImage.ResolvePath("packages\\w_apploadingscreen_fetm.xen", entry)) {
        printf("FOUND: sector=%u size=%u\n", entry.startSector, entry.fileSize);
    } else {
        printf("NOT FOUND\n");
        return 1;
    }
    if (g_xdvdfsImage.ResolvePath("default.xex", entry)) {
        printf("FOUND default.xex: sector=%u size=%u\n", entry.startSector, entry.fileSize);
    } else {
        printf("NOT FOUND default.xex\n");
        return 1;
    }
    return 0;
}
EOF
$(brew --prefix llvm)/bin/clang++ -std=c++17 -I. /tmp/phase3a_xdvdfs_test.cpp host/xdvdfs.cpp -o /tmp/phase3a_xdvdfs_test
/tmp/phase3a_xdvdfs_test
```
Expected: both lookups print `FOUND`, with plausible non-zero sector/size values. If
either prints `NOT FOUND` or the tool crashes, stop -- do not proceed to Task 2/3 with a
broken resolver. Report the exact output either way.

- [ ] **Step 6: Commit**

```bash
git add host/xdvdfs.h host/xdvdfs.cpp CMakeLists.txt
git commit -m "Add XDVDFS reader module for real disc file access"
```

---

## Task 2: Open the ISO at Startup

**Files:**
- Modify: `host/main.cpp`

**Interfaces:**
- Consumes: `g_xdvdfsImage` (Task 1).
- Produces: nothing new consumed by other tasks -- `g_xdvdfsImage` is already globally
  accessible once opened.

- [ ] **Step 1: Include the new header and open the ISO in `main()`**

Find:
```cpp
    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    uint8_t* base = SetupMemoryImage(xexPath);
```
Replace with:
```cpp
    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    const char* isoPath = argc > 2 ? argv[2] : "Big Bumpin' (USA).iso";
    uint8_t* base = SetupMemoryImage(xexPath);

    if (!g_xdvdfsImage.Open(isoPath))
    {
        fmt::println("Warning: failed to open ISO '{}' -- disc file access will report not-found for everything.", isoPath);
    }
    else
    {
        fmt::println("Opened disc image: {}", isoPath);
    }
```

Add the include near the top of the file:
```cpp
#include "xdvdfs.h"
```

- [ ] **Step 2: Rebuild**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase3a_task2_build.log
grep -c "error:" /tmp/phase3a_task2_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 3: Commit**

```bash
git add host/main.cpp
git commit -m "Open the ISO's XDVDFS image at startup"
```

---

## Task 3: Real NtCreateFile / NtOpenFile Path Dispatch

**Files:**
- Modify: `host/kernel_impl.cpp`

**Interfaces:**
- Consumes: `g_xdvdfsImage`, `XdvdfsEntry` (Task 1), existing `g_handleTable`/
  `g_stateMutex`/`g_nextHandle` (Phase 2C/2N).
- Produces: `HandleObjectType::File`, `struct FileHandleState`, `g_fileState` map --
  consumed by Task 4's `NtReadFile`.

- [ ] **Step 1: Include the new header**

Add near the top of `host/kernel_impl.cpp`:
```cpp
#include "xdvdfs.h"
```

- [ ] **Step 2: Add the `File` handle type and state map**

Find:
```cpp
enum class HandleObjectType { Mutant, Event, Generic, Thread };
```
Replace with:
```cpp
enum class HandleObjectType { Mutant, Event, Generic, Thread, File };
```

Add after the existing `g_handleTable`/`g_nextHandle`/`g_handleSignalCv` declarations:
```cpp
struct FileHandleState
{
    XdvdfsEntry entry;
    uint64_t position = 0;
};
static std::unordered_map<uint32_t, FileHandleState> g_fileState;
```

- [ ] **Step 3: Add the shared path-dispatch helper**

Add before `NtCreateFile`'s definition:
```cpp
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;

static std::string ReadGuestAnsiString(uint32_t objectAttributesPtr)
{
    uint32_t objectNamePtr = PPC_LOAD_U32(objectAttributesPtr + 4);
    uint16_t nameLength = PPC_LOAD_U16(objectNamePtr + 0);
    uint32_t nameBufferPtr = PPC_LOAD_U32(objectNamePtr + 4);
    std::string name;
    for (uint16_t i = 0; i < nameLength; i++)
    {
        name.push_back((char)PPC_LOAD_U8(nameBufferPtr + i));
    }
    return name;
}

static uint32_t OpenGuestFile(const std::string& path, uint32_t& outHandle)
{
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (lowerPath.rfind("\\device\\", 0) == 0)
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        outHandle = g_nextHandle++;
        g_handleTable[outHandle] = HandleObject{ HandleObjectType::Generic, true };
        return 0; // STATUS_SUCCESS
    }

    if (lowerPath.rfind("d:\\", 0) == 0 || lowerPath.rfind("d:/", 0) == 0)
    {
        std::string subPath = path.substr(3);
        XdvdfsEntry entry;
        if (g_xdvdfsImage.ResolvePath(subPath, entry))
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            outHandle = g_nextHandle++;
            g_handleTable[outHandle] = HandleObject{ HandleObjectType::File, true };
            g_fileState[outHandle] = FileHandleState{ entry, 0 };
            return 0; // STATUS_SUCCESS
        }
        return kStatusObjectNameNotFound;
    }

    return kStatusNoSuchDevice;
}
```

Add `#include <algorithm>` and `#include <cctype>` to the top include block if not already
present (needed for `std::transform`/`std::tolower`).

- [ ] **Step 4: Replace NtCreateFile**

Find:
```cpp
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
```
Replace with:
```cpp
PPC_FUNC(__imp__NtCreateFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t objectAttributesPtr = (uint32_t)ctx.r5.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r6.u64;

    std::string path = ReadGuestAnsiString(objectAttributesPtr);
    uint32_t handle = 0;
    uint32_t status = OpenGuestFile(path, handle);

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, status == 0 ? handle : 0);
    }
    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, status);
        PPC_STORE_U32(ioStatusBlockPtr + 4, status == 0 ? 1 : 0); // FILE_OPENED
    }

    fmt::println("[kernel] NtCreateFile: \"{}\" -> status=0x{:X} handle=0x{:X}", path, status, handle);
    ctx.r3.u64 = status;
}
```

- [ ] **Step 5: Replace NtOpenFile**

Find:
```cpp
PPC_FUNC(__imp__NtOpenFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, 0);
    }

    fmt::println("[kernel] NtOpenFile: reporting STATUS_NO_SUCH_DEVICE (no virtual hard disk backing)");
    ctx.r3.u64 = kStatusNoSuchDevice;
```
(the rest of the function -- check its closing brace before replacing) with:
```cpp
PPC_FUNC(__imp__NtOpenFile)
{
    uint32_t handleOutPtr = (uint32_t)ctx.r3.u64;
    uint32_t objectAttributesPtr = (uint32_t)ctx.r5.u64;

    std::string path = ReadGuestAnsiString(objectAttributesPtr);
    uint32_t handle = 0;
    uint32_t status = OpenGuestFile(path, handle);

    if (handleOutPtr != 0)
    {
        PPC_STORE_U32(handleOutPtr, status == 0 ? handle : 0);
    }

    fmt::println("[kernel] NtOpenFile: \"{}\" -> status=0x{:X} handle=0x{:X}", path, status, handle);
    ctx.r3.u64 = status;
```
(preserve the function's existing closing brace).

- [ ] **Step 6: Rebuild**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase3a_task3_build.log
grep -c "error:" /tmp/phase3a_task3_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 7: Commit**

```bash
git add host/kernel_impl.cpp
git commit -m "Implement real NtCreateFile/NtOpenFile path dispatch against the disc image"
```

---

## Task 4: Real NtReadFile

**Files:**
- Modify: `host/kernel_imports.txt` (remove 1 line, if `NtReadFile` is present -- see Step 1)
- Modify: `host/kernel_stubs.cpp` (regenerated)
- Modify: `host/kernel_impl.cpp` (append)

**Interfaces:**
- Consumes: `g_fileState`, `XdvdfsImage::ReadBytes` (Task 1/3).
- Produces: nothing consumed by other tasks -- leaf function.

- [ ] **Step 1: Check whether NtReadFile is on the import list**

Run: `grep -n "NtReadFile" host/kernel_imports.txt`

If it's present, remove that line and regenerate:
```bash
python3 tools/generate_kernel_stubs.py
```
If it's absent (never yet observed, so never added to the list), skip straight to Step 2 --
this is a new function being added directly to `kernel_impl.cpp`, matching how other
functions were added when first confirmed necessary rather than stubbed first.

- [ ] **Step 2: Implement NtReadFile**

Append to `host/kernel_impl.cpp`:
```cpp
PPC_FUNC(__imp__NtReadFile)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r7.u64;
    uint32_t bufferPtr = (uint32_t)ctx.r8.u64;
    uint32_t length = (uint32_t)ctx.r9.u64;
    uint32_t byteOffsetPtr = (uint32_t)ctx.r10.u64;

    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_fileState.find(handle);
    if (it == g_fileState.end())
    {
        fmt::println("[kernel] NtReadFile: unknown handle 0x{:X}", handle);
        if (ioStatusBlockPtr != 0)
        {
            PPC_STORE_U32(ioStatusBlockPtr + 0, 0xC0000008); // STATUS_INVALID_HANDLE
            PPC_STORE_U32(ioStatusBlockPtr + 4, 0);
        }
        ctx.r3.u64 = 0xC0000008;
        return;
    }

    FileHandleState& state = it->second;
    uint64_t offset = (byteOffsetPtr != 0) ? PPC_LOAD_U64(byteOffsetPtr) : state.position;

    uint32_t bytesToRead = 0;
    if (offset < state.entry.fileSize)
    {
        uint64_t remaining = state.entry.fileSize - offset;
        bytesToRead = (uint32_t)(remaining < length ? remaining : length);
    }

    if (bytesToRead > 0)
    {
        g_xdvdfsImage.ReadBytes(state.entry, offset, bytesToRead, base + bufferPtr);
    }
    state.position = offset + bytesToRead;

    if (ioStatusBlockPtr != 0)
    {
        PPC_STORE_U32(ioStatusBlockPtr + 0, 0); // STATUS_SUCCESS
        PPC_STORE_U32(ioStatusBlockPtr + 4, bytesToRead);
    }

    fmt::println("[kernel] NtReadFile: handle=0x{:X} offset={} requested={} read={}", handle, offset, length, bytesToRead);
    ctx.r3.u64 = 0; // STATUS_SUCCESS
}
```

- [ ] **Step 3: Rebuild**

```bash
cmake --build build --target BigBumpinHost 2>&1 | tee /tmp/phase3a_task4_build.log
grep -c "error:" /tmp/phase3a_task4_build.log
```
Expected: exits 0, `grep -c "error:"` prints `0`.

- [ ] **Step 4: Commit**

```bash
git add host/kernel_imports.txt host/kernel_stubs.cpp host/kernel_impl.cpp
git commit -m "Implement real NtReadFile against the disc image"
```

---

## Task 5: Verify Against a Real Run

**Files:**
- Modify (temporarily, reverted at end of task): `host/main.cpp` (watchdog duration)
- Create: `private/phase3a_run.log` (gitignored)
- Create: `docs/superpowers/specs/phase3a-stub-hits.txt` (committed, only if new territory
  is reached -- see Step 5)

**Interfaces:**
- Consumes: `build/BigBumpinHost` (Tasks 1-4), `private/default.xex`,
  `Big Bumpin' (USA).iso`.
- Produces: `docs/superpowers/specs/phase3a-stub-hits.txt` (conditional).

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
./build/BigBumpinHost private/default.xex > private/phase3a_run.log 2>&1
echo "exit=$?"
```

- [ ] **Step 3: Check outcome and report honestly**

Run: `grep "NtCreateFile\|NtOpenFile\|NtReadFile" private/phase3a_run.log | head -30`

Confirm real asset opens now succeed (look for the `w_apploadingscreen_fetm.xen` request
or similar returning `status=0x0` instead of the old blanket `STATUS_NO_SUCH_DEVICE`), and
that `NtReadFile` calls appear with non-zero `read=` counts.

Then check the overall outcome: `tail -40 private/phase3a_run.log; wc -l private/phase3a_run.log`

Report exactly what happened -- do not assume any particular downstream result:
- **New territory / further progress**: execution advances past where it stopped before
  (the Phase 2W/2X device-registry bugcheck), or reaches a different, new failure point,
  or returns cleanly. Continue to Step 4.
- **Same bugcheck as before, but real files now open successfully**: also valid and
  useful -- means this phase's own goal (real file I/O) is confirmed working even if it
  didn't resolve the unrelated registry issue. Continue to Step 4.
- **A new failure caused by this phase's own code** (e.g. a crash reading real asset
  bytes): report the exact symptom, do not paper over it.

- [ ] **Step 4: Revert the temporary watchdog change**

```bash
git checkout -- host/main.cpp
cmake --build build --target BigBumpinHost 2>&1 | tail -5
```

Do this regardless of Step 3's outcome.

- [ ] **Step 5: Extract a fresh stub-hit list, if new territory was reached**

Only if Step 3 showed genuine new progress past the previous stopping point:

Run: `grep -oE "^\[stub\] [A-Za-z0-9_]+" private/phase3a_run.log | sed -E 's/^\[stub\] //' | sort -u > docs/superpowers/specs/phase3a-stub-hits.txt; wc -l docs/superpowers/specs/phase3a-stub-hits.txt`

- [ ] **Step 6: Commit whatever evidence was produced**

```bash
git add docs/superpowers/specs/phase3a-stub-hits.txt 2>/dev/null
git commit -m "Record Phase 3A verification run outcome" --allow-empty
```

State the actual outcome honestly in the commit message body -- specifically confirm
whether real file reads were observed working, independent of whatever else did or didn't
change. This closes Phase 3A.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 1 implements the spec's XDVDFS reader design exactly (volume
  parse, BST path resolution, byte reads), including a standalone pre-integration test
  (Step 5) to catch resolver bugs before they're buried inside kernel call dispatch --
  stronger verification than the spec strictly required, justified given this is entirely
  new, previously-untested logic. Task 2 covers the `host/main.cpp` wiring. Task 3
  implements the confirmed `OBJECT_ATTRIBUTES`/`ANSI_STRING` parsing and path dispatch
  exactly as specified (device paths, disc paths, fallback). Task 4 implements
  `NtReadFile` per the documented standard NT signature. Task 5 covers success criteria
  5-6 (honest re-run report, conditional stub-hit capture).
- **Placeholder scan:** No TBD/TODO. The one deliberately-approximate piece (the
  `nameLength == 0xFF` empty-directory/out-of-bounds heuristic in `SearchDirectory`) is
  explicitly flagged as evidence-grounded-but-not-exhaustively-tested in its own comment,
  not silently assumed correct -- Task 1 Step 5's standalone test is specifically there to
  catch resolver bugs before Task 3 wires it in.
- **Type/interface consistency:** `XdvdfsEntry`/`XdvdfsImage`/`g_xdvdfsImage` declared once
  in Task 1, used identically (same names, same signatures) in Tasks 2-4.
  `FileHandleState`/`g_fileState` declared in Task 3, used identically in Task 4.
