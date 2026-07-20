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

void GpuCommandTracer::ScanAndTraceFrame(uint8_t* base)
{
    EnsureLogOpen();
    frameCounter_++;

    uint32_t offsetBytes = lastParsedOffset_;
    uint32_t packetsParsed = 0;

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u (starting offset %u) ---\n", frameCounter_, offsetBytes);
    }

    while (offsetBytes + 4 <= ringBufferSize_)
    {
        uint32_t header = LoadU32(base, ringBufferBase_ + offsetBytes);
        uint32_t type = (header >> 30) & 0x3;

        if (type == 0x2)
        {
            // Type 2: single-dword filler/no-op, no payload.
            if (logFile_) fprintf(logFile_, "TYPE2 (filler)\n");
            offsetBytes += 4;
            packetsParsed++;
            continue;
        }

        if (type == 0x0)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t baseIndex = header & 0x7FFF;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > ringBufferSize_)
            {
                break; // count runs past the buffer -- not a real packet, stop here
            }
            if (logFile_) fprintf(logFile_, "TYPE0 reg=0x%04X count=%u\n", baseIndex, count);
            offsetBytes += 4 + payloadBytes;
            packetsParsed++;
            continue;
        }

        if (type == 0x3)
        {
            uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            uint32_t opcode = (header >> 8) & 0x7F;
            uint32_t payloadBytes = count * 4;
            if (offsetBytes + 4 + payloadBytes > ringBufferSize_)
            {
                break;
            }
            if (logFile_) fprintf(logFile_, "TYPE3 opcode=0x%02X count=%u\n", opcode, count);
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

    if (offsetBytes < ringBufferSize_ && offsetBytes == lastParsedOffset_ && packetsParsed == 0)
    {
        // Nothing new parsed at all -- dump a small raw window so the header format
        // can be checked by hand instead of silently producing an empty trace.
        if (logFile_)
        {
            fprintf(logFile_, "RAW (unparsed) at offset %u:", offsetBytes);
            uint32_t dumpBytes = (ringBufferSize_ - offsetBytes < 64) ? (ringBufferSize_ - offsetBytes) : 64;
            for (uint32_t i = 0; i < dumpBytes; i += 4)
            {
                fprintf(logFile_, " %08X", LoadU32(base, ringBufferBase_ + offsetBytes + i));
            }
            fprintf(logFile_, "\n");
        }
    }

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u: parsed %u packets, offset %u -> %u ---\n",
            frameCounter_, packetsParsed, lastParsedOffset_, offsetBytes);
        fflush(logFile_);
    }

    lastParsedOffset_ = offsetBytes;

    // Real semantics: the GPU writes its "consumed up to here" read pointer so the CPU
    // knows how much ring space is free. There's no real GPU, so report "caught up to
    // everything we just parsed" every frame -- this is what keeps the game from ever
    // blocking waiting for a completion signal that would otherwise never come (the
    // exact shape of bug Phase 3H fixed for APC delivery). Units are dwords, matching
    // real CP_RB_RPTR/CP_RB_WPTR register convention -- not independently verified,
    // flagged here as a starting point.
    if (rptrWriteBackAddr_ != 0)
    {
        StoreU32(base, rptrWriteBackAddr_, offsetBytes / 4);
    }
}
