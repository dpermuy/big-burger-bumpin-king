#include "gpu_trace.h"
#include "ppc_config.h"
#include "shader_decode.h"
#include <ppc_context.h>
#include <string>
#include <vector>

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
    constexpr uint32_t kOpcodeSetConstant = 0x2D;    // PM4_SET_CONSTANT
    constexpr uint32_t kOpcodeDrawIndx = 0x22;       // PM4_DRAW_INDX
    constexpr uint32_t kOpcodeDrawIndx2 = 0x36;      // PM4_DRAW_INDX_2
    constexpr uint32_t kOpcodeImLoadImmediate = 0x2B;  // PM4_IM_LOAD_IMMEDIATE
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
            // Real semantics (Xenia's ExecutePacketType0): header bit 15
            // ("write one register") means every payload dword targets
            // baseIndex itself, not baseIndex+i -- not observed live in
            // this project yet, but a header that does set it would
            // otherwise corrupt neighboring registers. Final review
            // finding M4.
            bool writeOneReg = (header & 0x8000) != 0;
            for (uint32_t i = 0; i < count; i++)
            {
                uint32_t value = LoadU32(base, bufferAddr + offsetBytes + 4 + i * 4);
                uint32_t targetIndex = writeOneReg ? baseIndex : (baseIndex + i);
                gpuState_.WriteRegister(targetIndex, value);
            }
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
                : (opcode == kOpcodeIndirectBuffer) ? " (INDIRECT_BUFFER)"
                : (opcode == kOpcodeSetConstant) ? " (SET_CONSTANT)"
                : (opcode == kOpcodeImLoadImmediate) ? " (IM_LOAD_IMMEDIATE)" : "";
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

            if (opcode == kOpcodeSetConstant && count >= 1)
            {
                // Real semantics (Xenia's ExecutePacketType3_SET_CONSTANT):
                // first payload dword's low 11 bits are the sub-bank index,
                // bits 16-23 select which real register sub-bank it's
                // relative to. An undefined type (anything but the 5 real
                // cases) is skipped entirely rather than guessed at --
                // matches this project's established posture toward
                // unparsed data.
                uint32_t offsetType = LoadU32(base, bufferAddr + offsetBytes + 4);
                uint32_t subIndex = offsetType & 0x7FF;
                uint32_t subType = (offsetType >> 16) & 0xFF;
                uint32_t baseRegister = 0;
                bool validType = true;
                switch (subType)
                {
                    case 0: baseRegister = subIndex + 0x4000; break; // ALU
                    case 1: baseRegister = subIndex + 0x4800; break; // FETCH
                    case 2: baseRegister = subIndex + 0x4900; break; // BOOL
                    case 3: baseRegister = subIndex + 0x4908; break; // LOOP
                    case 4: baseRegister = subIndex + 0x2000; break; // REGISTERS
                    default: validType = false; break;
                }
                if (validType)
                {
                    for (uint32_t i = 0; i < count - 1; i++)
                    {
                        uint32_t value = LoadU32(base, bufferAddr + offsetBytes + 8 + i * 4);
                        gpuState_.WriteRegister(baseRegister + i, value);
                    }
                    if (logFile_)
                    {
                        fprintf(logFile_, "%s-> SET_CONSTANT: subType=%u subIndex=0x%X -> base=0x%X, %u dwords\n",
                            indent, subType, subIndex, baseRegister, count - 1);
                    }
                }
                else if (logFile_)
                {
                    fprintf(logFile_, "%s-> SET_CONSTANT: undefined subType=%u, skipped\n", indent, subType);
                }
            }

            if (opcode == kOpcodeDrawIndx2 && count >= 1)
            {
                // Real semantics (Xenia's ExecutePacketType3Draw): the
                // draw initiator travels inside the draw packet itself,
                // not as a pre-set register -- real hardware mirrors it
                // into the register file as a side effect of executing
                // the draw, which is what this write reproduces. DRAW_INDX_2
                // has no leading viz-query dword, so the initiator is the
                // packet's first payload dword. Final review finding I1
                // (this project's own prior assumption that TYPE0 writes
                // alone would populate this register was wrong -- live
                // data showed 49 real DRAW_INDX_2 packets and zero TYPE0
                // writes to this register).
                uint32_t drawInitiatorValue = LoadU32(base, bufferAddr + offsetBytes + 4);
                gpuState_.WriteRegister(GpuRegisterState::kDrawInitiatorRegister, drawInitiatorValue);

                DrawInitiator di = gpuState_.GetDrawInitiator();

                // Only real Xenos primitive types with a direct Metal
                // equivalent produce a DrawCommand (confirmed real values
                // against Xenia's xenos::PrimitiveType). Notably
                // kTriangleFan (5) has no modern Metal equivalent, and
                // kRectangleList (8) is confirmed real and common in this
                // project's own captured trace but also unsupported here.
                bool havePrimType = true;
                DrawPrimitiveType mappedPrimType = DrawPrimitiveType::Point;
                switch (di.primType)
                {
                    case 1: mappedPrimType = DrawPrimitiveType::Point; break;
                    case 2: mappedPrimType = DrawPrimitiveType::Line; break;
                    case 3: mappedPrimType = DrawPrimitiveType::LineStrip; break;
                    case 4: mappedPrimType = DrawPrimitiveType::Triangle; break;
                    case 6: mappedPrimType = DrawPrimitiveType::TriangleStrip; break;
                    default: havePrimType = false; break;
                }

                if (!havePrimType)
                {
                    if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: unsupported primType=%u, skipped\n", indent, di.primType);
                }
                else if (di.sourceSelect == 1)
                {
                    // kImmediate: unsupported even by Xenia itself.
                    if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: kImmediate source select unsupported, skipped\n", indent);
                }
                else if (di.sourceSelect != 0 && di.sourceSelect != 2)
                {
                    // Final review finding I4 (part 2): sourceSelect only
                    // defines 0 (kDMA), 1 (kImmediate), 2 (kAutoIndex) --
                    // value 3 is undefined/reserved and must not be
                    // silently treated as kAutoIndex.
                    if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: undefined sourceSelect=%u, skipped\n", indent, di.sourceSelect);
                }
                else
                {
                    VertexFetchConstant vfc = gpuState_.GetVertexFetchConstant(0);
                    uint32_t vertexByteAddr = (vfc.address << 2) | 0xA0000000u;
                    uint32_t vertexByteSize = vfc.size * 4;
                    constexpr uint32_t kMaxVertexBufferBytes = 16u * 1024u * 1024u;

                    // Final review finding I4 (part 1): resolve the kDMA
                    // index buffer BEFORE deciding whether to build a
                    // DrawCommand at all -- a kDMA draw whose index buffer
                    // can't be resolved (truncated packet, zero/oversized
                    // NUM_WORDS) must be skipped entirely, not silently
                    // drawn as if it were non-indexed over the whole
                    // vertex buffer.
                    bool isIndexed = (di.sourceSelect == 0);
                    bool indexResolutionFailed = false;
                    std::vector<uint8_t> resolvedIndexData;
                    uint32_t resolvedIndexCount = 0;
                    bool indexIs32Bit = (di.indexSize == 1);

                    if (isIndexed)
                    {
                        if (count < 3)
                        {
                            indexResolutionFailed = true;
                        }
                        else
                        {
                            // kDMA: real index buffer base/size travel in
                            // this same packet's next two payload dwords
                            // (Xenia's ExecutePacketType3Draw). VGT_DMA_BASE
                            // is already a byte address (unlike the fetch
                            // constant's dword-granular address field).
                            uint32_t dmaBase = LoadU32(base, bufferAddr + offsetBytes + 8);
                            uint32_t dmaSizeValue = LoadU32(base, bufferAddr + offsetBytes + 12);
                            uint32_t numWords = dmaSizeValue & 0xFFFFFF;
                            uint32_t indexWidthBytes = indexIs32Bit ? 4 : 2;
                            uint32_t indexByteAddr = dmaBase | 0xA0000000u;
                            uint32_t indexByteSize = numWords * indexWidthBytes;

                            if (indexByteSize == 0 || indexByteSize > kMaxVertexBufferBytes)
                            {
                                indexResolutionFailed = true;
                            }
                            else
                            {
                                resolvedIndexData.resize(indexByteSize);
                                if (indexIs32Bit)
                                {
                                    for (uint32_t i = 0; i + 4 <= indexByteSize; i += 4)
                                    {
                                        uint32_t v = LoadU32(base, indexByteAddr + i);
                                        resolvedIndexData[i + 0] = (v >> 0) & 0xFF;
                                        resolvedIndexData[i + 1] = (v >> 8) & 0xFF;
                                        resolvedIndexData[i + 2] = (v >> 16) & 0xFF;
                                        resolvedIndexData[i + 3] = (v >> 24) & 0xFF;
                                    }
                                }
                                else
                                {
                                    for (uint32_t i = 0; i + 2 <= indexByteSize; i += 2)
                                    {
                                        uint16_t v = __builtin_bswap16(*reinterpret_cast<volatile uint16_t*>(base + indexByteAddr + i));
                                        resolvedIndexData[i + 0] = v & 0xFF;
                                        resolvedIndexData[i + 1] = (v >> 8) & 0xFF;
                                    }
                                }
                                // Final review finding I5: the real resolved
                                // index buffer can hold fewer indices than
                                // the draw initiator's own numIndices claims
                                // (a truncated/mismatched NUM_WORDS) --
                                // clamp what gets passed to Metal to what
                                // the buffer actually holds, never read past
                                // its end.
                                resolvedIndexCount = (indexByteSize / indexWidthBytes < di.numIndices)
                                    ? (indexByteSize / indexWidthBytes) : di.numIndices;
                            }
                        }
                    }

                    if (vertexByteSize == 0 || vertexByteSize > kMaxVertexBufferBytes)
                    {
                        if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: vertex buffer size %u bytes out of sane range, skipped\n", indent, vertexByteSize);
                    }
                    else if (isIndexed && indexResolutionFailed)
                    {
                        if (logFile_) fprintf(logFile_, "%s-> DRAW_INDX_2: kDMA index buffer could not be resolved, skipped\n", indent);
                    }
                    else
                    {
                        DrawCommand cmd;
                        cmd.primitiveType = mappedPrimType;
                        cmd.vertexData.resize(vertexByteSize);
                        for (uint32_t i = 0; i < vertexByteSize; i += 4)
                        {
                            uint32_t floatBits = LoadU32(base, vertexByteAddr + i);
                            cmd.vertexData[i + 0] = (floatBits >> 0) & 0xFF;
                            cmd.vertexData[i + 1] = (floatBits >> 8) & 0xFF;
                            cmd.vertexData[i + 2] = (floatBits >> 16) & 0xFF;
                            cmd.vertexData[i + 3] = (floatBits >> 24) & 0xFF;
                        }
                        uint32_t bufferVertexCapacity = vertexByteSize / 12;

                        if (isIndexed)
                        {
                            // Final review finding I1 (indexed side): vertexCount
                            // isn't consumed by drawIndexedPrimitives (Metal
                            // reads it from the index buffer), but keep it
                            // descriptive of the real buffer capacity.
                            cmd.vertexCount = bufferVertexCapacity;
                            cmd.indexData = std::move(resolvedIndexData);
                            cmd.indexCount = resolvedIndexCount;
                            cmd.indexIs32Bit = indexIs32Bit;
                        }
                        else
                        {
                            // Final review finding I1 (non-indexed side): the
                            // real draw count is the game's own numIndices
                            // (vertex indices 0..numIndices-1 for kAutoIndex),
                            // not however many vertices happen to fit in the
                            // declared buffer -- that size is only a safety
                            // cap, never the real draw count. A shared vertex
                            // buffer drawn by many small draws would otherwise
                            // redraw the whole buffer on every single call.
                            cmd.vertexCount = (bufferVertexCapacity < di.numIndices) ? bufferVertexCapacity : di.numIndices;
                            cmd.indexCount = 0;
                            cmd.indexIs32Bit = false;
                        }

                        if (logFile_)
                        {
                            fprintf(logFile_, "%s-> DRAW_INDX_2: primType=%u vertexCount=%u indexCount=%u (indexed=%s)\n",
                                indent, di.primType, cmd.vertexCount, cmd.indexCount, cmd.indexData.empty() ? "no" : "yes");
                        }
                        frameDrawList_.AddDrawCommand(std::move(cmd));
                    }
                }
            }
            else if (opcode == kOpcodeDrawIndx && count >= 2)
            {
                // DRAW_INDX has a leading viz-query-condition dword before
                // the draw initiator (Xenia's ExecutePacketType3_DRAW_INDX).
                uint32_t drawInitiatorValue = LoadU32(base, bufferAddr + offsetBytes + 8);
                gpuState_.WriteRegister(GpuRegisterState::kDrawInitiatorRegister, drawInitiatorValue);
            }
            else if (opcode == kOpcodeImLoadImmediate && count >= 2)
            {
                // Real semantics (Xenia's ExecutePacketType3_IM_LOAD_IMMEDIATE):
                // payload dword 0 is the shader type (0=vertex, 1=pixel),
                // dword 1 is start_size (bits 16-31 = start, expected 0;
                // bits 0-15 = size_dwords), followed by size_dwords raw
                // big-endian microcode dwords embedded directly in the
                // packet -- byte-swapped here the same way every other PM4
                // dword in this file already is.
                uint32_t shaderTypeValue = LoadU32(base, bufferAddr + offsetBytes + 4);
                uint32_t startSizeValue = LoadU32(base, bufferAddr + offsetBytes + 8);
                uint32_t start = startSizeValue >> 16;
                uint32_t sizeDwords = startSizeValue & 0xFFFF;

                if (start != 0)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: non-zero start=%u, skipped\n", indent, start);
                }
                else if (count - 2 < sizeDwords)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: packet too short for sizeDwords=%u (count=%u), skipped\n", indent, sizeDwords, count);
                }
                else if (shaderTypeValue != 0 && shaderTypeValue != 1)
                {
                    if (logFile_) fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: unrecognized shaderType=%u, skipped\n", indent, shaderTypeValue);
                }
                else
                {
                    std::vector<uint32_t> microcodeDwords(sizeDwords);
                    for (uint32_t i = 0; i < sizeDwords; i++)
                    {
                        microcodeDwords[i] = LoadU32(base, bufferAddr + offsetBytes + 12 + i * 4);
                    }
                    DecodedShaderProgram program = DecodeShaderMicrocode(microcodeDwords.data(), sizeDwords, (int)shaderTypeValue);
                    if (logFile_)
                    {
                        fprintf(logFile_, "%s-> IM_LOAD_IMMEDIATE: shaderType=%u sizeDwords=%u, %zu disassembly lines:\n",
                            indent, shaderTypeValue, sizeDwords, program.disassemblyLines.size());
                        for (const std::string& disasmLine : program.disassemblyLines)
                        {
                            fprintf(logFile_, "%s  %s\n", indent, disasmLine.c_str());
                        }
                    }
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
