# Phase 1: Static Recompilation Pipeline — Big Bumpin' Recomp

## Context

Long-term goal: play a personally-owned Xbox 360 backup ("Big Bumpin'") natively
on Mac/PC via static recompilation, following the approach pioneered by
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) / UnleashedRecomp.

XenonRecomp only converts a Xbox 360 executable's PowerPC machine code into
portable C++ — it provides no runtime. A playable game requires a full custom
runtime (graphics, audio, input, kernel shims, game-specific fixes) built on
top of the generated code, which is a large, multi-phase effort. This spec
covers only the first, foundational phase.

## Roadmap (for context — only Phase 1 is in scope here)

1. **Static recompilation pipeline** (this spec)
2. Minimal bootable runtime (kernel stubs, reach a black window)
3. Graphics translation (Xenos → Vulkan/MoltenVK or Metal)
4. Audio (XMA decode/playback)
5. Input
6. Game-specific fixes
7. Packaging/distribution

Phases 2–7 are out of scope and will each get their own spec once Phase 1 is
working.

## Goal

Produce a compiling C++ static library from the game's `default.xex`, proving
the extraction → analysis → recompilation → build pipeline works end-to-end
for this specific game. The library does not run or link into an executable
yet — it only needs to compile.

## Repo Layout

```
Royal Burger Kart/               (git repo root)
├── thirdparty/XenonRecomp/       (git submodule: hedge-dev/XenonRecomp)
├── private/                      (gitignored — ISO, extracted XEX, generated ppc/ code)
├── config/BigBumpin.toml         (hand-authored recompiler config, committed)
├── CMakeLists.txt                (top-level, builds BigBumpinPPC static lib)
└── docs/superpowers/specs/       (design docs)
```

`private/` is gitignored. The extracted XEX and the C++ generated from it are
derived directly from copyrighted game binary data and must not be committed.
Only pipeline glue — hand-written config, CMake, docs — is version controlled.

## Toolchain

- `cmake`, `ninja` — installed via Homebrew
- `llvm` (Homebrew, provides Clang 18+) — used explicitly instead of Apple's
  Xcode Clang. XenonRecomp's README states other compilers "have not been
  tested and are not recommended" and relies on Clang-specific intrinsics;
  Apple's Clang is a divergent fork with its own versioning, so Homebrew's
  upstream LLVM Clang is the safer match for the project's assumptions.
- `xdvdfs-cli` (antangelo/xdvdfs) — installed via `cargo install xdvdfs-cli`,
  used to unpack the Xbox 360 XDVDFS disc image
- `XenonAnalyse`, `XenonRecomp`, `XenonTests` — built from the
  `thirdparty/XenonRecomp` submodule using its own CMake build. This machine
  is Apple Silicon (arm64), so VMX instruction implementations route through
  the SIMD Everywhere (simde) backend rather than native x86 intrinsics.

## Data Flow

```
ISO --xdvdfs unpack--> private/default.xex (+ private/default.xexp if present)
    --XenonAnalyse--> private/switch_tables.toml
    --XenonRecomp (+ config/BigBumpin.toml)--> private/ppc/*.cpp, ppc_config.h
    --CMake target BigBumpinPPC--> compiled static library (.a)
```

## Approach

Config iteration is fast-and-loose rather than a full upfront disassembly
pass: run `XenonAnalyse` as-is (its jump-table detection is tuned for Sonic
Unleashed's compiler output and may not catch everything in this game),
hand-write a minimal `config/BigBumpin.toml`, attempt the build, and patch
config only where compiler errors point at a real problem (e.g. a
misdetected function boundary). A full disassembly-first pass (Ghidra/IDA)
is deferred — it's better justified later when chasing actual gameplay bugs
(Phase 6) than for just getting generated code to compile.

## Success Criteria

Phase 1 is complete when all of the following hold:

1. `XenonAnalyse` and `XenonRecomp` build successfully via CMake+Ninja and
   run without crashing (invoked with no args, each prints usage) —
   validates the toolchain compiles and runs on this machine (arm64/simde
   path) independently of any game-specific config issues. (Originally
   scoped as "XenonTests builds and passes," but XenonTests requires PPC
   test binaries generated from a full Xenia build — a large separate
   dependency — and its CMakeLists hardcodes an x86-only `-march=sandybridge`
   flag that fails on arm64. Deferred; not worth the cost for a sanity
   check.)
2. `xdvdfs unpack` extracts a valid `default.xex` from the ISO.
3. `XenonAnalyse` runs against `default.xex` without crashing and produces a
   switch-table TOML (may legitimately be empty on the first pass).
4. `XenonRecomp` runs against `default.xex` + `config/BigBumpin.toml` and
   produces C++ output under `private/ppc/`.
5. The `BigBumpinPPC` CMake target compiles that generated output plus
   XenonUtils into a static library with **zero compiler errors**. Compiler
   *warnings* for unimplemented instruction variants are expected and
   acceptable at this stage (per XenonRecomp's own documentation).

## Explicitly Out of Scope

- Resolving Xbox 360 kernel imports
- Linking into a runnable executable
- Graphics, audio, or input handling
- Actually running/booting the game

Build failures during config iteration (step 5) are a normal, expected part
of the development loop — not an error condition to design handling for.
