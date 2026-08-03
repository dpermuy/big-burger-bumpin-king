#include "ppc_config.h"
#include <ppc_context.h>

#include <chrono>
#include <thread>

// Finding 61: a targeted, host-level override of ONE specific auto-generated PPC
// function -- not a kernel export, a real in-game routine (sub_820B4EE8, XenonRecomp's
// PPC_WEAK_FUNC pattern makes this a plain C++-mangled weak symbol any strong
// definition elsewhere in the link takes priority over -- see
// thirdparty/XenonRecomp/XenonUtils/ppc_context.h). No extern "C", no __imp__ prefix:
// the signature must match PPC_FUNC's plain (mangled) form exactly so the linker
// resolves every call site in the auto-generated code to this definition instead.
//
// Real contract (private/ppc/ppc_recomp.4.cpp:10961-11055, confirmed via full static
// trace and multiple live captures, Findings 56/57/58/59/60): given self (r3) and a
// candidate write position (r4), busy-wait until the EVENT_WRITE_SHD progress fence
// at [[self+10768]+4] satisfies (target - fence) & 3 == 0, or ((target - fence) & 3
// == 1 AND target <= (fence & ~3)). No return value is consumed by any real caller
// (confirmed: both call sites in sub_820B53C0 discard r3 afterward).
//
// Confirmed live (Finding 56) this wait deadlocks permanently for this title's own
// self+13484-13508 scratch-buffer reservation path: the only thread that could ever
// advance the fence is the same thread stuck here, so once entered with a target
// past the fence, it can never exit on its own. Real hardware already has a ~5-second
// watchdog escape valve for exactly this class of stall (Finding 52), but this title
// never registers the hang-notify callback (self+13064) that escape depends on, so it
// silently never fires either.
//
// Two prior attempts at the same underlying problem both targeted the SHARED fence
// value directly and were reverted: Finding 55's PPC-level decrement (insufficient --
// only ever fired once per run) and Finding 58's host-level force-advance (a genuine
// regression, confirmed via a direct 900s A/B test -- forcing the shared fence ahead
// of real progress corrupted some other consumer's view of it, causing an EARLIER,
// tighter stall than doing nothing at all). This override touches no guest memory
// whatsoever -- it only bounds THIS function's own control flow, reimplementing the
// exact real wait condition faithfully but with a short, reliable, host-timed escape
// instead of depending on the guest's own broken watchdog chain. Nothing else in the
// pipeline can be corrupted by a change that never writes anything.
void sub_820B4EE8(PPCContext& __restrict ctx, uint8_t* base)
{
    uint32_t self = static_cast<uint32_t>(ctx.r3.u64);
    uint32_t target = static_cast<uint32_t>(ctx.r4.u64);
    uint32_t structPtr = PPC_LOAD_U32(self + 10768);
    if (structPtr == 0)
    {
        return;
    }

    // Real hardware waits up to ~5000ms (Finding 52) before giving up; this title's
    // own escape for that timeout is broken, so bound it far tighter -- there's
    // nothing to gain by spinning anywhere near that long once we know this specific
    // deadlock class exists, and every real (non-stuck) case resolves within this
    // wait almost immediately since the fence tracks real EVENT_WRITE_SHD execution.
    constexpr auto kMaxWait = std::chrono::milliseconds(50);
    const auto start = std::chrono::steady_clock::now();

    while (true)
    {
        uint32_t fence = PPC_LOAD_U32(structPtr + 4);
        uint32_t diff = target - fence;
        if ((diff & 0x3u) == 0)
        {
            return;
        }
        if ((diff & 0x3u) == 1 && target <= (fence & ~0x3u))
        {
            return;
        }
        if (std::chrono::steady_clock::now() - start > kMaxWait)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

PPC_EXTERN_FUNC(sub_820B6220);

// Finding 63: a second, independent instance of the exact same class of problem
// Finding 61 fixed, on a completely different call chain -- confirmed live via lldb
// thread backtrace (two samples, 30s apart, same tight loop both times) that the
// MAIN game thread, not any worker thread, gets stuck inside sub_820B9A58 via this
// function, reached through the original Finding 19-24/38/43 call chain
// (sub_820CCA68 -> sub_824A76D0 -> ... -> sub_8212B130 -> __xstart), independent of
// self+13484-13508 (Finding 61 already covers that one; it doesn't reach this path).
//
// Real contract (private/ppc/ppc_recomp.4.cpp:12980-13101, confirmed against
// Finding 43/44's own live captures of this exact function): given self (r3) and a
// target (r4), wait until current -- *(*(self+10768)+0), a DIFFERENT field of the
// same 96-byte fence block sub_820B4EE8 also uses (that one reads offset+4) --
// reaches target. Finding 20-24 (early in this investigation, well before any of
// this session's fixes) already established current's real nature: it's a
// dispatcher-object-style signal count that's set once at creation and never
// incremented again by anything this project implements -- no code path (vblank
// ISR, PM4_INTERRUPT ISR, or otherwise) ever writes self+10768+0. That diagnosis
// still holds; every fix since then (Findings 51-62) addressed other fields of the
// same pipeline, none of which touch this one.
//
// The real function has a genuine producer side effect worth preserving: when the
// target being waited for is exactly the current self+10780 expectation value and
// self+12944 (an in-progress flag) is clear, it proactively calls sub_820B6220 (the
// same top-level flush entry point Finding 54 mapped) to try to produce the very
// progress it's about to wait for. Preserved here by calling the real function
// directly (a normal extern PPC call, not touching any shared state ourselves) --
// its own effects go through the already-safe, already-overridden pipeline
// (including Finding 61's own sub_820B4EE8 override further down that call chain).
// Bounded the same way as Finding 61: short, reliable, host-timed escape instead of
// the guest's own watchdog chain, since current is confirmed to never legitimately
// advance on its own regardless of how long anything waits.
void sub_820B5BC8(PPCContext& __restrict ctx, uint8_t* base)
{
    uint32_t self = static_cast<uint32_t>(ctx.r3.u64);
    uint32_t target = static_cast<uint32_t>(ctx.r4.u64);
    if (target == 0)
    {
        return;
    }

    uint32_t structPtr = PPC_LOAD_U32(self + 10768);
    if (structPtr == 0)
    {
        return;
    }

    auto current = [&]() { return PPC_LOAD_U32(structPtr + 0); };

    if (target <= current())
    {
        return;
    }

    uint32_t expectation = PPC_LOAD_U32(self + 10780);
    uint32_t inProgress = PPC_LOAD_U32(self + 12944);
    if (target == expectation && inProgress == 0)
    {
        ctx.r3.u64 = self;
        sub_820B6220(ctx, base);
    }

    if (target <= current())
    {
        return;
    }

    constexpr auto kMaxWait = std::chrono::milliseconds(50);
    const auto start = std::chrono::steady_clock::now();
    while (target > current())
    {
        if (std::chrono::steady_clock::now() - start > kMaxWait)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}
