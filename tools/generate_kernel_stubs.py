#!/usr/bin/env python3
"""One-time generator for host/kernel_stubs.cpp from host/kernel_imports.txt.

Not part of the build. Run manually when host/kernel_imports.txt changes.
Phase 2B edits host/kernel_stubs.cpp by hand afterward -- re-running this
script would overwrite those edits, so it is not wired into CMake.
"""

TEMPLATE = '''PPC_FUNC(__imp__{name})
{{
    fmt::println("[stub] {name}(r3=0x{{:X}}, r4=0x{{:X}}, r5=0x{{:X}}, r6=0x{{:X}})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
{comment}    ctx.r3.u64 = 0;
}}
'''

SPECIAL_COMMENTS = {
    "ExCreateThread": (
        "    // Deliberate Phase 2A scope cut: reports success but does not spawn a\n"
        "    // real host thread or execute the requested guest function. Real\n"
        "    // thread semantics are Phase 2B work.\n"
    ),
}


def main():
    with open("host/kernel_imports.txt") as f:
        names = [line.strip() for line in f if line.strip()]

    with open("host/kernel_stubs.cpp", "w") as out:
        out.write('#include "ppc_config.h"\n')
        out.write('#include <ppc_context.h>\n')
        out.write('#include <fmt/core.h>\n\n')
        for name in names:
            comment = SPECIAL_COMMENTS.get(name, "")
            out.write(TEMPLATE.format(name=name, comment=comment))
            out.write("\n")

    print(f"Wrote {len(names)} stub functions to host/kernel_stubs.cpp")


if __name__ == "__main__":
    main()
