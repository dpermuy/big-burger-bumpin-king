#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>
#include <file.h>
#include <image.h>
#include "gpu_trace.h"
#include "xdvdfs.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <sys/mman.h>
#include <thread>

PPC_EXTERN_FUNC(_xstart);

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

    // GPU MMIO register block at 0x7FC80000. Real hardware (and Xenia) trap these
    // reads; this harness backs them with plain memory, so registers the game polls
    // must be pre-seeded with the values Xenia's GraphicsSystem::ReadRegister returns
    // (ground truth, graphics_system.cc). The critical one is interrupt status
    // (dword index 0x1951, byte offset 0x6544): the game's own graphics interrupt
    // callback (sub_820B5160, vblank branch) reads it and returns without doing ANY
    // vblank work -- including the swap-completion path that unblocks the whole
    // render pipeline -- unless bit 0 is set. Xenia returns constant 1 here.
    constexpr uint32_t kGpuRegisterBase = 0x7FC80000;
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
