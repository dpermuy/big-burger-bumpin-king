#include "gpu_trace.h"
#include "ppc_config.h"
#include <ppc_context.h>

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
    if (!startTimeSet_)
    {
        startTime_ = std::chrono::steady_clock::now();
        startTimeSet_ = true;
    }
}

void GpuCommandTracer::RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ringBufferBase_ = physAddr;
    // Finding 62: corrected against real Xenia source (CommandProcessor::
    // InitializeRingBuffer, command_processor.cc) -- primary_buffer_size_ =
    // 1 << (size_log2 + 3), not 8 << (size_log2 + 3). The previous formula was 8x
    // too large (1MB instead of the real 128KB with the observed sizeLog2Raw=14).
    // Initially suspected (and briefly implemented, then reverted -- Finding 62)
    // this explained the long-observed ~131000-byte plateau via ring wraparound;
    // live testing disproved that specifically (the "wrapped" region still held
    // stale, ancient content, and re-scanning it produced garbage) -- the real
    // cause is a separate deadlock (Finding 63/64). This formula correction is
    // kept regardless since it's independently verified correct against real
    // hardware semantics, wraparound relevance aside.
    ringBufferSize_ = 1u << (sizeLog2Raw + 3);
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    rptrWriteBackAddr_ = addr;
}

void GpuCommandTracer::SetIdentifierAddr(uint32_t addr)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    identifierAddr_ = addr;
}

void GpuCommandTracer::SetGraphicsInterruptCallback(uint32_t callback, uint32_t context)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    graphicsInterruptCallback_ = callback;
    graphicsInterruptContext_ = context;
}

bool GpuCommandTracer::HasRingBuffer()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ringBufferBase_ != 0;
}

uint32_t GpuCommandTracer::GraphicsInterruptCallback()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return graphicsInterruptCallback_;
}

uint32_t GpuCommandTracer::GraphicsInterruptContext()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return graphicsInterruptContext_;
}

void GpuCommandTracer::ObserveWritePointer(uint32_t dwordIndex)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (wptrObserved_ && dwordIndex == lastWptrDwords_)
    {
        return; // no change since last poll -- don't spam the log every 1ms tick
    }

    EnsureLogOpen();
    if (logFile_)
    {
        // Logged unconditionally on every real change, independent of ScanAndTraceFrame's
        // own frame cadence -- this is the ground truth to diff against
        // lastParsedOffset_/newOffset in the frame log around it. If CP_RB_WPTR keeps
        // climbing past the point the zero-byte-padding heuristic stops finding new
        // content, that's direct evidence the heuristic is the bug, not real production
        // (Finding 65's open question). If it freezes at the same point the heuristic
        // does, that's direct evidence production really does stop upstream, exactly as
        // Finding 65 concluded.
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
        fprintf(logFile_, "[CP_RB_WPTR] t=%lldms observed %u -> %u dwords (%u -> %u bytes); heuristic offset currently %u bytes\n",
            (long long)elapsedMs, lastWptrDwords_, dwordIndex, lastWptrDwords_ * 4, dwordIndex * 4, lastParsedOffset_);
        fflush(logFile_);
    }

    lastWptrDwords_ = dwordIndex;
    wptrObserved_ = true;
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

    // Real Type3Opcode values (src/xenia/gpu/xenos.h), cross-referenced live against
    // this project's actual trace output (Finding 36, phase3 spec) -- every opcode ever
    // observed in a real run now has a confirmed real identity, not a guess.
    constexpr uint32_t kOpcodeInterrupt = 0x54;      // PM4_INTERRUPT
    constexpr uint32_t kOpcodeEventWriteShd = 0x58;  // PM4_EVENT_WRITE_SHD
}

uint32_t GpuCommandTracer::ScanBuffer(PPCContext& ctx, uint8_t* base, uint32_t bufferAddr, uint32_t startOffsetBytes, uint32_t sizeBytes, int depth)
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
                ScanBuffer(ctx, base, targetAddr, 0, targetDwordCount * 4, depth + 1);
            }

            if (opcode == kOpcodeEventWriteShd && count == 3)
            {
                // Real semantics (Xenia's ExecutePacketType3_EVENT_WRITE_SHD,
                // command_processor.cc): payload is {initiator, address, value}.
                // Bit 31 of initiator selects "write the GPU's own vblank counter"
                // instead of the literal value dword. address's low 2 bits select an
                // endianness/swap mode on real hardware -- not yet distinguished here
                // (every address observed live so far has them clear); masked off
                // before use either way. address is a physical address, same
                // 0xA0000000 segment-offset translation as INDIRECT_BUFFER (Finding 35).
                uint32_t initiator = LoadU32(base, bufferAddr + offsetBytes + 4);
                uint32_t address = (LoadU32(base, bufferAddr + offsetBytes + 8) & ~0x3u) | 0xA0000000u;
                uint32_t value = LoadU32(base, bufferAddr + offsetBytes + 12);
                uint32_t dataValue = (initiator & 0x80000000u) ? vblankCounter_ : value;
                StoreU32(base, address, dataValue);
                if (logFile_)
                {
                    fprintf(logFile_, "%s-> EVENT_WRITE_SHD: wrote 0x%08X to 0x%08X (initiator=0x%08X)\n",
                        indent, dataValue, address, initiator);
                    fflush(logFile_);
                }
            }

            if (opcode == kOpcodeInterrupt && count == 1)
            {
                // Real semantics (Xenia's ExecutePacketType3_INTERRUPT): payload is a
                // single cpu_mask dword; real hardware dispatches one interrupt per set
                // bit (0-5), each targeting a specific hardware thread
                // (DispatchInterruptCallback(1, n)). This project has no per-core
                // routing (PPC_CALL_INDIRECT_FUNC always runs on the calling thread's
                // own ctx) -- fire once, on the GPU pump thread's own ctx (Finding 57;
                // previously ran on whatever thread called VdSwap, before scanning moved
                // off that synchronous path), if the mask is non-empty and a callback is
                // registered. source=1, matching Finding 36's confirmed real convention
                // (source=0 is the separate vblank path, still dispatched once per VdSwap
                // in kernel_impl.cpp, on the main thread).
                uint32_t cpuMask = LoadU32(base, bufferAddr + offsetBytes + 4);
                if (cpuMask != 0 && graphicsInterruptCallback_ != 0)
                {
                    if (logFile_)
                    {
                        fprintf(logFile_, "%s-> INTERRUPT: dispatching source=1 cpuMask=0x%X\n", indent, cpuMask);
                        fflush(logFile_);
                    }
                    ctx.r3.u64 = 1; // source
                    ctx.r4.u64 = graphicsInterruptContext_;
                    PPC_CALL_INDIRECT_FUNC(graphicsInterruptCallback_);
                }
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

void GpuCommandTracer::RegisterSystemCommandBuffer(uint32_t addr, uint32_t size)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (systemCmdBufAddr_ != 0)
    {
        return; // one real allocation for the whole run, same as VdGetSystemCommandBuffer's own once-only bump alloc
    }
    systemCmdBufAddr_ = addr;
    systemCmdBufSize_ = size;
    systemCmdBufLastOffset_ = 0;

    EnsureLogOpen();
    if (logFile_)
    {
        fprintf(logFile_, "[init] system command buffer base=0x%08X size=%u bytes\n", addr, size);
        fflush(logFile_);
    }
}

// Finding 57: called from the dedicated GPU pump thread (host/main.cpp), on a
// ~1ms cadence, independent of VdSwap. Previously this only ran synchronously
// inside VdSwap on the main CPU thread, which meant the fence it advances here
// could never move while that same thread was stuck in a ring-space wait loop
// (sub_820B4EE8) -- a real, confirmed-live deadlock (Finding 56). Running it
// off a separate thread with its own PPCContext matches real hardware, where
// the GPU retires ring backlog continuously and independently of CPU state.
void GpuCommandTracer::ScanAndTraceFrame(PPCContext& ctx, uint8_t* base)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    EnsureLogOpen();
    frameCounter_++;
    vblankCounter_++;

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u (starting offset %u) ---\n", frameCounter_, lastParsedOffset_);
    }

    // Finding 66: prefer the real CP_RB_WPTR doorbell (polled by the caller into
    // ObserveWritePointer) over the full ring size as the scan's upper bound, when it's
    // available and makes sense as one. This doesn't change what a well-formed run looks
    // like (the zero-byte heuristic still stops exactly where WPTR says content ends,
    // since that's where real content actually ends) -- what it changes is a run where the
    // two disagree, which is now visible in gpu_trace.log instead of silently trusting
    // whichever one runs first.
    //
    // Finding 68: a wptrBytes < lastParsedOffset_ disagreement is real ring wraparound,
    // confirmed live -- a 60-minute run reached lastParsedOffset_ = 131068 (the exact
    // historical plateau from Findings 43/44 onward) while CP_RB_WPTR reported 53732,
    // i.e. the real write pointer had already wrapped back near the start while this
    // project's scanner, which only ever treated the ring as a flat buffer capped at
    // ringBufferSize_, sat stuck 4 bytes from the physical end with nowhere left to go.
    // Finding 62 looked for wraparound once already and found only stale garbage at
    // offset 0 -- that was before any real wrap had happened yet (a premature check, not
    // a disproof); this time the drop in a live-polled hardware register is the
    // confirmation Finding 62 didn't have.
    uint32_t scanBound = ringBufferSize_;
    bool wrapPending = false;
    uint32_t wrapTargetBytes = 0;
    if (wptrObserved_)
    {
        uint64_t wptrBytes = static_cast<uint64_t>(lastWptrDwords_) * 4;
        if (wptrBytes >= lastParsedOffset_ && wptrBytes <= ringBufferSize_)
        {
            scanBound = static_cast<uint32_t>(wptrBytes);
        }
        else if (wptrBytes < lastParsedOffset_)
        {
            wrapPending = true;
            wrapTargetBytes = static_cast<uint32_t>(wptrBytes);
            if (logFile_)
            {
                fprintf(logFile_, "(CP_RB_WPTR=%u bytes is behind already-parsed offset=%u bytes -- real "
                    "wraparound, will finish this frame's tail then resume scanning from offset 0)\n",
                    wrapTargetBytes, lastParsedOffset_);
            }
        }
    }

    uint32_t startOffsetThisFrame = lastParsedOffset_;
    uint32_t newOffset = ScanBuffer(ctx, base, ringBufferBase_, lastParsedOffset_, scanBound, 0);

    // Bug (live-caught, same session): originally required newOffset >= scanBound to call
    // the tail "exhausted", but the zero-byte heuristic stops at the first unwritten dword
    // -- which is always at or before scanBound, never past it -- so that never actually
    // fires once scanBound is the ring's physical end (131072) and the real trailing
    // padding starts 4 bytes earlier (131068). Confirmed live: this hung in an identical
    // "wraparound pending" loop every single frame, forever, never following the wrap.
    // The real, correct exhaustion signal is simpler: no new packets were parsed out of
    // the tail this frame (newOffset == startOffsetThisFrame). Real content still pending
    // in the tail makes real progress and this naturally won't fire until it's genuinely
    // used up, same as intended.
    if (wrapPending && newOffset == startOffsetThisFrame)
    {
        // The tail up to the physical end of the ring is exhausted (the normal case --
        // real hardware doesn't leave a dangling unconsumed tail across a wrap either).
        // Follow the real write pointer back around to the start, same as any circular
        // buffer consumer would.
        if (logFile_)
        {
            fprintf(logFile_, "--- wrapped: resuming scan at offset 0 (target %u bytes) ---\n", wrapTargetBytes);
        }
        newOffset = ScanBuffer(ctx, base, ringBufferBase_, 0, wrapTargetBytes, 0);
    }

    if (logFile_)
    {
        fprintf(logFile_, "--- frame %u done ---\n", frameCounter_);
        fflush(logFile_);
    }

    lastParsedOffset_ = newOffset;

    // Real second buffer (Finding 38) -- scanned the same way as the main ring, right
    // after it, once per pump iteration (Finding 57). Resumes from where the last scan
    // left off, same incremental pattern as the main ring's lastParsedOffset_.
    if (systemCmdBufAddr_ != 0)
    {
        if (logFile_)
        {
            fprintf(logFile_, "--- system command buffer (starting offset %u) ---\n", systemCmdBufLastOffset_);
        }
        uint32_t systemNewOffset = ScanBuffer(ctx, base, systemCmdBufAddr_, systemCmdBufLastOffset_, systemCmdBufSize_, 0);
        if (logFile_)
        {
            fprintf(logFile_, "--- system command buffer done ---\n");
            fflush(logFile_);
        }
        systemCmdBufLastOffset_ = systemNewOffset;
    }

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
