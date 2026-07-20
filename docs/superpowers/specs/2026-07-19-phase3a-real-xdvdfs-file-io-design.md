# Phase 3A: Real XDVDFS-Backed File I/O — Big Bumpin' Recomp

## Context

Every `NtCreateFile`/`NtOpenFile` call has returned `STATUS_NO_SUCH_DEVICE` unconditionally
since Phase 2D — a deliberate, conservative choice made when no code path actually depended
on real file access. That's no longer true: this project has now reached genuine asset-load
attempts, and zero real game data (textures, models, config, shaders) has ever loaded in any
run this session. Real GPU rendering work is worthless without real assets to feed it, so
this is now the higher-leverage next step over continuing to reverse-engineer the
in-progress device-registry investigation (Phase 2W/2X).

Investigation confirmed, with hard evidence, everything needed to implement this for real:

- **Disc format**: `Big Bumpin' (USA).iso` is a direct, unencrypted XDVDFS Xbox 360 disc
  image (not wrapped/compressed). Confirmed via the `MICROSOFT*XBOX*MEDIA` magic at byte
  offset `0x10000` (sector 32, the standard XDVDFS volume descriptor location).
- **Volume descriptor layout** (bytes immediately following the 20-byte magic): `u32
  root_dir_sector` (little-endian), `u32 root_dir_size`. Parsed directly: root directory at
  sector `264` (byte offset `0x84000`), size `244` bytes.
- **Directory entry format**, confirmed byte-for-byte against real parsed entries
  (`dashupdate.xbe`, `$SystemUpdate`, `CharacterTool.xex`, `default.xex`, `Packages`, etc.):
  ```c
  struct XdvdfsDirEntry {
      uint16_t left_offset;   // LE; BST left child, in 4-byte units from table start; 0xFFFF = none
      uint16_t right_offset;  // LE; BST right child
      uint32_t start_sector;  // LE
      uint32_t file_size;     // LE
      uint8_t  attributes;    // 0x10 = directory, 0x20 = normal file (confirmed both values observed)
      uint8_t  name_length;
      char     name[name_length]; // not null-terminated; entry padded to 4-byte alignment after
  };
  ```
  Entries within a directory table form a binary search tree (not a flat list) --
  `left_offset`/`right_offset` are the child entry's byte offset within the table, divided
  by 4; `0xFFFF` means no child.
- **`OBJECT_ATTRIBUTES` layout on this Xbox 360 target**: confirmed via reading a real
  `NtCreateFile` call site's register setup and cross-checking against captured runtime
  memory -- NOT the 24-byte desktop-Windows structure. This target uses a cut-down 12-byte
  version:
  ```c
  struct OBJECT_ATTRIBUTES {
      uint32_t RootDirectory; // offset 0 -- handle, 0 observed (root-relative/absolute path)
      uint32_t ObjectName;    // offset 4 -- pointer to an ANSI_STRING (length:u16, maxLength:u16, buffer:u32 -- same layout RtlInitAnsiString already produces)
      uint32_t Attributes;    // offset 8
  };
  ```
- **Real requested paths, captured live**: `\Device\Harddisk0\partition0` (raw partition
  device open, precedes drive mounting), `cache:\` (Xbox 360's app-cache pseudo-drive),
  `d:\_T_E_S_T.___` (a synthetic probe filename -- almost certainly a "is D: really there"
  check, expected to legitimately not exist), and real asset requests:
  `d:\Packages\packages\w_apploadingscreen_fetm.xen` /
  `d:\packages\w_apploadingscreen_fetm.xen` (case varies between attempts -- confirms path
  resolution must be case-insensitive, matching the real XDVDFS/FATX convention and the
  ISO's own `Packages` entry, capital P).

## Goal

Real, read-only file access to the disc's actual contents via `NtCreateFile`/`NtOpenFile`/
`NtReadFile`, so the game can load its real assets instead of failing every open.

## Design

### New module: XDVDFS reader

New files `host/xdvdfs.h` / `host/xdvdfs.cpp` (a new, self-contained subsystem --
justifies its own file pair rather than growing `kernel_impl.cpp` further, matching this
project's established practice of keeping each file responsible for one thing).

```cpp
struct XdvdfsEntry
{
    uint32_t startSector;
    uint32_t fileSize;
    bool isDirectory;
};

class XdvdfsImage
{
public:
    bool Open(const std::string& isoPath);
    bool ResolvePath(const std::string& path, XdvdfsEntry& outEntry); // case-insensitive, '\' or '/' separated
    void ReadBytes(const XdvdfsEntry& entry, uint64_t offsetInFile, uint32_t length, uint8_t* dest);

private:
    std::ifstream file_;
    uint32_t rootSector_ = 0;
    uint32_t rootSize_ = 0;
};
```

`ResolvePath` splits the input on `\`/`/`, then walks the directory tree one segment at a
time: starting from the root table, binary-search the current table's entries for a
case-insensitive name match (comparing against `left_offset`/`right_offset`-linked BST
nodes, per the confirmed format above); when a segment matches a directory entry and more
segments remain, descend into that entry's own table (`start_sector`/`file_size` become the
new table to search); the final segment's matched entry (file or directory) is the result.

`ReadBytes` computes the absolute byte offset (`entry.startSector * 2048 + offsetInFile`)
and reads directly from the open ISO file -- no caching, no extraction, matching the
"parse directly against the source of truth" approach chosen over extracting the disc to a
host directory first.

The `XdvdfsImage` instance is a single process-wide object (opened once at startup,
alongside the existing `SetupMemoryImage` in `host/main.cpp`), matching this project's
existing single-`g_stateMutex`-protected-globals pattern in `kernel_impl.cpp` -- read-only
after open, so no locking needed for concurrent reads (`std::ifstream` access will need a
mutex around the actual `seekg`/`read` calls, since the file position is shared mutable
state across threads).

### Handle table extension

Add `HandleObjectType::File` to the existing enum (`host/kernel_impl.cpp:151`). File state
(the open entry plus current read position) doesn't fit the existing `HandleObject`
struct's `signaled` field meaningfully, so it's tracked in a parallel map:

```cpp
struct FileHandleState
{
    XdvdfsEntry entry;
    uint64_t position; // current sequential read offset, advanced by NtReadFile
};
static std::unordered_map<uint32_t, FileHandleState> g_fileState;
```

### Path dispatch (shared by NtCreateFile and NtOpenFile)

Both functions parse the real filename via the now-confirmed `OBJECT_ATTRIBUTES`/
`ANSI_STRING` chain, then dispatch:

- **`\Device\Harddisk0\partition0`** (or any `\Device\...` prefix): open succeeds as a
  generic no-op device handle (`HandleObjectType::Generic`). No evidence yet that anything
  reads real data through this handle -- it's opened once, early, likely just to confirm
  the partition device exists before the game proceeds to open drive-letter paths.
- **`d:\...`** (case-insensitive drive prefix): strip the prefix, resolve the remainder
  against `XdvdfsImage::ResolvePath`. Found: allocate a `File`-type handle, populate
  `g_fileState`, return `STATUS_SUCCESS`. Not found: return
  `STATUS_OBJECT_NAME_NOT_FOUND` (distinct from `STATUS_NO_SUCH_DEVICE` -- the device now
  genuinely exists, this specific file doesn't; this is also the expected, correct outcome
  for the synthetic `_T_E_S_T.___` probe path).
- **Anything else** (e.g. `cache:\`): keep today's `STATUS_NO_SUCH_DEVICE` behavior. No
  evidence yet that real backing is needed for asset loading; not touched by this phase.

Only the read-existing-file case is handled -- no write support (the disc is read-only by
its nature; no evidence any observed call requests write access to a `d:\` path), no
`CreateDisposition` handling beyond "open if it exists."

### NtReadFile

Currently unimplemented (still on `host/kernel_imports.txt`, unconfirmed hit -- expected,
since every open has always failed so far, so this call path has never been reachable).
Implementing it is a necessary, non-speculative part of completing this feature: once opens
succeed, this will be called immediately.

Real NT signature (standard, well-documented convention, unrelated to the
Xbox-360-specific `OBJECT_ATTRIBUTES` cut-down): `NtReadFile(HANDLE FileHandle, HANDLE
Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID
Buffer, ULONG Length, PLARGE_INTEGER ByteOffset)` -- `r3`=FileHandle, `r4`=Event (ignored,
no evidence needed), `r5`=ApcRoutine (ignored), `r6`=ApcContext (ignored),
`r7`=IoStatusBlock, `r8`=Buffer, `r9`=Length, `r10`=ByteOffset (optional; null means "read
from current position").

```cpp
PPC_FUNC(__imp__NtReadFile)
{
    uint32_t handle = (uint32_t)ctx.r3.u64;
    uint32_t ioStatusBlockPtr = (uint32_t)ctx.r7.u64;
    uint32_t bufferPtr = (uint32_t)ctx.r8.u64;
    uint32_t length = (uint32_t)ctx.r9.u64;
    uint32_t byteOffsetPtr = (uint32_t)ctx.r10.u64;

    // look up g_fileState[handle]; STATUS_INVALID_HANDLE if absent
    // offset = byteOffsetPtr ? *byteOffsetPtr : state.position
    // clampedLength = min(length, entry.fileSize - offset), 0 if offset >= fileSize
    // xdvdfsImage.ReadBytes(state.entry, offset, clampedLength, base + bufferPtr)
    // state.position = offset + clampedLength
    // write IoStatusBlock: Status = STATUS_SUCCESS, Information = clampedLength
    // ctx.r3 = STATUS_SUCCESS
}
```

The real read call must actually copy bytes into guest memory at `bufferPtr` -- this is the
whole point of the feature, not a stub.

### `host/main.cpp` changes

Open the ISO once during `SetupMemoryImage` (or immediately after), matching the existing
`xexPath` argv-override pattern: default to `"Big Bumpin' (USA).iso"` (the file's real
name, already present at the repo root and symlinked into every worktree this session), 
overridable via a new optional `argv[2]`.

The XEX itself continues to load exactly as it does today (`private/default.xex`,
pre-extracted) -- this phase does not change how the title's own executable is loaded, only
how the *running game* accesses disc files at runtime. Changing XEX loading to also go
through the new XDVDFS reader is a reasonable future simplification but out of scope here
(no benefit to this phase's goal, real regression risk to something that already works).

## Success Criteria

1. `XdvdfsImage` correctly resolves real paths from this session's evidence (at minimum:
   `packages\w_apploadingscreen_fetm.xen`, case-insensitively) against the real ISO.
2. `NtCreateFile`/`NtOpenFile` real-path dispatch implemented: device-path success,
   disc-path resolution (found vs. not-found), other paths unchanged.
3. `NtReadFile` implemented, real bytes copied into guest memory, verified by dumping a
   handful of bytes from a resolved read against the same bytes read directly from the ISO
   file independently.
4. `BigBumpinHost` builds clean.
5. Re-run `private/default.xex` with an adequate watchdog, report honestly what happens.
   Given this changes a previously-permanent failure into real, variable success/failure
   depending on actual disc contents, do not assume any particular downstream outcome --
   report the actual new behavior, whatever it is (including the possibility that the
   Phase 2W/2X device-registry bugcheck is unaffected, resolved, or replaced by a new
   failure now that real asset bytes are flowing).
6. Capture the fresh run log and a distinct stub-hit list, per the established phase
   pattern.

## Explicitly Out of Scope

- Write support (disc is read-only; no evidence any real path needs it).
- `cache:\` real backing -- no evidence it's needed for asset loading.
- `NtQueryInformationFile`/`NtSetInformationFile` (e.g. for file-size queries) -- not yet
  confirmed hit; implement only if a future run shows they're actually called.
- Changing how `private/default.xex` itself is loaded.
- Any GPU/rendering work consuming the now-real asset bytes -- that's the actual "Phase 3"
  rendering wall, a separate, much larger undertaking this phase does not start.
- Continuing the Phase 2W/2X device-registry investigation -- deliberately deprioritized in
  favor of this higher-leverage, better-evidenced work; may or may not turn out to be
  related once this phase lands.
