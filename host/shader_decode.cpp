#include "shader_decode.h"

bool IsExecFamily(ControlFlowOpcode op)
{
    switch (op)
    {
        case ControlFlowOpcode::kExec:
        case ControlFlowOpcode::kExecEnd:
        case ControlFlowOpcode::kCondExec:
        case ControlFlowOpcode::kCondExecEnd:
        case ControlFlowOpcode::kCondExecPred:
        case ControlFlowOpcode::kCondExecPredEnd:
        case ControlFlowOpcode::kCondExecPredClean:
        case ControlFlowOpcode::kCondExecPredCleanEnd:
            return true;
        default:
            return false;
    }
}

namespace
{
    ControlFlowInstruction DecodeOneControlFlowInstruction(uint32_t word0, uint32_t word1_16bit)
    {
        ControlFlowInstruction instr;
        instr.address = word0 & 0xFFF;
        instr.count = (word0 >> 12) & 0x7;
        instr.sequence = (word0 >> 16) & 0xFFF;
        instr.opcode = static_cast<ControlFlowOpcode>((word1_16bit >> 12) & 0xF);
        return instr;
    }
}

void UnpackControlFlowPair(uint32_t d0, uint32_t d1, uint32_t d2,
    ControlFlowInstruction& outA, ControlFlowInstruction& outB)
{
    uint32_t aWord0 = d0;
    uint32_t aWord1 = d1 & 0xFFFF;
    uint32_t bWord0 = (d1 >> 16) | (d2 << 16);
    uint32_t bWord1 = d2 >> 16;

    outA = DecodeOneControlFlowInstruction(aWord0, aWord1);
    outB = DecodeOneControlFlowInstruction(bWord0, bWord1);
}
