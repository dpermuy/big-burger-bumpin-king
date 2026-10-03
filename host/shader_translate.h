#pragma once
#include "shader_decode.h"
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

// A plain, Metal-free representation of a real vertex attribute format
// this translator recognizes. The Objective-C++ renderer maps this to
// the real symbolic MTLVertexFormat constant -- this header never
// embeds Apple's raw enum integer values.
enum class TranslatedVertexFormat
{
    Float3,
    Float4,
};

// A real vertex fetch instruction this translator recognized and turned
// into an attribute binding.
struct TranslatedAttribute
{
    uint32_t fetchConstantIndex; // 0-95, the real GpuRegisterState slot
    uint32_t destReg;            // which register this attribute lands in
    TranslatedVertexFormat format;
    uint32_t byteOffset;         // real offset field * 4
};

// A real ALU export-via-self-mov instruction this translator recognized.
struct TranslatedExport
{
    uint32_t exportRegister; // real ExportRegister value (vectorDest)
    uint32_t sourceRegister; // the real TEMP register both sources matched
};

// The result of attempting to translate one real shader's microcode.
// On failure, vertexShaderSource/fragmentShaderSource/attributes/exports
// are all empty -- callers must check success before using anything else.
struct TranslationResult
{
    bool success = false;
    std::string failureReason;
    std::string vertexShaderSource;   // real MSL vertex_main function text, only set for shaderType==0
    std::string fragmentShaderSource; // real MSL fragment_main function text, only set for shaderType==1
    std::vector<TranslatedAttribute> attributes;   // only populated for shaderType==0
    std::vector<TranslatedExport> vertexExports;   // only populated for shaderType==0
    std::vector<TranslatedExport> pixelExports;    // only populated for shaderType==1
    uint32_t vertexStrideBytes = 0; // real per-vertex byte stride, only set for shaderType==0
};

// dwords are already host-byte-order (matching this project's established
// convention). shaderType: 0 = vertex, 1 = pixel.
//
// Recognizes exactly two real instruction patterns -- a vertex fetch
// with a recognized format (Float3/Float4), and an ALU export-via-
// self-mov (vector_opc==2/kMax, both sources the same real TEMP
// register, non-zero write mask). A write-mask-0 ALU instruction is a
// real no-op and is skipped, not a failure. Anything else (real
// arithmetic, a non-zero constant reference, an unrecognized fetch
// format, real control flow beyond simple sequential EXEC blocks) fails
// translation for the whole shader -- this is a narrow, honest
// translator for exactly the passthrough pattern this project has ever
// observed, not a general Xenos-to-MSL compiler.
TranslationResult TranslateShader(const uint32_t* dwords, uint32_t dwordCount, int shaderType);

// Real, standard FNV-1a 32-bit hash (offset basis 2166136261, prime
// 16777619) over raw bytes -- used to detect when this project's real
// repeated shader reloads (confirmed: identical microcode every frame)
// describe unchanged content, so translation/compilation isn't redone
// every single frame.
uint32_t Fnv1aHash(const uint8_t* data, size_t len);

// Carries the latest translation ATTEMPT (success or not) from the GPU
// pump thread (where PM4_IM_LOAD_IMMEDIATE is parsed) to the Metal
// render thread. This cache never tries to preserve a prior success
// internally -- a failed UpdateX overwrites the previous (possibly
// successful) result. The renderer owns its own separate memory of the
// last successfully COMPILED Metal pipeline, and decides for itself
// whether to replace it based on what CurrentVertexShader/
// CurrentPixelShader return.
class ShaderTranslationCache
{
public:
    void UpdateVertexShader(uint32_t hash, TranslationResult result);
    void UpdatePixelShader(uint32_t hash, TranslationResult result);
    uint32_t CurrentVertexShaderHash();
    uint32_t CurrentPixelShaderHash();
    TranslationResult CurrentVertexShader();
    TranslationResult CurrentPixelShader();

private:
    std::mutex vertexMutex_;
    uint32_t vertexHash_ = 0;
    TranslationResult vertexResult_;

    std::mutex pixelMutex_;
    uint32_t pixelHash_ = 0;
    TranslationResult pixelResult_;
};
