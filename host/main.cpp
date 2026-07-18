#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>
#include <file.h>
#include <image.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <sys/mman.h>

PPC_EXTERN_FUNC(_xstart);

static uint8_t* SetupMemoryImage(const char* xexPath)
{
    void* mem = mmap(nullptr, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
    {
        fmt::println("Failed to mmap {} bytes for guest memory image", PPC_MEMORY_SIZE);
        std::exit(1);
    }
    uint8_t* base = static_cast<uint8_t*>(mem);

    const auto file = LoadFile(xexPath);
    if (file.empty())
    {
        fmt::println("Failed to load XEX file: {}", xexPath);
        std::exit(1);
    }

    auto image = Image::ParseImage(file.data(), file.size());
    for (const auto& section : image.sections)
    {
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

    return base;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* xexPath = argc > 1 ? argv[1] : "private/default.xex";
    uint8_t* base = SetupMemoryImage(xexPath);

    constexpr uint32_t kStackBase = 0x90000000;
    constexpr uint32_t kStackSize = 0x100000;

    PPCContext ctx{};
    ctx.r1.u64 = kStackBase + kStackSize - 0x10;

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
