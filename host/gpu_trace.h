#pragma once
#include <cstdint>
#include <cstdio>

class GpuCommandTracer
{
public:
    void RegisterRingBuffer(uint32_t physAddr, uint32_t sizeLog2Raw);
    void SetRptrWriteBackAddr(uint32_t addr);
    void SetIdentifierAddr(uint32_t addr);
    void ScanAndTraceFrame(uint8_t* base);
    bool HasRingBuffer() const { return ringBufferBase_ != 0; }

private:
    void EnsureLogOpen();

    uint32_t ringBufferBase_ = 0;
    uint32_t ringBufferSize_ = 0;
    uint32_t rptrWriteBackAddr_ = 0;
    uint32_t identifierAddr_ = 0;
    uint32_t lastParsedOffset_ = 0;
    uint32_t frameCounter_ = 0;
    FILE* logFile_ = nullptr;
};

extern GpuCommandTracer g_gpuTracer;
