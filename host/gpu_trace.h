#pragma once
#include <cstdint>
#include <cstdio>

class GpuCommandTracer
{
public:
    void RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw);
    void SetRptrWriteBackAddr(uint32_t addr);
    void SetIdentifierAddr(uint32_t addr);
    void SetGraphicsInterruptCallback(uint32_t callback, uint32_t context);
    void ScanAndTraceFrame(uint8_t* base);
    bool HasRingBuffer() const { return ringBufferBase_ != 0; }
    uint32_t GraphicsInterruptCallback() const { return graphicsInterruptCallback_; }
    uint32_t GraphicsInterruptContext() const { return graphicsInterruptContext_; }

private:
    void EnsureLogOpen();
    // Parses PM4 packets starting at (bufferAddr, startOffsetBytes) up to sizeBytes,
    // logs them (indented by depth), and returns the offset reached. Used for the main
    // ring buffer (depth 0, resumed from the last frame's offset) and recursively for
    // PM4_INDIRECT_BUFFER (opcode 0x3F) targets, which are short-lived "call into this
    // other buffer" jumps -- always scanned fresh from 0 rather than incrementally
    // tracked like the main ring. depth is capped to guard against a malformed or
    // cyclic indirect-buffer chain.
    uint32_t ScanBuffer(uint8_t* base, uint32_t bufferAddr, uint32_t startOffsetBytes, uint32_t sizeBytes, int depth);

    uint32_t ringBufferBase_ = 0;
    uint32_t ringBufferSize_ = 0;
    uint32_t rptrWriteBackAddr_ = 0;
    uint32_t identifierAddr_ = 0;
    uint32_t graphicsInterruptCallback_ = 0;
    uint32_t graphicsInterruptContext_ = 0;
    uint32_t lastParsedOffset_ = 0;
    uint32_t frameCounter_ = 0;
    FILE* logFile_ = nullptr;
};

extern GpuCommandTracer g_gpuTracer;
