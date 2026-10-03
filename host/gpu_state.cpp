#include "gpu_state.h"
#include <cstdio>

void GpuRegisterState::WriteRegister(uint32_t index, uint32_t value)
{
    if (index >= kRegisterCount)
    {
        if (!loggedOutOfRange_)
        {
            fprintf(stderr, "[gpu_state] out-of-range register write index=0x%X (kRegisterCount=0x%X) -- ignoring (further occurrences not logged)\n",
                index, kRegisterCount);
            loggedOutOfRange_ = true;
        }
        return;
    }
    regs_[index] = value;
}

uint32_t GpuRegisterState::ReadRegister(uint32_t index) const
{
    if (index >= kRegisterCount)
    {
        if (!loggedOutOfRange_)
        {
            fprintf(stderr, "[gpu_state] out-of-range register read index=0x%X (kRegisterCount=0x%X) -- returning 0 (further occurrences not logged)\n",
                index, kRegisterCount);
            loggedOutOfRange_ = true;
        }
        return 0;
    }
    return regs_[index];
}

DrawInitiator GpuRegisterState::GetDrawInitiator() const
{
    uint32_t value = ReadRegister(kDrawInitiatorRegister);
    DrawInitiator result;
    result.primType = value & 0x3F;
    result.sourceSelect = (value >> 6) & 0x3;
    result.indexSize = (value >> 11) & 0x1;
    result.numIndices = (value >> 16) & 0xFFFF;
    return result;
}

VertexFetchConstant GpuRegisterState::GetVertexFetchConstant(uint32_t slot) const
{
    if (slot >= kVertexFetchConstantSlotCount)
    {
        return VertexFetchConstant{0, 0, 0, 0};
    }
    uint32_t baseIndex = kVertexFetchConstantBase + slot * 2;
    uint32_t dword0 = ReadRegister(baseIndex);
    uint32_t dword1 = ReadRegister(baseIndex + 1);
    VertexFetchConstant result;
    result.type = dword0 & 0x3;
    result.address = dword0 >> 2;
    result.endian = dword1 & 0x3;
    result.size = (dword1 >> 2) & 0xFFFFFF;
    return result;
}
