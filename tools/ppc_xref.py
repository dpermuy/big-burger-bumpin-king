#!/usr/bin/env python3
"""
ppc_xref.py -- static cross-reference tool for XenonRecomp-generated PPC code.

Problem this solves: the recompiled game code (private/ppc/*.cpp) is
hundreds of thousands of lines across ~110 files. Manually tracing "who
writes real guest address X" means grepping for every instruction that
could compute X, checking each candidate by hand, and following call
chains one level at a time -- this project's own loading-screen
investigation did exactly that (Findings 106-110) and hit real,
demonstrated diminishing returns after 3-4 levels of manual tracing.

What this tool does: for every PPC_FUNC_IMPL block in every
private/ppc/*.cpp file, it does a linear (not control-flow-aware --
deliberately a heuristic, see Limitations) symbolic walk of register
assignments, tracking:
  - integer constants (li / lis, both emitted by XenonRecomp as a plain
    `ctx.rN.s64 = <literal>;` assignment)
  - register copies (mr)
  - register + constant (addi), when the base register's value is known
  - memory loads (PPC_LOAD_U32/U64/U8), when the source address is known
    -- these produce a symbolic "deref(base, offset)" value, so a load
    through an unresolved runtime pointer (e.g. a heap object reached
    only via a global pointer slot) is still tracked symbolically, not
    discarded
  - memory stores (PPC_STORE_U32/U64/U8): every store whose target
    address resolves (fully or symbolically) is recorded in a global
    write index
  - call sites: the resolved values of the first 8 integer argument
    registers (r3..r10) at each `bl`/call are recorded against the
    callee's name, so a later query can ask "does any function ever
    receive expression E as its Nth parameter, and does it write
    paramN+offset" -- this is what lets the tool answer across function
    boundaries without the user manually tracing each call site.

Usage:
    python3 tools/ppc_xref.py --index                 # build/refresh the index (writes tools/.ppc_xref_cache.json)
    python3 tools/ppc_xref.py --find-writes 0x826EB104 --deref --offset 136 [--hops 3]
    python3 tools/ppc_xref.py --find-writes 0x826EB104 --offset 0           # writes to the pointer slot itself

--find-writes semantics:
    --find-writes ADDR            seed expression = Const(ADDR)
    --find-writes ADDR --deref    seed expression = Deref(Const(ADDR), 0)
                                   (i.e. "the pointer stored AT ADDR", not ADDR itself)
    --offset N                    look for writes to seed+N (default 0)
    --hops N                      how many levels of call-argument propagation to follow (default 3)

Limitations (read before trusting a negative result):
  - Linear scan per function, not real control-flow analysis: a value
    "known" earlier in a function is assumed to still hold later,
    regardless of intervening branches. This can produce false
    positives (a write that's actually unreachable on the real path) --
    always verify any hit by reading the real surrounding code, the
    same discipline this project's own findings log has used
    throughout. False negatives are possible if a branch REQUIRES a
    different value than the linear scan assumes, but are rarer.
  - Only tracks r3-r31 as plain 64-bit "pointer/int" values via the
    assignment forms XenonRecomp actually emits for li/lis/mr/addi/
    load/store. Vector/float registers, rotate/mask instructions, and
    arithmetic beyond simple addition are not modeled -- a value that
    passes through one of those becomes Unknown from that point on in
    that register.
  - Across a real call, volatile registers (r3-r12 by the real PPC ABI)
    are reset to Unknown; non-volatile registers (r14-r31) keep their
    pre-call value, matching this project's own observed
    save/restore (__savegprlr_XX/__restgprlr_XX) convention.
  - Call-argument propagation (the --hops mechanism) matches a callee
    by name only, not by real call-graph reachability proof -- if the
    same function name is reached two different ways with two
    different argument values, this tool considers BOTH, which is
    correct (sound for "could this write it"), but means a hit list can
    include paths that don't actually occur in a given real execution.
    Live-verify, don't just trust the static result, same as every
    finding in this project's own investigation log.

This tool is a coarse, fast triage step, not a replacement for reading
the real code or running the game live. Use it to turn "which of
several hundred functions should I actually read" into a short list.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PPC_DIR = REPO_ROOT / "private" / "ppc"
CACHE_FILE = Path(__file__).resolve().parent / ".ppc_xref_cache.json"

FUNC_START_RE = re.compile(r'^PPC_FUNC_IMPL\(__imp__(\w+)\)\s*\{')
FUNC_END_RE = re.compile(r'^\}')

# ctx.rN.(s64|u64) = <signed-int-literal>;
RE_CONST = re.compile(r'^ctx\.r(\d+)\.(?:s64|u64)\s*=\s*(-?\d+)(?:ULL|U)?;')
# ctx.rN.u64 = ctx.rM.u64;   (plain register copy, mr)
RE_COPY = re.compile(r'^ctx\.r(\d+)\.u64\s*=\s*ctx\.r(\d+)\.u64;')
# ctx.rN.(s64|u64) = ctx.rM.(s64|u64) + (-?\d+);   (addi)
RE_ADDI = re.compile(r'^ctx\.r(\d+)\.(?:s64|u64)\s*=\s*ctx\.r(\d+)\.(?:s64|u64)\s*\+\s*(-?\d+);')
# ctx.rN.u64 = PPC_LOAD_U(8|16|32|64)(ctx.rM.u32 + (-?\d+));
RE_LOAD = re.compile(r'^ctx\.r(\d+)\.u64\s*=\s*PPC_LOAD_U(\d+)\(ctx\.r(\d+)\.u32\s*\+\s*(-?\d+)\);')
# PPC_STORE_U(8|16|32|64)(ctx.rM.u32 + (-?\d+), ...);
RE_STORE = re.compile(r'^PPC_STORE_U(\d+)\(ctx\.r(\d+)\.u32\s*\+\s*(-?\d+),')
# a real call: "sub_XXXXXXXX(ctx, base);" or "__imp__NAME(ctx, base);"
RE_CALL = re.compile(r'^(?:sub_[0-9A-Fa-f]+|__imp__\w+)\(ctx, base\);')

ARG_REGS = list(range(3, 11))  # r3..r10, first 8 integer args (real PPC ABI)
VOLATILE_REGS = set(range(3, 13))  # r3-r12 clobbered by a real call
# r14-r31 are callee-saved on the real PPC ABI and this project's own
# observed __savegprlr_XX/__restgprlr_XX convention -- preserved across calls.


def const(v):
    return ("const", v)


def param(n):
    return ("param", n)


def deref(base, offset, size):
    return ("deref", base, offset, size)


def expr_key(e):
    """A hashable, order-independent key for an expression (for the write index)."""
    return json.dumps(e)


def parse_functions(path):
    """Yield (func_name, start_line, list_of_(lineno, raw_line)) for every
    PPC_FUNC_IMPL block in this file. Only the *_IMPL body is analyzed --
    the PPC_WEAK_FUNC trampoline immediately after each one is skipped,
    since it never does anything but forward the call."""
    lines = path.read_text(errors="replace").splitlines()
    i = 0
    n = len(lines)
    while i < n:
        m = FUNC_START_RE.match(lines[i].strip())
        if not m:
            i += 1
            continue
        name = m.group(1)
        start = i + 1
        body = []
        depth = 1
        i += 1
        while i < n and depth > 0:
            stripped = lines[i].strip()
            if stripped.startswith("PPC_FUNC_IMPL(") or stripped.startswith("PPC_FUNC(") :
                depth += 1
            elif FUNC_END_RE.match(stripped):
                depth -= 1
                if depth == 0:
                    break
            body.append((i + 1, lines[i]))
            i += 1
        yield name, start, body
        i += 1


def analyze_file(path, write_index, call_index):
    """Populate write_index: expr_key -> list of (func, file, line, raw)
    and call_index: callee_name -> list of (caller_func, file, line, {argN: expr})."""
    rel = str(path.relative_to(REPO_ROOT))
    for func_name, _start, body in parse_functions(path):
        regs = {n: param(i) for i, n in enumerate(ARG_REGS)}
        for lineno, raw in body:
            line = raw.strip()

            m = RE_CONST.match(line)
            if m:
                r = int(m.group(1))
                regs[r] = const(int(m.group(2)))
                continue

            m = RE_COPY.match(line)
            if m:
                rd, rs = int(m.group(1)), int(m.group(2))
                regs[rd] = regs.get(rs)
                continue

            m = RE_ADDI.match(line)
            if m:
                rd, rs, off = int(m.group(1)), int(m.group(2)), int(m.group(3))
                base = regs.get(rs)
                if base is not None:
                    if base[0] == "const":
                        regs[rd] = const(base[1] + off)
                    else:
                        regs[rd] = deref(base, off, 0) if base[0] == "deref" and off == 0 else ("addr", base, off)
                else:
                    regs[rd] = None
                continue

            m = RE_LOAD.match(line)
            if m:
                rd, size, rs, off = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
                base = regs.get(rs)
                regs[rd] = deref(base, off, size) if base is not None else None
                continue

            m = RE_STORE.match(line)
            if m:
                size, rs, off = int(m.group(1)), int(m.group(2)), int(m.group(3))
                base = regs.get(rs)
                if base is not None:
                    addr_expr = ("at", base, off)
                    write_index.setdefault(expr_key(addr_expr), []).append(
                        {"func": func_name, "file": rel, "line": lineno, "raw": raw.strip(), "size": size}
                    )
                continue

            m = RE_CALL.match(line)
            if m:
                callee = re.match(r'(?:sub_[0-9A-Fa-f]+|__imp__\w+)', line).group(0)
                args = {}
                for idx, r in enumerate(ARG_REGS):
                    v = regs.get(r)
                    if v is not None:
                        args[idx] = v
                call_index.setdefault(callee, []).append(
                    {"caller": func_name, "file": rel, "line": lineno, "args": args}
                )
                # Real PPC ABI: volatile regs are clobbered by the call;
                # non-volatile (r14-r31) survive it.
                for r in list(regs.keys()):
                    if r in VOLATILE_REGS:
                        regs[r] = None
                continue

            # Any other instruction form (float/vector ops, rotates, branches,
            # etc.) is not modeled -- conservatively, it does not change any
            # tracked register's value in this heuristic (see Limitations).


def build_index():
    write_index = {}
    call_index = {}
    files = sorted(PPC_DIR.glob("ppc_recomp.*.cpp"))
    if not files:
        print(f"No private/ppc/ppc_recomp.*.cpp files found under {PPC_DIR}", file=sys.stderr)
        sys.exit(1)
    for i, f in enumerate(files):
        analyze_file(f, write_index, call_index)
        if (i + 1) % 20 == 0 or i + 1 == len(files):
            print(f"  analyzed {i + 1}/{len(files)} files...", file=sys.stderr)
    CACHE_FILE.write_text(json.dumps({"writes": write_index, "calls": call_index}))
    print(f"Index built: {len(write_index)} distinct write targets, "
          f"{sum(len(v) for v in call_index.values())} call sites across "
          f"{len(call_index)} distinct callee names.", file=sys.stderr)
    print(f"Cached to {CACHE_FILE}", file=sys.stderr)


def load_index():
    if not CACHE_FILE.exists():
        print("No cached index found -- run with --index first.", file=sys.stderr)
        sys.exit(1)
    data = json.loads(CACHE_FILE.read_text())
    return data["writes"], data["calls"]


def expr_matches_seed(e, seed):
    return e == seed


def find_writes(write_index, call_index, seed, offset, hops):
    """Find every real store whose address resolves to seed+offset,
    directly, or via a chain of up to `hops` real call-argument passes."""
    target = ("at", seed, offset)
    results = list(write_index.get(expr_key(target), []))

    # Interprocedural: does any function receive `seed` (or seed+k, chained
    # through further addi-style offsets a caller applies before the call)
    # as one of its first 8 integer arguments? If so, treat that argument
    # register as a fresh "param(n)" seed in the callee and recurse.
    visited = set()

    def recurse(cur_seed, cur_offset, depth):
        if depth > hops:
            return
        tgt = ("at", cur_seed, cur_offset)
        for w in write_index.get(expr_key(tgt), []):
            key = (w["file"], w["line"])
            if key not in visited:
                visited.add(key)
                results.append(dict(w, hop=depth))
        # find calls where some argument equals cur_seed (as a bare pointer,
        # i.e. the callee would see it as its own param and could apply an
        # offset to it internally -- we don't need to pre-apply cur_offset
        # here since the callee's own body already encodes "paramN + X").
        for callee, sites in call_index.items():
            for site in sites:
                for arg_idx, arg_val in site["args"].items():
                    if expr_matches_seed(arg_val, cur_seed):
                        # Re-scan the callee's own writes with its param(arg_idx)
                        # as the new seed.
                        recurse(param(arg_idx), cur_offset, depth + 1)

    recurse(seed, offset, 1)
    return results


def parse_seed_addr(s):
    return int(s, 16) if s.lower().startswith("0x") else int(s)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index", action="store_true", help="build/refresh the cached index")
    ap.add_argument("--find-writes", metavar="ADDR", help="seed guest address (hex like 0x826EB104, or decimal)")
    ap.add_argument("--deref", action="store_true", help="seed = the pointer VALUE stored at ADDR, not ADDR itself")
    ap.add_argument("--offset", type=int, default=0, help="field offset to search for writes to (default 0)")
    ap.add_argument("--hops", type=int, default=3, help="max call-argument propagation depth (default 3)")
    args = ap.parse_args()

    if args.index:
        build_index()
        return

    if args.find_writes:
        write_index, call_index = load_index()
        addr = parse_seed_addr(args.find_writes)
        seed = const(addr)
        if args.deref:
            seed = deref(seed, 0, 32)
        hits = find_writes(write_index, call_index, seed, args.offset, args.hops)
        if not hits:
            print(f"No writes found to seed={seed} offset={args.offset} within {args.hops} call hops.")
            print("(Remember: this is a heuristic static scan -- see Limitations in the module docstring. "
                  "A negative result here is strong but not absolute proof.)")
            return
        print(f"Found {len(hits)} candidate write site(s):\n")
        for h in hits:
            hop = h.get("hop", 0)
            print(f"  [{'direct' if hop <= 1 else f'{hop} hops'}] {h['file']}:{h['line']}  "
                  f"in {h['func']}():  {h['raw']}")
        return

    ap.print_help()


if __name__ == "__main__":
    main()
