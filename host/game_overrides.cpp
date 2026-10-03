#include "ppc_config.h"
#include <ppc_context.h>

#include <chrono>
#include <thread>

// Finding 61, corrected by Findings 90-95: a targeted, host-level override of ONE
// specific auto-generated PPC function -- not a kernel export, a real in-game routine
// (sub_820B4EE8, XenonRecomp's PPC_WEAK_FUNC pattern makes this a plain C++-mangled
// weak symbol any strong definition elsewhere in the link takes priority over -- see
// thirdparty/XenonRecomp/XenonUtils/ppc_context.h). No extern "C", no __imp__ prefix:
// the signature must match PPC_FUNC's plain (mangled) form exactly so the linker
// resolves every call site in the auto-generated code to this definition instead.
//
// Real contract (private/ppc/ppc_recomp.4.cpp:10961-11055, read end to end in Finding
// 92 -- Finding 61's own paraphrase below was wrong about which register is which):
// given self (r3), a SECOND value x (r4), and target (r5), the real INITIAL gate
// (before any wait) compares the fence at [[self+10768]+4] against r5/target: done if
// (target-fence)&3==0, or if (target-fence)&3==1 AND target<=(fence&~3). If not
// satisfied, the real code enters a wait loop (sub_820B98A8/sub_820B9A58/sub_820B98D8)
// whose OWN internal recheck compares the fence against r4/x instead -- a different
// value from the initial gate, not a restatement of it. Finding 61 collapsed both into
// a single "target" read from r4 only, silently dropping r5 and gating on the wrong
// register for the primary check. Fixed here: gate on r5 first (matching the real
// initial check exactly), then loop rechecking against r4 (matching the real wait
// loop exactly) -- faithful to the real two-parameter contract instead of a collapsed
// approximation. No return value is consumed by any real caller (confirmed: both call
// sites in sub_820B53C0 discard r3 afterward).
//
// Confirmed live (Finding 56) this wait deadlocks permanently for this title's own
// self+13484-13508 scratch-buffer reservation path: the only thread that could ever
// advance the fence is the same thread stuck here, so once entered with a target past
// the fence, it can never exit on its own *within a single frame*. But Finding 94 (live
// gpu_trace.log correlation) proved the real fence is NOT frozen across the whole run --
// it genuinely advances, just sparsely and in bursts (a handful of times per second,
// tied to real VdSwap completions per Finding 95), because sub_820CCA68's own VdSwap
// call is what arms the next real EVENT_WRITE_SHD, and that same VdSwap can't run until
// other waits earlier in the same frame resolve -- a real, circular, per-frame pacing
// dependency, not a fully dead fence. Finding 61's original 50ms bound was tight enough
// to reliably miss that bursty real progress and give up before it arrived. Finding 92
// also found the real hardware behavior on true, permanent timeout (self+13064's
// hang-notify never registered by this title) is a deliberate assert/crash (`twi
// 31,r0,22`, Finding 93) after ~5000ms (Finding 52) -- not a silent hang. This override
// still can't safely reproduce that crash (it would just kill the host process instead
// of showing a real debug break), so it keeps the bounded-return behavior, but widens
// the bound from 50ms toward that same real ~5000ms figure so genuine bursty progress
// gets a real chance to land before giving up, instead of almost always missing it.
//
// Two prior attempts at the same underlying problem both targeted the SHARED fence
// value directly and were reverted: Finding 55's PPC-level decrement (insufficient --
// only ever fired once per run) and Finding 58's host-level force-advance (a genuine
// regression, confirmed via a direct 900s A/B test -- forcing the shared fence ahead
// of real progress corrupted some other consumer's view of it, causing an EARLIER,
// tighter stall than doing nothing at all). This override still touches no guest memory
// whatsoever -- it only bounds THIS function's own control flow. Nothing else in the
// pipeline can be corrupted by a change that never writes anything.
void sub_820B4EE8(PPCContext& __restrict ctx, uint8_t* base)
{
    uint32_t self = static_cast<uint32_t>(ctx.r3.u64);
    uint32_t x = static_cast<uint32_t>(ctx.r4.u64);
    uint32_t target = static_cast<uint32_t>(ctx.r5.u64);
    uint32_t structPtr = PPC_LOAD_U32(self + 10768);
    if (structPtr == 0)
    {
        return;
    }

    auto satisfied = [&](uint32_t value)
    {
        uint32_t fence = PPC_LOAD_U32(structPtr + 4);
        uint32_t diff = value - fence;
        if ((diff & 0x3u) == 0)
        {
            return true;
        }
        return (diff & 0x3u) == 1 && value <= (fence & ~0x3u);
    };

    // Real initial gate (r5/target) -- matches ppc_recomp.4.cpp:10983-10999 exactly.
    if (satisfied(target))
    {
        return;
    }

    // Real hardware waits up to ~5000ms (Finding 52) before its own hang-notify/crash
    // escape (Finding 92/93). Finding 94 showed genuine bursty progress can take up to
    // several seconds to land (a real ~6.5s gap was observed at the very start of a
    // run), so bound this near that same real figure rather than the far tighter 50ms
    // Finding 61 originally used -- wide enough to let real progress actually arrive,
    // still far short of Finding 56's original unbounded spin.
    constexpr auto kMaxWait = std::chrono::milliseconds(5000);
    const auto start = std::chrono::steady_clock::now();

    // Real wait loop's own recheck (r4/x) -- matches ppc_recomp.4.cpp:11020-11040.
    while (!satisfied(x))
    {
        if (std::chrono::steady_clock::now() - start > kMaxWait)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

PPC_EXTERN_FUNC(sub_820B6220);

// Finding 102: fix attempt for the real emulation-fidelity gap Findings 90-101
// precisely diagnosed (not a logic bug in the game, not a wait-bound issue --
// both already fixed/correct, Finding 96-98). sub_820B5A28 (real contract,
// private/ppc/ppc_recomp.4.cpp:12733-12851) has its own real, already-compiled
// DIRECT write to this fence (structPtr+0 = old expectation, structPtr+4 =
// (self+13500 & 3) | self+40), which would track real demand 1:1 and make the
// slow, game-emitted EVENT_WRITE_SHD mechanism (Finding 37/94/95, only ~1
// completion per 5-7 real frames, Finding 100) unnecessary for this object --
// but it only fires when self+10808 bit 0x80 is clear AND a real global flag
// (0x8265C6A0) is nonzero AND self+10809 bit 0x2 isn't already latched
// (sub_820B6220's own one-time resync sequence, private/ppc/ppc_recomp.4.cpp:
// 14085-14125). Live-confirmed (temporary probe, since reverted) that global
// flag reads 0 for an entire run -- this real gate never opens in this disc
// image/boot path, for reasons not yet understood (plausibly a real, legitimate
// feature flag that's off by design, not necessarily a bug).
//
// Rather than guess why that flag is 0 or force it (touching a real, shared
// global flag's value is a much bigger blast-radius change than this specific
// fence), this override reproduces ONLY sub_820B5A28's own already-compiled
// direct-write values -- the exact real numbers the game's own code already
// computes and would write if that one gate were open -- and applies them
// unconditionally. This is not host-invented data: every value written here is
// read from the same real guest memory the original function already reads,
// using the same real formula. Calls through to the real implementation
// afterward, which still does its own unconditional expectation+=2 and its own
// (still-closed) gated check -- harmless, no double-write, since real gate
// stays shut.
extern "C" PPC_EXTERN_FUNC(__imp__sub_820B5A28);
void sub_820B5A28(PPCContext& __restrict ctx, uint8_t* base)
{
    uint32_t self = static_cast<uint32_t>(ctx.r3.u64);
    uint32_t structPtr = PPC_LOAD_U32(self + 10768);
    if (structPtr != 0)
    {
        uint32_t oldExpectation = PPC_LOAD_U32(self + 10780);
        uint32_t cursorFlags = (PPC_LOAD_U32(self + 13500) & 0x3u) | PPC_LOAD_U32(self + 40);
        PPC_STORE_U32(structPtr + 0, oldExpectation);
        PPC_STORE_U32(structPtr + 4, cursorFlags);
    }
    __imp__sub_820B5A28(ctx, base);
}

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
// SAME 96-byte fence block sub_820B4EE8 also uses (that one reads offset+4) --
// reaches target. Finding 20-24 (early in this investigation) diagnosed current as
// permanently dead (no code path ever writes it) -- but that predates Finding 37's
// real PM4_EVENT_WRITE_SHD execution. Re-tested twice, live, with proper rigor
// (rather than trusting either the old diagnosis or a short sample): a first,
// short (60s) run showed current() genuinely advancing throughout, in lockstep
// with the sibling field sub_820B4EE8 uses -- confirmed it's the SAME real object
// (structPtr=0xB9405000 both times), so Finding 20-24's diagnosis is stale, not
// current. A second, longer (180s) run then showed current() genuinely FREEZE at a
// fixed value (7305) while target kept climbing -- the exact same real, bursty,
// VdSwap-tied rate mismatch Finding 94/95 already diagnosed for this object's
// sibling field, not a permanently dead one. sub_820CCA68 calls this function AFTER
// its own frame's VdSwap (Finding 95), so a failed wait here blocks THIS frame's
// own completion, which blocks the NEXT frame's VdSwap, which is what would produce
// the NEXT real EVENT_WRITE_SHD burst this wait needs -- the identical circular
// pacing dependency, one call further down the same real path.
//
// The real function has a genuine producer side effect worth preserving: when the
// target being waited for is exactly the current self+10780 expectation value and
// self+12944 (an in-progress flag) is clear, it proactively calls sub_820B6220 (the
// same top-level flush entry point Finding 54 mapped) to try to produce the very
// progress it's about to wait for. Preserved here by calling the real function
// directly (a normal extern PPC call, not touching any shared state ourselves) --
// its own effects go through the already-safe, already-overridden pipeline
// (including sub_820B4EE8's own override further down that call chain).
// Bound widened from an original, tighter 50ms to the real hardware's own ~5000ms
// figure (Finding 52/92), matching sub_820B4EE8's own fix, so genuine bursty real
// progress on this object gets the same chance to land instead of reliably missing
// it after 50ms.
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

    constexpr auto kMaxWait = std::chrono::milliseconds(5000);
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
