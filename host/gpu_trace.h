#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>

struct PPCContext;

class GpuCommandTracer
{
public:
    void RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw);
    void SetRptrWriteBackAddr(uint32_t addr);
    void SetIdentifierAddr(uint32_t addr);
    void SetGraphicsInterruptCallback(uint32_t callback, uint32_t context);
    // Real second buffer (Finding 38): a dedicated worker thread (entry 0x820C8D28)
    // builds real PM4 packets (confirmed: sub_820C4678 hand-constructs a real
    // EVENT_WRITE_SHD header) into this buffer, independent of the main ring buffer,
    // which is why the main ring goes quiet after frame 2 while real GPU-adjacent work
    // keeps happening. addr/size are already known host-side (VdGetSystemCommandBuffer
    // owns this allocation) -- just needs to be scanned the same way as the main ring.
    void RegisterSystemCommandBuffer(uint32_t addr, uint32_t size);
    void ScanAndTraceFrame(PPCContext& ctx, uint8_t* base);
    bool HasRingBuffer();
    uint32_t GraphicsInterruptCallback();
    uint32_t GraphicsInterruptContext();

private:
    // Findings 55/56/57: ring-space wait loops (sub_820B4EE8) deadlocked because
    // this tracer's scan/fence-advance only ever ran synchronously inside VdSwap,
    // on the same CPU thread that could get stuck waiting on it. Scanning now runs
    // from a dedicated pump thread (host/main.cpp) independent of VdSwap, so every
    // field below is genuinely cross-thread: VdXxx setters write from the main
    // thread, ScanAndTraceFrame reads/writes from the pump thread. One mutex over
    // all of it -- writes are init-time-rare, so lock contention is a non-issue.
    // Recursive: ScanAndTraceFrame holds the lock while PM4_INTERRUPT invokes real
    // guest code (the registered interrupt callback), which could in principle call
    // back into another VdXxx export on the same (pump) thread.
    std::recursive_mutex mutex_;
    void EnsureLogOpen();
    // Parses PM4 packets starting at (bufferAddr, startOffsetBytes) up to sizeBytes,
    // logs them (indented by depth), and returns the offset reached. Used for the main
    // ring buffer (depth 0, resumed from the last frame's offset) and recursively for
    // PM4_INDIRECT_BUFFER (opcode 0x3F) targets, which are short-lived "call into this
    // other buffer" jumps -- always scanned fresh from 0 rather than incrementally
    // tracked like the main ring. depth is capped to guard against a malformed or
    // cyclic indirect-buffer chain. ctx is threaded through so PM4_INTERRUPT (0x54) can
    // invoke the registered graphics interrupt callback for real, matching real hardware
    // (Xenia's ExecutePacketType3_INTERRUPT), instead of only the synthetic per-VdSwap
    // vblank firing.
    uint32_t ScanBuffer(PPCContext& ctx, uint8_t* base, uint32_t bufferAddr, uint32_t startOffsetBytes, uint32_t sizeBytes, int depth);

    uint32_t ringBufferBase_ = 0;
    uint32_t ringBufferSize_ = 0;
    uint32_t rptrWriteBackAddr_ = 0;
    uint32_t identifierAddr_ = 0;
    uint32_t graphicsInterruptCallback_ = 0;
    uint32_t graphicsInterruptContext_ = 0;
    uint32_t lastParsedOffset_ = 0;
    uint32_t frameCounter_ = 0;
    uint32_t vblankCounter_ = 0;
    uint32_t systemCmdBufAddr_ = 0;
    uint32_t systemCmdBufSize_ = 0;
    uint32_t systemCmdBufLastOffset_ = 0;
    FILE* logFile_ = nullptr;
};

extern GpuCommandTracer g_gpuTracer;
