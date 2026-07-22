#include "gpu_trace.h"

GpuCommandTracer g_gpuTracer;

namespace
{
    inline uint32_t LoadU32(uint8_t* base, uint32_t addr)
    {
        return __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(base + addr));
    }

    inline void StoreU32(uint8_t* base, uint32_t addr, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t*>(base + addr) = __builtin_bswap32(value);
    }
}

void GpuCommandTracer::EnsureLogOpen()
{
    if (!logFile_)
    {
        logFile_ = fopen("gpu_trace.log", "w");
    }
}

void GpuCommandTracer::RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw)
{
    ringBufferBase_ = physAddr;
    // Inverts the call site's own computation (private/ppc/ppc_recomp.4.cpp:14842-14856):
    // the game derives sizeLog2Raw as (31 - clz(originalSizeBytes)) - 3, so
    // originalSizeBytes is approximately 8 << (sizeLog2Raw + 3). Not independently
    // verified against real hardware docs -- a starting point, correctable once trace
    // output shows packets running past this boundary.
    ringBufferSize_ = 8u << (sizeLog2Raw + 3);
    lastParsedOffset_ = 0;
    frameCounter_ = 0;

    EnsureLogOpen();
    if (logFile_)
    {
        fprintf(logFile_, "[init] ring buffer base=0x%08X sizeLog2Raw=%u decodedSize=%u bytes\n",
            physAddr, sizeLog2Raw, ringBufferSize_);
        fflush(logFile_);
    }
}

void GpuCommandTracer::SetRptrWriteBackAddr(uint32_t addr)
{
    rptrWriteBackAddr_ = addr;
}

void GpuCommandTracer::SetIdentifierAddr(uint32_t addr)
{
    identifierAddr_ = addr;
}

void GpuCommandTracer::SetGraphicsInterruptCallback(uint32_t callback, uint32_t context)
{
    graphicsInterruptCallback_ = callback;
    graphicsInterruptContext_ = context;
}

namespace
{
    // Real IT_OPCODE values for the ATI/AMD R500-family PM4 command format Xenos
    // belongs to. Confirmed against this game's own real call sites and live payload
    // shape (Phase 3K/3L), not assumed from generic docs alone:
    //   0x48 ME_INIT           -- one-time, first-ever packet, fixed-size config
    //                              payload copied from static XEX data (confirmed:
    //                              private/ppc/ppc_recomp.4.cpp:14982-15013).
    //   0x3F INDIRECT_BUFFER   -- always exactly 2 payload dwords in every real
    //                              instance observed; textbook IB shape is
    //                              {address, dwordCount}, and the addresses seen live
    //                              in the same 0x18Dxxxxx family as other real
    //                              GPU-adjacent context addresses (Phase 3I spinlock
    //                              addresses), not random data.
    constexpr uint32_t kOpcodeMeInit = 0x48;
    constexpr uint32_t kOpcodeIndirectBuffer = 0x3F;
    constexpr int kMaxIndirectDepth = 3;
}

uint32_t GpuCommandTracer::ScanBuffer(uint8_t* base, uint32_t bufferAddr, uint32_t startOffsetBytes, uint32_t sizeBytes, int depth)
{
    const char* indent = depth == 0 ? "" : (depth == 1 ? "  " : (depth == 2 ? "    " : "      "));
    uint32_t offsetBytes = startOffsetBytes;
    uint32_t packetsParsed = 0;

    while (offsetBytes + 4 <= sizeBytes)
    {
        uint32_t header = LoadU32(base, bufferAddr + offsetBytes);
        if (header == 0)
        {
            // Confirmed live: a genuine all-zero dword decodes as a "valid" TYPE0
            // reg=0 count=1 packet under the rules below, but real unwritten buffer
            // memory is zero-filled and the game never actually emits that as a real
            // packet (real no-ops use TYPE2, observed elsewhere in the same trace).
            // Treat a zero header as the end of real data, not a packet.
            if (logFile_) fprintf(logFile_, "%s(zero padding at offset %u, stopping)\n", indent, offsetBytes);
            break;
        }

        uint32_t type = (header >> 30) & 0x3;

        if (type == 0x2)
        {
            // Type 2: single-dword filler/no-op, no payload.
            if (logFile_) fprintf(logFile_, "%sTYPE2 (filler)\n", indent);
            offsetBytes += 4;
            packetsParsed++;
            continue;
        }

        if (type == 0x0)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t baseIndex = header & 0x7FFF;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > sizeBytes)
            {
                break; // count runs past the buffer -- not a real packet, stop here
            }
            if (logFile_) fprintf(logFile_, "%sTYPE0 reg=0x%04X count=%u\n", indent, baseIndex, count);
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }

        if (type == 0x3)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t opcode = (header >> 8) & 0x7F;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > sizeBytes)
            {
                break;
            }

            const char* name = (opcode == kOpcodeMeInit) ? " (ME_INIT)"
                : (opcode == kOpcodeIndirectBuffer) ? " (INDIRECT_BUFFER)" : "";
            if (logFile_) fprintf(logFile_, "%sTYPE3 opcode=0x%02X count=%u%s\n", indent, opcode, count, name);

            if (opcode == kOpcodeIndirectBuffer && count == 2 && depth < kMaxIndirectDepth)
            {
                // The game computes this target address as a true physical address (its own
                // inline "& 0x1FFFFFFF"-style conversion, confirmed present in the sibling
                // VdEnableRingBufferRPtrWriteBack address-mangling call site at
                // private/ppc/ppc_recomp.4.cpp:14873-14889), not a guest virtual address --
                // real Xenos hardware GPU commands reference physical RAM directly. This
                // project's guest memory model needs the same 0xA0000000 uncached
                // direct-map segment offset its own allocators already use (confirmed:
                // physical offset 0 == guest virtual 0xA0000000, NtAllocateVirtualMemory's
                // very first allocation) to resolve it to the right location in `base`.
                // Empirically confirmed live (Finding 35): reading raw or with +0x80000000
                // always reads zero; +0xA0000000 reads real, well-formed PM4 packet headers
                // at every single observed target.
                uint32_t targetAddr = LoadU32(base, bufferAddr + offsetBytes + 4) | 0xA0000000u;
                uint32_t targetDwordCount = LoadU32(base, bufferAddr + offsetBytes + 8);
                if (logFile_)
                {
                    fprintf(logFile_, "%s-> following indirect buffer at 0x%08X (%u dwords)\n",
                        indent, targetAddr, targetDwordCount);
                }
                ScanBuffer(base, targetAddr, 0, targetDwordCount * 4, depth + 1);
            }

            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }

        // Type 1 (legacy two-register write, not expected on Xenos) or anything else
        // that didn't decode above: stop and dump raw bytes for manual inspection
        // rather than guessing further, matching the project's established posture
        // toward unparsed data (see xdvdfs.cpp's BST padding handling).
        break;
    }

    if (offsetBytes < sizeBytes && offsetBytes == startOffsetBytes && packetsParsed == 0)
    {
        // Nothing new parsed at all -- dump a small raw window so the header format
        // can be checked by hand instead of silently producing an empty trace.
        if (logFile_)
        {
            fprintf(logFile_, "%sRAW (unparsed) at offset %u:", indent, offsetBytes);
            uint32_t dumpBytes = (sizeBytes - offsetBytes < 64) ? (sizeBytes - offsetBytes) : 64;
            for (uint32_t i = 0; i < dumpBytes; i += 4)
            {
                fprintf(logFile_, " %08X", LoadU32(base, bufferAddr + offsetBytes + i));
            }
            fprintf(logFile_, "\n");
        }
    }

    if (logFile_)
    {
        fprintf(logFile_, "%s(parsed %u packets, offset %u -> %u)\n", indent, packetsParsed, startOffsetBytes, offsetBytes);
        fflush(logFile_);
    }

    return offsetBytes;
}

void GpuCommandTracer::ScanAndTraceFrame(uint8_t* base)
{
    EnsureLogOpen();
    frameCounter_++;

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u (starting offset %u) ---\n", frameCounter_, lastParsedOffset_);
    }

    uint32_t newOffset = ScanBuffer(base, ringBufferBase_, lastParsedOffset_, ringBufferSize_, 0);

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u done ---\n", frameCounter_);
        fflush(logFile_);
    }

    lastParsedOffset_ = newOffset;

    // Real semantics: the GPU writes its "consumed up to here" read pointer so the CPU
    // knows how much ring space is free. There's no real GPU, so report "caught up to
    // everything we just parsed" every frame -- this is what keeps the game from ever
    // blocking waiting for a completion signal that would otherwise never come (the
    // exact shape of bug Phase 3H fixed for APC delivery). Units are dwords, matching
    // real CP_RB_RPTR/CP_RB_WPTR register convention -- not independently verified,
    // flagged here as a starting point.
    if (rptrWriteBackAddr_ != 0)
    {
        StoreU32(base, rptrWriteBackAddr_, newOffset / 4);
    }
}
