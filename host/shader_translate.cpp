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

    // Tracks the most recent real full (non-mini) vertex fetch's real
    // fetchConstantIndex, so a following real mini-fetch can inherit it
    // (real Xenos semantics -- see host/shader_decode.h's own comment on
    // VertexFetchInstructionFields::isMiniFetch: a mini-fetch reuses the
    // preceding full fetch's real stride AND fetch constant, not its own
    // raw const_index/const_index_sel bits).
    bool sawFullFetch = false;
    uint32_t lastFullFetchConstantIndex = 0;

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
            bool isExecFamily = IsExecFamily(cf.opcode);
            bool isUnconditionalExec = (cf.opcode == ControlFlowOpcode::kExec) || (cf.opcode == ControlFlowOpcode::kExecEnd);

            if (isExecFamily && !isUnconditionalExec)
            {
                // Real conditional variants (kCondExec, kCondExecEnd,
                // kCondExecPred, kCondExecPredEnd, kCondExecPredClean,
                // kCondExecPredCleanEnd) have a real condition this
                // translator does not evaluate -- walking their block
                // as if unconditional would be wrong, so fail closed
                // for the whole shader instead.
                return FailedTranslation("conditional control flow not supported").result;
            }

            if (!isExecFamily)
            {
                // Only a real no-op (kNop) or a real allocation
                // directive with no ALU/fetch block of its own (kAlloc)
                // are safe to silently skip. Anything else (kLoopStart,
                // kLoopEnd, kCondCall, kReturn, kCondJmp) is real
                // control flow this translator does not resolve --
                // silently skipping it could lose real semantics while
                // still reporting overall success, so fail closed
                // instead.
                if (cf.opcode != ControlFlowOpcode::kNop && cf.opcode != ControlFlowOpcode::kAlloc)
                {
                    return FailedTranslation("unsupported control-flow opcode").result;
                }
                continue;
            }

            // isUnconditionalExec is true here (kExec/kExecEnd only).
            uint32_t candidateEnd = cf.address * 3;
            if (candidateEnd < cfEndDword)
            {
                cfEndDword = candidateEnd;
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

                    for (uint32_t comp = 0; comp < 4; comp++)
                    {
                        if (GetFetchSwizzleComponent(vf.destSwizzle, comp) != comp)
                        {
                            return FailedTranslation("non-identity fetch swizzle not supported").result;
                        }
                    }

                    uint32_t effectiveFetchConstantIndex;
                    if (vf.isMiniFetch)
                    {
                        // Real Xenos semantics (host/shader_decode.h):
                        // a mini-fetch reuses the preceding full fetch's
                        // real stride AND fetch constant -- its own raw
                        // const_index/const_index_sel bits are not the
                        // real fetch constant to use.
                        if (!sawFullFetch)
                        {
                            return FailedTranslation("mini-fetch with no preceding full fetch").result;
                        }
                        effectiveFetchConstantIndex = lastFullFetchConstantIndex;
                    }
                    else
                    {
                        effectiveFetchConstantIndex = vf.fetchConstantIndex;
                        sawFullFetch = true;
                        lastFullFetchConstantIndex = vf.fetchConstantIndex;
                        result.vertexStrideBytes = vf.stride * 4;
                    }

                    TranslatedAttribute attr;
                    attr.fetchConstantIndex = effectiveFetchConstantIndex;
                    attr.destReg = vf.destReg;
                    attr.format = translatedFormat;
                    attr.byteOffset = static_cast<uint32_t>(vf.offset) * 4;
                    result.attributes.push_back(attr);
                }
                else
                {
                    AluInstructionFields alu = DecodeAluInstruction(iw0, iw1, iw2);

                    if (alu.scalarWriteMask != 0 && alu.scalarOpcode != 50 /*kRetainPrev*/)
                    {
                        return FailedTranslation("scalar ALU op not recognized").result;
                    }

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

                    if (shaderType == 0)
                    {
                        // Only interpolator0 (0) and position (62) are
                        // recognized real vertex export registers.
                        if (alu.vectorDest != 0 && alu.vectorDest != 62)
                        {
                            return FailedTranslation("unrecognized vertex export register").result;
                        }
                        bool sourceWasFetched = false;
                        for (const TranslatedAttribute& fetchedAttr : result.attributes)
                        {
                            if (fetchedAttr.destReg == alu.src1Reg)
                            {
                                sourceWasFetched = true;
                                break;
                            }
                        }
                        if (!sourceWasFetched)
                        {
                            return FailedTranslation("export source register was never fetched").result;
                        }
                    }
                    else
                    {
                        // Only color0 (0) is a recognized real pixel
                        // export register.
                        if (alu.vectorDest != 0)
                        {
                            return FailedTranslation("unrecognized pixel export register").result;
                        }
                    }

                    for (uint32_t comp = 0; comp < 4; comp++)
                    {
                        if (ResolveAluSwizzleComponent(alu.src1Swizzle, comp) != comp
                            || ResolveAluSwizzleComponent(alu.src2Swizzle, comp) != comp)
                        {
                            return FailedTranslation("non-identity ALU swizzle not supported").result;
                        }
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
