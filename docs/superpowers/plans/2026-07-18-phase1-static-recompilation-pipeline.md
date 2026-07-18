# Phase 1: Static Recompilation Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract `default.xex` from the "Big Bumpin'" Xbox 360 ISO and get XenonRecomp's generated C++ output compiling into a static library (`BigBumpinPPC`), proving the extraction → analysis → recompilation → build pipeline works end-to-end for this game. No runtime, no linking to an executable, no running the game — that's Phase 2+.

**Architecture:** `thirdparty/XenonRecomp` is vendored as a git submodule and built as a CMake subdirectory of a single unified root `CMakeLists.txt`, which also defines a small custom host tool (`FindSpecialAddresses`) for locating game-specific register save/restore function addresses, and — once generated code exists — the `BigBumpinPPC` static library target. `private/` holds all copyrighted-derived artifacts (ISO-extracted files, generated code) and is gitignored; only hand-written config, CMake glue, and the custom tool are committed.

**Tech Stack:** CMake 3.20+, Ninja, Homebrew LLVM Clang (18+, not Apple's Xcode Clang), `xdvdfs-cli` (Rust/cargo), XenonRecomp/XenonAnalyse/XenonUtils (C++17).

## Global Constraints

- Use Homebrew LLVM Clang (`$(brew --prefix llvm)/bin/clang` / `clang++`) for **all** builds in this project, not Apple's Xcode Clang — XenonRecomp's README states other compilers/forks "have not been tested and are not recommended."
- `private/` is gitignored. Never commit the ISO, extracted XEX, or generated `ppc/` C++ — they are derived from copyrighted game binary data.
- CMake minimum version 3.20 (per XenonRecomp's own requirement).
- All new C++ source in this repo (the `tools/` helper) targets C++17, matching `thirdparty/XenonRecomp`'s own standard, to avoid ABI/standard-library mismatches within the same build.

---

## Task 1: Install and Verify Toolchain

**Files:** None (system-level tool installation only).

**Interfaces:**
- Produces: working `cmake`, `ninja`, `$(brew --prefix llvm)/bin/clang`/`clang++`, and `xdvdfs` binaries on `PATH` for all later tasks.

- [ ] **Step 1: Install build tools via Homebrew**

Run: `brew install cmake ninja llvm`

- [ ] **Step 2: Verify cmake and ninja versions**

Run: `cmake --version && ninja --version`
Expected: cmake reports version `3.20` or higher; ninja prints a version number without error.

- [ ] **Step 3: Verify Homebrew LLVM Clang version**

Run: `"$(brew --prefix llvm)/bin/clang" --version`
Expected: Output starts with `Homebrew clang version` (not `Apple clang`) and the version number is `18` or higher.

- [ ] **Step 4: Install xdvdfs-cli via cargo**

Run: `cargo install xdvdfs-cli`

- [ ] **Step 5: Verify xdvdfs is available**

Run: `xdvdfs --help`
Expected: Help text listing subcommands including `unpack`, `ls`, `tree`. If `xdvdfs: command not found`, add `~/.cargo/bin` to `PATH` (`export PATH="$HOME/.cargo/bin:$PATH"`) and retry.

---

## Task 2: Vendor XenonRecomp as a Git Submodule

**Files:**
- Create (via git): `.gitmodules`
- Create (via git): `thirdparty/XenonRecomp/` (submodule checkout)

**Interfaces:**
- Consumes: nothing.
- Produces: `thirdparty/XenonRecomp/{XenonAnalyse,XenonRecomp,XenonUtils,thirdparty}` directory tree used by every later task's CMake configuration.

- [ ] **Step 1: Add the submodule**

Run: `git submodule add https://github.com/hedge-dev/XenonRecomp.git thirdparty/XenonRecomp`

- [ ] **Step 2: Initialize its nested submodules (fmt, simde, tomlplusplus, xxHash, libmspack, tiny-AES-c)**

Run: `git submodule update --init --recursive`

- [ ] **Step 3: Verify the checkout is complete**

Run: `test -f thirdparty/XenonRecomp/README.md && test -f thirdparty/XenonRecomp/thirdparty/fmt/CMakeLists.txt && echo OK`
Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add .gitmodules thirdparty/XenonRecomp
git commit -m "Vendor XenonRecomp as a git submodule"
```

---

## Task 3: Build XenonAnalyse and XenonRecomp Host Tools

**Files:**
- Create: `CMakeLists.txt` (repo root)

**Interfaces:**
- Consumes: `thirdparty/XenonRecomp` from Task 2.
- Produces: `build/XenonAnalyse/XenonAnalyse` and `build/XenonRecomp/XenonRecomp` executables, plus the `XenonUtils` CMake target that Task 5's tool and Task 9's `BigBumpinPPC` target link against.

- [ ] **Step 1: Write the root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(BigBumpinRecomp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(thirdparty/XenonRecomp)
```

- [ ] **Step 2: Configure with Ninja and Homebrew LLVM Clang**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
```
Expected: Configuration completes with `-- Build files have been written to: .../build`, no errors.

- [ ] **Step 3: Build**

Run: `cmake --build build`
Expected: Build completes with no errors (target `XenonRecomp` links; `XenonTests` produces no target since `thirdparty/XenonRecomp/XenonTests/*.cpp` doesn't exist yet — this is expected, matches the same empty-glob guard the upstream project itself uses).

- [ ] **Step 4: Verify XenonAnalyse runs**

Run: `./build/XenonAnalyse/XenonAnalyse`
Expected: `Usage: XenonAnalyse [input XEX file path] [output jump table TOML file path]`, exit code `0`.

- [ ] **Step 5: Verify XenonRecomp runs**

Run: `./build/XenonRecomp/XenonRecomp/XenonRecomp`
Expected: `Usage: XenonRecomp [input TOML file path] [PPC context header file path]`, exit code `0`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "Build XenonAnalyse/XenonRecomp host tools via CMake+Ninja"
```

---

## Task 4: Extract default.xex From the ISO

**Files:**
- Create: `private/default.xex` (gitignored)
- Create (conditionally): `private/default.xexp` (gitignored)

**Interfaces:**
- Consumes: `Big Bumpin' (USA).iso` (repo root).
- Produces: `private/default.xex`, consumed by Tasks 5, 6, 8.

- [ ] **Step 1: Create the private working directory**

Run: `mkdir -p private`

- [ ] **Step 2: Unpack the ISO**

Run: `xdvdfs unpack "Big Bumpin' (USA).iso" private/extracted`
Expected: Command exits `0` and `private/extracted/` is populated.

- [ ] **Step 3: Locate default.xex and any title-update .xexp, copy into place**

Run: `find private/extracted -iname "default.xex*"`
Expected: At minimum one match, `private/extracted/default.xex` (path may vary slightly if nested in a subdirectory — use the actual path reported here in the next command).

Then:
```bash
cp private/extracted/default.xex private/default.xex
# Only if the previous find also listed a default.xexp:
cp private/extracted/default.xexp private/default.xexp
```

- [ ] **Step 4: Verify the XEX2 header magic**

Run: `xxd -l 4 private/default.xex`
Expected: `58455832` — spells `XEX2` in ASCII (the field is right there in the hex dump's ASCII column).

- [ ] **Step 5: Record whether a title update exists**

If `private/default.xexp` was found in Step 3, note it — `config/BigBumpin.toml` in Task 7 will set `patch_file_path`/`patched_file_path`. If not found, Task 7's config omits those two keys entirely (per XenonRecomp's README, they're only required "if the game has no title updates" — i.e. not required when there are none).

---

## Task 5: Locate Register Save/Restore Function Addresses

XenonRecomp requires the addresses of eight compiler-emitted helper functions (`__restgprlr_14`, `__savegprlr_14`, etc. — see `thirdparty/XenonRecomp/README.md`, "Register Restore & Save Functions"). These are game-binary-specific and not published anywhere for this title, so we locate them ourselves by scanning `default.xex`'s code sections for the exact byte patterns XenonRecomp's own README documents, reusing the same `Image::ParseImage` XEX loader XenonAnalyse and XenonRecomp use internally (`thirdparty/XenonRecomp/XenonUtils/image.h`).

**Files:**
- Create: `tools/find_special_addresses.cpp`
- Create: `tools/CMakeLists.txt`
- Modify: `CMakeLists.txt:7` (repo root — add `add_subdirectory(tools)` after the XenonRecomp submodule line)

**Interfaces:**
- Consumes: `Image::ParseImage(const uint8_t*, size_t)`, `Section{base, size, flags, data}`, `SectionFlags_Code` from `thirdparty/XenonRecomp/XenonUtils/{image.h,section.h}`; `LoadFile` from `thirdparty/XenonRecomp/XenonUtils/file.h`.
- Produces: eight `key = 0xADDRESS` lines (stdout), consumed directly by Task 7 when authoring `config/BigBumpin.toml`.

- [ ] **Step 1: Write the pattern-finder tool**

Create `tools/find_special_addresses.cpp`:

```cpp
#include <cstdio>
#include <cstring>
#include <file.h>
#include <image.h>

struct Pattern
{
    const char* name;
    uint8_t bytes[8];
    size_t length;
};

static const Pattern patterns[] = {
    { "restgprlr_14_address", { 0xe9, 0xc1, 0xff, 0x68 }, 4 },
    { "savegprlr_14_address", { 0xf9, 0xc1, 0xff, 0x68 }, 4 },
    { "restfpr_14_address",   { 0xc9, 0xcc, 0xff, 0x70 }, 4 },
    { "savefpr_14_address",   { 0xd9, 0xcc, 0xff, 0x70 }, 4 },
    { "restvmx_14_address",   { 0x39, 0x60, 0xfe, 0xe0, 0x7d, 0xcb, 0x60, 0xce }, 8 },
    { "savevmx_14_address",   { 0x39, 0x60, 0xfe, 0xe0, 0x7d, 0xcb, 0x61, 0xce }, 8 },
    { "restvmx_64_address",   { 0x39, 0x60, 0xfc, 0x00, 0x10, 0x0b, 0x60, 0xcb }, 8 },
    { "savevmx_64_address",   { 0x39, 0x60, 0xfc, 0x00, 0x10, 0x0b, 0x61, 0xcb }, 8 },
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: FindSpecialAddresses [input XEX file path]\n");
        return 1;
    }

    const auto file = LoadFile(argv[1]);
    if (file.empty())
    {
        printf("Failed to load file: %s\n", argv[1]);
        return 1;
    }

    auto image = Image::ParseImage(file.data(), file.size());

    for (const auto& pattern : patterns)
    {
        bool found = false;
        for (const auto& section : image.sections)
        {
            if (!(section.flags & SectionFlags_Code))
                continue;

            for (size_t i = 0; i + pattern.length <= section.size; i++)
            {
                if (memcmp(section.data + i, pattern.bytes, pattern.length) == 0)
                {
                    printf("%s = 0x%zX\n", pattern.name, section.base + i);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
        {
            printf("# %s NOT FOUND\n", pattern.name);
        }
    }

    return 0;
}
```

- [ ] **Step 2: Write tools/CMakeLists.txt**

```cmake
add_executable(FindSpecialAddresses find_special_addresses.cpp)
target_link_libraries(FindSpecialAddresses PRIVATE XenonUtils)
```

- [ ] **Step 3: Wire it into the root CMakeLists.txt**

Modify `CMakeLists.txt`, after `add_subdirectory(thirdparty/XenonRecomp)`:

```cmake
add_subdirectory(thirdparty/XenonRecomp)
add_subdirectory(tools)
```

- [ ] **Step 4: Reconfigure and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Expected: `build/tools/FindSpecialAddresses` exists, build has no errors.

- [ ] **Step 5: Run it against default.xex**

Run: `./build/tools/FindSpecialAddresses private/default.xex`
Expected: Eight lines, one per pattern. Each should read `name = 0xHEXVALUE`, not `# name NOT FOUND`. If any pattern is not found, see Troubleshooting below before proceeding to Task 7.

**Troubleshooting a `NOT FOUND` line:** The byte patterns match unmodified Xbox 360 SDK-emitted helper functions; a `NOT FOUND` most often means the function exists but our code-section scan missed it (rare), or the game genuinely doesn't use that specific non-volatile-register range (uncommon for `_14` variants, more plausible for `_64` VMX variants if the game uses few vector registers). Do not fabricate a value — leave that key out of `config/BigBumpin.toml` in Task 7 and revisit only if Task 9's build errors trace back to it.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/find_special_addresses.cpp tools/CMakeLists.txt
git commit -m "Add FindSpecialAddresses tool for locating register save/restore functions"
```

---

## Task 6: Run XenonAnalyse to Generate the Switch Table TOML

**Files:**
- Create: `private/switch_tables.toml` (gitignored)

**Interfaces:**
- Consumes: `private/default.xex` (Task 4), `build/XenonAnalyse/XenonAnalyse` (Task 3).
- Produces: `private/switch_tables.toml`, referenced by `config/BigBumpin.toml` in Task 7.

- [ ] **Step 1: Run XenonAnalyse**

Run: `./build/XenonAnalyse/XenonAnalyse private/default.xex private/switch_tables.toml`
Expected: Exit code `0`.

- [ ] **Step 2: Verify the output file exists**

Run: `test -f private/switch_tables.toml && wc -l private/switch_tables.toml`
Expected: File exists. Line count may legitimately be small (just the `# ---- ... JUMPTABLE ----` section comments) if few or no jump tables were detected — that's an acceptable outcome for this pass per the approved design (fast-iterate approach, not full upfront disassembly).

---

## Task 7: Author config/BigBumpin.toml

**Files:**
- Create: `config/BigBumpin.toml`

**Interfaces:**
- Consumes: eight `key = 0xADDRESS` lines from Task 5 Step 5's output; `private/default.xex` path (Task 4); `private/switch_tables.toml` path (Task 6).
- Produces: `config/BigBumpin.toml`, consumed by `XenonRecomp` in Task 8.

- [ ] **Step 1: Create the config directory and the recompiler's output directory**

Run: `mkdir -p config private/ppc`

- [ ] **Step 2: Write config/BigBumpin.toml**

Start from this template. All paths are relative to this file's own directory (`config/`), per XenonRecomp's README. Replace each `0x...` on the eight `*_address` lines with the corresponding value printed by Task 5 Step 5 — copy them verbatim, do not renumber or guess:

```toml
[main]
file_path = "../private/default.xex"
out_directory_path = "../private/ppc"
switch_table_file_path = "../private/switch_tables.toml"

restgprlr_14_address = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
savegprlr_14_address = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
restfpr_14_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
savefpr_14_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
restvmx_14_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
savevmx_14_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
restvmx_64_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
savevmx_64_address   = 0x00000000 # from FindSpecialAddresses output, Task 5 Step 5
```

If Task 4 found a `private/default.xexp`, add these two lines under `[main]` as well:
```toml
patch_file_path = "../private/default.xexp"
patched_file_path = "../private/default_patched.xex"
```

Do not add `longjmp_address` / `setjmp_address` yet — per the README these are optional ("If the game does not use these functions, you can remove the properties from the TOML file") and are not auto-discoverable the way the eight register functions are. Only add them later if Task 9's build errors specifically point at `longjmp`/`setjmp` handling.

- [ ] **Step 3: Verify the TOML parses**

Run: `python3 -c "import tomllib,sys; tomllib.load(open('config/BigBumpin.toml','rb')); print('OK')"`
Expected: `OK`. (If this fails with a syntax error, fix the TOML — a trailing comment on the same line as a value, as used above, is valid TOML, but a copy-paste error is the likely cause.)

- [ ] **Step 4: Commit**

```bash
git add config/BigBumpin.toml
git commit -m "Add hand-authored XenonRecomp config for Big Bumpin'"
```

---

## Task 8: Run XenonRecomp to Generate C++ Output

**Files:**
- Create: `private/ppc/*.cpp`, `private/ppc/ppc_config.h` (gitignored, generated)

**Interfaces:**
- Consumes: `config/BigBumpin.toml` (Task 7), `build/XenonRecomp/XenonRecomp/XenonRecomp` (Task 3), `thirdparty/XenonRecomp/XenonUtils/ppc_context.h`.
- Produces: `private/ppc/*.cpp`, consumed by Task 9's `BigBumpinPPC` CMake target.

- [ ] **Step 1: Run XenonRecomp**

Run: `./build/XenonRecomp/XenonRecomp/XenonRecomp config/BigBumpin.toml thirdparty/XenonRecomp/XenonUtils/ppc_context.h`
Expected: Exit code `0`. Console output may include per-instruction warnings (e.g. about unimplemented instruction variants) — per the README these are expected and don't indicate failure.

- [ ] **Step 2: Verify generated output exists**

Run: `ls private/ppc/*.cpp | wc -l && test -f private/ppc/ppc_config.h && echo OK`
Expected: A count greater than `0`, and `OK`.

---

## Task 9: Compile Generated Code Into the BigBumpinPPC Static Library

This is the task where the approved design's "fast iterate" approach applies: build once, and if compiler errors point at a specific misdetected function boundary or invalid-instruction region, add a targeted config entry (exact syntax below, from XenonRecomp's own README) and repeat Task 8 + this task's build step. Continue until the build reports **zero compiler errors** — the Phase 1 spec's success criterion. Warnings for unimplemented instruction variants are expected and acceptable; do not attempt to fix those in this phase.

**Files:**
- Modify: `CMakeLists.txt` (repo root — append the `BigBumpinPPC` target block)
- Modify (conditionally, iteratively): `config/BigBumpin.toml` — add `functions`/`invalid_instructions` entries only if a build error points at one

**Interfaces:**
- Consumes: `private/ppc/*.cpp` (Task 8), `XenonUtils` CMake target (Task 3).
- Produces: `build/BigBumpinPPC.a` (or platform-equivalent static library file) — the deliverable that closes out Phase 1.

- [ ] **Step 1: Append the BigBumpinPPC target to the root CMakeLists.txt**

Modify `CMakeLists.txt`, adding at the end:

```cmake
file(GLOB BIGBUMPIN_PPC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc/*.cpp")

if(BIGBUMPIN_PPC_SOURCES)
    add_library(BigBumpinPPC STATIC ${BIGBUMPIN_PPC_SOURCES})
    target_include_directories(BigBumpinPPC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc")
    target_link_libraries(BigBumpinPPC PRIVATE XenonUtils)
    target_compile_options(BigBumpinPPC PRIVATE -Wno-unused-label -Wno-unused-variable -Wno-switch)
endif()
```

- [ ] **Step 2: Reconfigure (picks up the new generated sources) and build**

Run:
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BigBumpinPPC
```
Expected (best case): exit code `0`, `build/libBigBumpinPPC.a` exists. Go to Step 4.

- [ ] **Step 3: If the build reports compiler errors, fix and retry**

Read each error's source file (`private/ppc/*.cpp`) and address — the generated code is not meant to be edited directly (it's regenerated by Task 8, and hand-edits would be lost), so fixes go into `config/BigBumpin.toml` instead:

- **Error implies a function's boundary was misdetected** (e.g. code apparently falls through into unrelated bytes, or a jump table target is out of range): add an explicit boundary under `[main]` in `config/BigBumpin.toml`:
  ```toml
  functions = [
      { address = 0x82XXXXXX, size = 0xNN },
  ]
  ```
  (address = the function's start address from the error/disassembly context; size = its byte length, computed from where the *next* real function starts.)

- **Error implies the recompiler tried to decode non-code bytes as instructions** (e.g. padding or exception-handler data between functions, producing nonsense/invalid opcodes): add an entry to skip it:
  ```toml
  invalid_instructions = [
      { data = 0xXXXXXXXX, size = N },
  ]
  ```
  (data = the 32-bit word value to match; size = how many bytes to skip when it's encountered.)

After editing `config/BigBumpin.toml`, redo **Task 8 Step 1** (regenerate `private/ppc/`) and then this task's **Step 2** (rebuild). Repeat until the build is clean.

- [ ] **Step 4: Verify zero compiler errors in a clean build**

Run: `rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" -DCMAKE_BUILD_TYPE=Release && cmake --build build --target BigBumpinPPC 2>&1 | tee /tmp/bigbumpin_build.log && grep -c "error:" /tmp/bigbumpin_build.log`
Expected: The build command exits `0` and `grep -c "error:"` prints `0`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt config/BigBumpin.toml
git commit -m "Compile generated Big Bumpin' PPC code into BigBumpinPPC static library"
```

This closes Phase 1. `build/libBigBumpinPPC.a` now exists but is not linked into anything runnable — Phase 2 (minimal bootable runtime, kernel stubs) is a separate future spec/plan.

---

## Plan Self-Review Notes

- **Spec coverage:** All five Phase 1 success criteria from the spec map onto tasks: (1) Task 3 (tool build+run, revised per the spec amendment), (2) Task 4 (extraction+magic check), (3) Task 6 (XenonAnalyse run), (4) Task 8 (XenonRecomp run), (5) Task 9 (zero-compiler-error static lib).
- **Placeholder scan:** The only `0x00000000` values in the plan are explicitly template slots in Task 7 with a documented, concrete fill-in procedure (copy from Task 5's tool output) — not unresolved TBDs.
- **Type/interface consistency:** `Image`, `Section`, `SectionFlags_Code`, `LoadFile` in Task 5's tool match their actual declarations in `thirdparty/XenonRecomp/XenonUtils/{image.h,section.h,file.h}`, verified by reading those headers directly rather than assuming an API shape.
