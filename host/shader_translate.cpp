#include "shader_translate.h"
#include <cstdio>

namespace
{
    struct FailedTranslation
    {
        TranslationResult result;
        FailedTranslation(const std::string& reason)
        {
            result.success = false;
            result.failureReason = reason;
        }
    };
}

TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType)
{
    TranslationResult result;
    result.success = true;

    // Same real EXEC-family-bound-shrinking walk 3a's own
    // DecodeShaderMicrocode uses (confirmed exact, including 3a's own
    // final-review fix for where the real control-flow program ends).
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
                    return FailedTranslation("control-flow address/count referenced a slot past the real microcode end").result;
                }

                uint32_t iw0 = dwords[slotDwordStart];
                uint32_t iw1 = dwords[slotDwordStart + 1];
                uint32_t iw2 = dwords[slotDwordStart + 2];
                bool isFetch = (cf.sequence >> (2 * j)) & 0x1;

                if (isFetch)
                {
                    uint32_t fetchOpcode = iw0 & 0x1F;
                    if (fetchOpcode != 0)
                    {
                        return FailedTranslation("a real texture fetch has no recognized translation").result;
                    }
                    VertexFetchInstructionFields vf = DecodeVertexFetchInstruction(iw0, iw1, iw2);
                    TranslatedVertexFormat translatedFormat;
                    if (vf.format == 57) translatedFormat = TranslatedVertexFormat::Float3;
                    else if (vf.format == 38) translatedFormat = TranslatedVertexFormat::Float4;
                    else return FailedTranslation("unrecognized real vertex-fetch format").result;

                    TranslatedAttribute attr;
                    attr.fetchConstantIndex = vf.fetchConstantIndex;
                    attr.destReg = vf.destReg;
                    attr.format = translatedFormat;
                    attr.byteOffset = static_cast<uint32_t>(vf.offset) * 4;
                    result.attributes.push_back(attr);

                    if (!vf.isMiniFetch)
                    {
                        result.vertexStrideBytes = vf.stride * 4;
                    }
                }
                else
                {
                    AluInstructionFields alu = DecodeAluInstruction(iw0, iw1, iw2);
                    if (alu.vectorWriteMask == 0)
                    {
                        continue; // real no-op/filler, not a failure
                    }
                    bool isSelfMov = (alu.vectorOpcode == 2) && (alu.src1Reg == alu.src2Reg)
                        && alu.src1Sel && alu.src2Sel;
                    if (!isSelfMov)
                    {
                        return FailedTranslation("a real ALU instruction outside the recognized export-via-self-mov pattern").result;
                    }

                    TranslatedExport exp;
                    exp.exportRegister = alu.vectorDest;
                    exp.sourceRegister = alu.src1Reg;
                    if (shaderType == 1)
                    {
                        result.pixelExports.push_back(exp);
                    }
                    else
                    {
                        result.vertexExports.push_back(exp);
                    }
                }
            }
        }
    }

    if (shaderType == 0)
    {
        if (result.attributes.empty() || result.vertexExports.empty())
        {
            return FailedTranslation("no real attributes or exports recognized").result;
        }

        char source[2048];
        std::string attributeFields;
        std::string assignments;
        for (size_t i = 0; i < result.attributes.size(); i++)
        {
            const char* mslType = (result.attributes[i].format == TranslatedVertexFormat::Float3) ? "float3" : "float4";
            char fieldLine[128];
            snprintf(fieldLine, sizeof(fieldLine), "    %s r%u [[attribute(%zu)]];\n", mslType, result.attributes[i].destReg, i);
            attributeFields += fieldLine;
        }
        for (const TranslatedExport& e : result.vertexExports)
        {
            char assignLine[128];
            if (e.exportRegister == 62)
            {
                snprintf(assignLine, sizeof(assignLine), "    out.position = float4(in.r%u.xyz, 1.0);\n    out.pointSize = 8.0;\n", e.sourceRegister);
            }
            else
            {
                snprintf(assignLine, sizeof(assignLine), "    out.interpolator%u = float4(in.r%u);\n", e.exportRegister, e.sourceRegister);
            }
            assignments += assignLine;
        }
        snprintf(source, sizeof(source),
            "struct VertexIn {\n%s};\n"
            "struct RasterizerData {\n    float4 position [[position]];\n    float pointSize [[point_size]];\n    float4 interpolator0;\n};\n"
            "vertex RasterizerData vertex_main(VertexIn in [[stage_in]]) {\n    RasterizerData out;\n%s    return out;\n}\n",
            attributeFields.c_str(), assignments.c_str());
        result.vertexShaderSource = source;
    }
    else
    {
        if (result.pixelExports.empty())
        {
            return FailedTranslation("no real pixel exports recognized").result;
        }
        result.fragmentShaderSource =
            "fragment float4 fragment_main(RasterizerData in [[stage_in]]) {\n"
            "    return in.interpolator0;\n"
            "}\n";
    }

    return result;
}
