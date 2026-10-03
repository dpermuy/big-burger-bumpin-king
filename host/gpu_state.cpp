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
