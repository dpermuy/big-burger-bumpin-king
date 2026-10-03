#include "shader_decode.h"
#include <cstdio>

AluInstructionFields DecodeAluInstruction(uint32_t word0, uint32_t word1, uint32_t word2)
{
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
    fields.src1Sel = ((word2 >> 31) & 0x1) != 0;
    fields.src2Sel = ((word2 >> 30) & 0x1) != 0;
    fields.src3Sel = ((word2 >> 29) & 0x1) != 0;
    fields.src1Swizzle = (word1 >> 16) & 0xFF;
    fields.src2Swizzle = (word1 >> 8) & 0xFF;
    fields.src3Swizzle = word1 & 0xFF;
    fields.src3Negate = ((word1 >> 24) & 0x1) != 0;
    fields.src2Negate = ((word1 >> 25) & 0x1) != 0;
    fields.src1Negate = ((word1 >> 26) & 0x1) != 0;
    fields.isPredicated = ((word1 >> 28) & 0x1) != 0;
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
    fields.isMiniFetch = ((word1 >> 30) & 0x1) != 0;
    fields.destSwizzle = word1 & 0xFFF;
    return fields;
}

DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType)
{
    DecodedShaderProgram program;
    char line[256];
    const char* shaderTag = (shaderType == 1) ? "PS" : "VS";

    // Final review finding I1: the naive dwordCount/3 bound only limits
    // how far the scan COULD go -- the real control-flow program ends
    // at the first EXEC-family instruction's own address (that's where
    // ALU/fetch instruction data starts, confirmed live: real address=3
    // for this project's own vertex shader, and slots 0-2 are exactly
    // its real 1-pair CF program). cfEndDword shrinks as each
    // EXEC-family instruction is found; the loop condition re-checks it
    // every iteration, so once it shrinks below the next pair's own
    // position, that pair (real ALU/fetch data) is never misread as
    // control flow.
    uint32_t cfEndDword = (dwordCount / 3) * 3;
    for (uint32_t i = 0; i * 3 < cfEndDword; i++)
    {
        ControlFlowInstruction a, b;
        UnpackControlFlowPair(dwords[i * 3], dwords[i * 3 + 1], dwords[i * 3 + 2], a, b);

        ControlFlowInstruction pairInstrs[2] = { a, b };
        for (int which = 0; which < 2; which++)
        {
            const ControlFlowInstruction& cf = pairInstrs[which];
            if (IsExecFamily(cf.opcode))
            {
                uint32_t candidateEnd = cf.address * 3;
                if (candidateEnd < cfEndDword)
                {
                    cfEndDword = candidateEnd;
                }
            }
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
                    // Final review finding I2: the real fetch opcode
                    // (word0 bits 0-4) distinguishes a vertex fetch
                    // (kVertexFetch=0) from a texture fetch -- a texture
                    // fetch's const_index field means something
                    // different, so it must not go through
                    // DecodeVertexFetchInstruction's formula.
                    uint32_t fetchOpcode = iw0 & 0x1F;
                    if (fetchOpcode != 0)
                    {
                        snprintf(line, sizeof(line), "[%s]   instr %u: TFETCH opcode=%u (not decoded -- texture sampling out of scope)",
                            shaderTag, j, fetchOpcode);
                    }
                    else
                    {
                        VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(iw0, iw1, iw2);
                        snprintf(line, sizeof(line), "[%s]   instr %u: FETCH fetchConstantIndex=%u destReg=%u srcReg=%u format=%u stride=%u offset=%d isMiniFetch=%d",
                            shaderTag, j, vf.fetchConstantIndex, vf.destReg, vf.srcReg, vf.format, vf.stride, vf.offset, vf.isMiniFetch ? 1 : 0);
                    }
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

uint32_t ResolveAluSwizzleComponent(uint32_t rawSwizzle, uint32_t destComponent)
{
    return ((rawSwizzle >> (2 * destComponent)) + destComponent) & 0x3;
}

uint32_t GetFetchSwizzleComponent(uint32_t rawSwizzle, uint32_t component)
{
    return (rawSwizzle >> (3 * component)) & 0x7;
}
