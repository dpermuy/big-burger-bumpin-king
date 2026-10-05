#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>
#include <file.h>
#include <image.h>
#include "gpu_trace.h"
#include "xdvdfs.h"
#include "renderer_metal.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <vector>

PPC_EXTERN_FUNC(_xstart);

// GPU MMIO register block base (Xenia/rexglue-confirmed convention: register index r,
// as used in guest code, maps to byte address kGpuRegisterBase + r*4). File-scope so both
// SetupMemoryImage (seeding read-only registers) and the pump thread (polling CP_RB_WPTR,
// Finding 66) can use it.
constexpr uint32_t kGpuRegisterBase = 0x7FC80000;
// CP_RB_WPTR (Finding 66): confirmed against real reference (rexglue-skate3/skate3recomp's
// graphics_system.cpp, itself Xenia's real GPU command processor) -- register index 0x1C5.
// Real hardware/Xenia traps writes to this MMIO register as the doorbell that tells the GPU
// how far the CPU has produced real ring-buffer content (CommandProcessor::
// UpdateWritePointer). This project's GPU register block is untrapped plain memory (see
// below) -- the CPU's writes land there same as any store, so this project can't intercept
// the write, but CAN poll the value: it's the same authoritative content boundary the real
// hardware would have used, instead of this project's own zero-byte-padding heuristic
// (GpuCommandTracer::ScanBuffer). See gpu_trace.cpp's ObserveWritePointer.
constexpr uint32_t kCpRbWptrRegAddr = kGpuRegisterBase + 0x1C5 * 4;

static uint8_t* SetupMemoryImage(const char* xexPath)
{
    void* mem = mmap(nullptr, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        fprintf(stderr, "Failed to mmap %llu bytes for guest memory image: %s\n", (unsigned long long)PPC_MEMORY_SIZE, strerror(errno));
        std::exit(1);
    }
    uint8_t* base = static_cast<uint8_t*>(mem);

    const auto file = LoadFile(xexPath);
    if (file.empty())
    {
        fprintf(stderr, "Failed to load XEX file: %s\n", xexPath);
        std::exit(1);
    }

    auto image = Image::ParseImage(file.data(), file.size());
    for (const auto& section : image.sections)
    {
        // XenonUtils maps each section's size straight from the XEX's PE-style
        // section header (VirtualSize), not clamped against the actual allocated
        // image buffer (image.size, fixed at the XEX security header's imageSize).
        // .reloc's declared VirtualSize legitimately extends past that allocation
        // -- it's PE relocation-table metadata, never touched by the recompiled
        // code, and was never actually decompressed into image.data. Copying it
        // verbatim reads out of bounds. Skip any section whose declared range
        // exceeds the real allocation rather than assuming every reported section
        // is safe to copy.
        size_t offsetIntoAllocation = section.data - image.data.get();
        if (offsetIntoAllocation + section.size > image.size)
        {
            fmt::println("Skipping section '{}': declared range exceeds allocated image size "
                "(loader metadata, not part of the runtime image)", section.name);
            continue;
        }
        std::memcpy(base + section.base, section.data, section.size);
    }

    size_t mappingCount = 0;
    for (auto* mapping = PPCFuncMappings; mapping->guest != 0; ++mapping)
    {
        PPC_LOOKUP_FUNC(base, mapping->guest) = mapping->host;
        mappingCount++;
    }

    fmt::println("Guest memory image ready: base={}, {} sections loaded, {} function mappings installed",
        static_cast<void*>(base), image.sections.size(), mappingCount);

    // Real Xbox 360 kernel/HAL boot code (outside any title's own executable) populates
    // this slot with a pointer to a kernel-shared tick structure before the title's
    // entry point runs. This project never emulates that boot sequence, and the raw
    // XEX's .data section genuinely contains zero here (confirmed, Phase 2S) -- so the
    // host writes it directly, pointing at a small host-owned structure (Phase 2T).
    constexpr uint32_t kKernelTickStructAddr = 0x7FFF0000;
    constexpr uint32_t kKernelTickPointerSlot = 0x82670100; // r13(0x82670000) + 256
    PPC_STORE_U32(kKernelTickPointerSlot, kKernelTickStructAddr);

    // field+76 of the loading-progress object at guest 0x826EB104's canonical
    // pointer slot (Phase 3 investigation, Findings 106-118) is unconditionally
    // gated: sub_82451408 skips the whole loading-complete path whenever it's
    // <= 0, and sub_82451630 (the one-time boot-time initializer) sets it
    // straight from this global via a config-lookup fallback chain. The raw
    // XEX's .data section genuinely contains zero here too (confirmed live via
    // XenonUtils' Image::Find, bypassing this project's own loader entirely --
    // Finding 118), so like the kernel tick struct above, real console
    // kernel/profile/settings machinery this project doesn't emulate must be
    // what's supposed to populate it. 1 is a placeholder, not a researched
    // correct value -- it's the minimum that satisfies the observed `<= 0`
    // gate (and matches the sibling field+72, which is naturally 1 in this
    // image), chosen to unblock forward progress; live-confirmed (Finding 118)
    // to be necessary and sufficient to reach genuinely new code (real
    // package-loading activity never observed before in this investigation)
    // rather than just moving the hang. Revisit if real difficulty/profile
    // settings loading is ever implemented.
    constexpr uint32_t kLoadingProgressConfigSlot = 0x8270F784;
    PPC_STORE_U32(kLoadingProgressConfigSlot, 1);

    // GPU MMIO register block at 0x7FC80000. Real hardware (and Xenia) trap these
    // reads; this harness backs them with plain memory, so registers the game polls
    // must be pre-seeded with the values Xenia's GraphicsSystem::ReadRegister returns
    // (ground truth, graphics_system.cc). The critical one is interrupt status
    // (dword index 0x1951, byte offset 0x6544): the game's own graphics interrupt
    // callback (sub_820B5160, vblank branch) reads it and returns without doing ANY
    // vblank work -- including the swap-completion path that unblocks the whole
    // render pipeline -- unless bit 0 is set. Xenia returns constant 1 here.
    PPC_STORE_U32(kGpuRegisterBase + 0x1951 * 4, 1);          // interrupt status: vblank
    PPC_STORE_U32(kGpuRegisterBase + 0x194C * 4, 0x000002D0); // R500_D1MODE_V_COUNTER
    PPC_STORE_U32(kGpuRegisterBase + 0x1961 * 4, 0x050002D0); // AVIVO_D1MODE_VIEWPORT_SIZE (1280x720)
    PPC_STORE_U32(kGpuRegisterBase + 0x0F00 * 4, 0x08100748); // RB_EDRAM_TIMING
    PPC_STORE_U32(kGpuRegisterBase + 0x0F01 * 4, 0x0000200E); // RB_BC_CONTROL

    return base;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    // --window can appear anywhere on the command line; remaining
    // arguments stay positional (xexPath, isoPath) in their original
    // relative order.
    bool windowMode = false;
    std::vector<const char*> positionalArgs;
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--window")
        {
            windowMode = true;
        }
        else
        {
            positionalArgs.push_back(argv[i]);
        }
    }

    const char* xexPath = positionalArgs.size() > 0 ? positionalArgs[0] : "private/default.xex";
    const char* isoPath = positionalArgs.size() > 1 ? positionalArgs[1] : "Big Bumpin' (USA).iso";
    uint8_t* base = SetupMemoryImage(xexPath);

    if (windowMode)
    {
        // Fixed at 1280x720 for this milestone, matching the real
        // AVIVO_D1MODE_VIEWPORT_SIZE value already seeded in
        // SetupMemoryImage above -- keep these two in sync by eye; not
        // worth a shared constant for one hardcoded milestone value.
        constexpr int kWindowWidth = 1280;
        constexpr int kWindowHeight = 720;
        if (!Renderer_Init(kWindowWidth, kWindowHeight))
        {
            fmt::println("Failed to initialize Metal renderer (no GPU device available?) -- exiting.");
            std::_Exit(1);
        }
    }

    if (!g_xdvdfsImage.Open(isoPath))
    {
        fmt::println("Warning: failed to open ISO '{}' -- disc file access will report not-found for everything.", isoPath);
    }
    else
    {
        fmt::println("Opened disc image: {}", isoPath);
    }

    // Advances the tick field the guest reads via the pointer SetupMemoryImage wrote
    // at 0x82670100. Detached, matching ExCreateThread's precedent (host/kernel_impl.cpp)
    // -- never joined, runs harmlessly for the process's lifetime.
    std::thread tickThread([base]()
    {
        constexpr uint32_t kTickFieldAddr = 0x7FFF0000 + 88;
        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            PPC_STORE_U32(kTickFieldAddr, (uint32_t)elapsedMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    tickThread.detach();

    // Finding 57: the fake GPU used to only "consume" ring buffer content
    // (GpuCommandTracer::ScanAndTraceFrame, which retires packets and advances the
    // EVENT_WRITE_SHD fence) synchronously inside VdSwap, on the main CPU thread.
    // That meant the fence could never advance while that same thread was stuck in
    // a ring-space wait loop (sub_820B4EE8) blocked waiting on exactly that fence --
    // a real, confirmed-live permanent deadlock (Finding 56). Real hardware doesn't
    // have this problem: the GPU retires backlog continuously and independently of
    // whatever the CPU is doing. This thread does the same here -- polls/scans
    // whenever a ring buffer is registered, on its own ~1ms cadence, detached like
    // tickThread above. It needs its own PPCContext (own stack, same small-data-area
    // base as the main context) because ScanBuffer's PM4_INTERRUPT handling actually
    // invokes real guest code (the registered graphics interrupt callback) -- that
    // can't safely share the main thread's live register state.
    constexpr uint32_t kGpuPumpStackBase = 0x90200000; // well clear of the main
                                                        // thread's stack (below, at
                                                        // 0x90000000-0x90100000)
    constexpr uint32_t kGpuPumpStackSize = 0x100000;
    std::thread gpuPumpThread([base]()
    {
        PPCContext pumpCtx{};
        pumpCtx.r1.u64 = kGpuPumpStackBase + kGpuPumpStackSize - 0x10;
        pumpCtx.r13.u64 = 0x82670000; // same small-data-area base as the main context
        while (true)
        {
            if (g_gpuTracer.HasRingBuffer())
            {
                // Finding 66: poll the real CP_RB_WPTR doorbell register every tick,
                // same cadence as the scan itself. See kCpRbWptrRegAddr's comment above.
                g_gpuTracer.ObserveWritePointer(PPC_LOAD_U32(kCpRbWptrRegAddr));
                g_gpuTracer.ScanAndTraceFrame(pumpCtx, base);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    gpuPumpThread.detach();

    constexpr uint32_t kStackBase = 0x90000000;
    constexpr uint32_t kStackSize = 0x100000;

    PPCContext ctx{};
    ctx.r1.u64 = kStackBase + kStackSize - 0x10;
    ctx.r13.u64 = 0x82670000; // small-data-area base (confirmed via cross-reference, Phase 2R)

    fmt::println("Calling _xstart...");

    auto future = std::async(std::launch::async, [&]() {
        _xstart(ctx, base);
    });

    if (!windowMode)
    {
        // Existing behavior, byte-for-byte unchanged.
        auto status = future.wait_for(std::chrono::seconds(10));
        if (status == std::future_status::timeout)
        {
            fmt::println("_xstart did not return within 10 seconds (watchdog timeout) -- "
                "this is an expected, informative outcome for Phase 2A, not a crash.");
            std::_Exit(2);
        }

        fmt::println("_xstart returned normally.");
        return 0;
    }

    // --window mode: the main thread must run the real AppKit event loop
    // instead of blocking directly on future.wait_for (windows only
    // function on the main thread, a hard Cocoa requirement). A separate
    // watchdog thread mirrors the exact same wait_for logic and timeout
    // message as headless mode, then signals the AppKit loop to stop.
    std::atomic<int> watchdogResult{-1}; // set to 0 (returned normally) or 2 (timeout) below
    std::thread watchdogThread([&]() {
        auto status = future.wait_for(std::chrono::seconds(10));
        if (status == std::future_status::timeout)
        {
            fmt::println("_xstart did not return within 10 seconds (watchdog timeout) -- "
                "this is an expected, informative outcome for Phase 2A, not a crash.");
            watchdogResult.store(2);
        }
        else
        {
            fmt::println("_xstart returned normally.");
            watchdogResult.store(0);
        }
        Renderer_RequestShutdown();
    });

    Renderer_RunEventLoop(); // blocks main thread until shutdown is requested

    if (Renderer_WasClosedByUser())
    {
        // Real, expected behavior: a reasonable person who closes the
        // window expects the program to end right away, not keep running
        // invisibly for up to the PPC thread's own remaining watchdog
        // bound (which could still be most of 10s, or most of an
        // extended validation run's much longer bound). std::_Exit
        // terminates the whole process unconditionally -- the still-
        // running watchdog thread and PPC execution thread do not need
        // to be joined or cleaned up first, matching every other exit
        // path in this program (both the no-GPU-device and watchdog-
        // timeout paths also use std::_Exit rather than a clean join).
        std::_Exit(3); // distinct from 1 (no GPU device) and 2 (watchdog timeout)
    }

    watchdogThread.join();

    if (watchdogResult.load() == 2)
    {
        std::_Exit(2);
    }
    return 0;
}
