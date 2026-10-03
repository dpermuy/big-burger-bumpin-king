#include "shader_decode.h"
#include <cstdio>

AluInstructionFields DecodeAluInstruction(uint32_t word0, uint32_t word1, uint32_t word2)
{
    (void)word1; // swizzle/predication/negate fields not decoded in this pass
    AluInstructionFields fields;
    fields.vectorDest = word0 & 0x3F;
    fields.scalarDest = (word0 >> 8) & 0x3F;
    fields.vectorWriteMask = (word0 >> 16) & 0xF;
    fields.scalarWriteMask = (word0 >> 20) & 0xF;
    fields.scalarOpcode = (word0 >> 26) & 0x3F;
    fields.src1Reg = (word2 >> 16) & 0xFF;
    fields.src2Reg = (word2 >> 8) & 0xFF;
    fields.src3Reg = word2 & 0xFF;
    fields.vectorOpcode = (word2 >> 24) & 0x1F;
    return fields;
}

VertexFetchInstructionFields DecodeVertexFetchInstruction(uint32_t word0, uint32_t word1, uint32_t word2)
{
    uint32_t constIndex = (word0 >> 20) & 0x1F;
    uint32_t constIndexSel = (word0 >> 25) & 0x3;
    VertexFetchInstructionFields fields;
    fields.fetchConstantIndex = constIndex * 3 + constIndexSel;
    fields.srcReg = (word0 >> 5) & 0x3F;
    fields.destReg = (word0 >> 12) & 0x3F;
    fields.format = (word1 >> 16) & 0x3F;
    fields.stride = word2 & 0xFF;
    uint32_t rawOffset = (word2 >> 8) & 0x7FFFFF;
    // Sign-extend the real 23-bit signed offset field.
    if (rawOffset & 0x400000) rawOffset |= 0xFF800000;
    fields.offset = static_cast<int32_t>(rawOffset);
    return fields;
}

DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType)
{
    DecodedShaderProgram program;
    char line[256];
    const char* shaderTag = (shaderType == 1) ? "PS" : "VS";

    uint32_t cfPairCount = dwordCount / 3;
    for (uint32_t i = 0; i < cfPairCount; i++)
    {
        ControlFlowInstruction a, b;
        UnpackControlFlowPair(dwords[i * 3], dwords[i * 3 + 1], dwords[i * 3 + 2], a, b);

        ControlFlowInstruction pairInstrs[2] = { a, b };
        for (int which = 0; which < 2; which++)
        {
            const ControlFlowInstruction& cf = pairInstrs[which];
            const char* opcodeName = "UNKNOWN";
            switch (cf.opcode)
            {
                case ControlFlowOpcode::kNop: opcodeName = "kNop"; break;
                case ControlFlowOpcode::kExec: opcodeName = "kExec"; break;
                case ControlFlowOpcode::kExecEnd: opcodeName = "kExecEnd"; break;
                case ControlFlowOpcode::kCondExec: opcodeName = "kCondExec"; break;
                case ControlFlowOpcode::kCondExecEnd: opcodeName = "kCondExecEnd"; break;
                case ControlFlowOpcode::kCondExecPred: opcodeName = "kCondExecPred"; break;
                case ControlFlowOpcode::kCondExecPredEnd: opcodeName = "kCondExecPredEnd"; break;
                case ControlFlowOpcode::kLoopStart: opcodeName = "kLoopStart"; break;
                case ControlFlowOpcode::kLoopEnd: opcodeName = "kLoopEnd"; break;
                case ControlFlowOpcode::kCondCall: opcodeName = "kCondCall"; break;
                case ControlFlowOpcode::kReturn: opcodeName = "kReturn"; break;
                case ControlFlowOpcode::kCondJmp: opcodeName = "kCondJmp"; break;
                case ControlFlowOpcode::kAlloc: opcodeName = "kAlloc"; break;
                case ControlFlowOpcode::kCondExecPredClean: opcodeName = "kCondExecPredClean"; break;
                case ControlFlowOpcode::kCondExecPredCleanEnd: opcodeName = "kCondExecPredCleanEnd"; break;
                case ControlFlowOpcode::kMarkVsFetchDone: opcodeName = "kMarkVsFetchDone"; break;
            }
            snprintf(line, sizeof(line), "[%s] CF pair=%u.%d %s address=%u count=%u sequence=0x%03X",
                shaderTag, i, which, opcodeName, cf.address, cf.count, cf.sequence);
            program.disassemblyLines.push_back(line);

            if (!IsExecFamily(cf.opcode))
            {
                continue;
            }

            for (uint32_t j = 0; j < cf.count; j++)
            {
                uint32_t slot = cf.address + j;
                uint32_t slotDwordStart = slot * 3;
                if (slotDwordStart + 2 >= dwordCount)
                {
                    snprintf(line, sizeof(line), "[%s]   instr %u: slot %u out of range (dwordCount=%u), skipped",
                        shaderTag, j, slot, dwordCount);
                    program.disassemblyLines.push_back(line);
                    continue;
                }

                uint32_t iw0 = dwords[slotDwordStart];
                uint32_t iw1 = dwords[slotDwordStart + 1];
                uint32_t iw2 = dwords[slotDwordStart + 2];
                bool isFetch = (cf.sequence >> (2 * j)) & 0x1;

                if (isFetch)
                {
                    VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(iw0, iw1, iw2);
                    snprintf(line, sizeof(line), "[%s]   instr %u: FETCH fetchConstantIndex=%u destReg=%u srcReg=%u format=%u stride=%u offset=%d",
                        shaderTag, j, vf.fetchConstantIndex, vf.destReg, vf.srcReg, vf.format, vf.stride, vf.offset);
                }
                else
                {
                    AluInstructionFields alu = DecodeAluInstruction(iw0, iw1, iw2);
                    snprintf(line, sizeof(line), "[%s]   instr %u: ALU vector_opc=%u scalar_opc=%u vectorDest=%u scalarDest=%u src1=%u src2=%u src3=%u",
                        shaderTag, j, alu.vectorOpcode, alu.scalarOpcode, alu.vectorDest, alu.scalarDest, alu.src1Reg, alu.src2Reg, alu.src3Reg);
                }
                program.disassemblyLines.push_back(line);
            }
        }
    }

    return program;
}

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
