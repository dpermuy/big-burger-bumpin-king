#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Real Xenos ALU instruction fields this decoder reports (bit-exact
// against Xenia's AluInstruction::Data). Full opcode NAME tables
// (AluVectorOpcode/AluScalarOpcode, each dozens of real values) are out
// of scope for this decoder -- opcodes are reported as bounded numeric
// values (vectorOpcode: 0-31, scalarOpcode: 0-63).
struct AluInstructionFields
{
    uint32_t vectorOpcode;   // 5 bits
    uint32_t scalarOpcode;   // 6 bits
    uint32_t vectorDest;     // 6 bits
    uint32_t scalarDest;     // 6 bits
    uint32_t vectorWriteMask; // 4 bits
    uint32_t scalarWriteMask; // 4 bits
    uint32_t src1Reg;        // 8 bits
    uint32_t src2Reg;        // 8 bits
    uint32_t src3Reg;        // 8 bits
};
AluInstructionFields DecodeAluInstruction(uint32_t word0, uint32_t word1, uint32_t word2);

// Real Xenos vertex fetch instruction fields (bit-exact against Xenia's
// VertexFetchInstruction::Data). fetchConstantIndex is the real formula
// (const_index * 3 + const_index_sel) that replaces this project's
// earlier hardcoded "always slot 0" vertex-format assumption.
struct VertexFetchInstructionFields
{
    uint32_t fetchConstantIndex; // 0-95, the real GpuRegisterState slot
    uint32_t destReg;            // 6 bits
    uint32_t srcReg;             // 6 bits
    uint32_t format;             // 6 bits (real xenos::VertexFormat value)
    uint32_t stride;             // 8 bits, in dwords
    int32_t offset;              // 23 bits, signed, in dwords
};
VertexFetchInstructionFields DecodeVertexFetchInstruction(uint32_t word0, uint32_t word1, uint32_t word2);

// Real, human-readable disassembly of a decoded shader microcode
// program -- one text line per instruction. No MSL generation, no
// control-flow execution/simulation; this is a static disassembly.
struct DecodedShaderProgram
{
    std::vector<std::string> disassemblyLines;
};

// dwords are already host-byte-order (the caller byte-swaps guest
// memory before calling this, matching this project's established
// convention). shaderType: 0 = vertex, 1 = pixel (real xenos::ShaderType
// values) -- used only to label the output, decode logic is identical
// for both.
DecodedShaderProgram DecodeShaderMicrocode(const uint32_t* dwords, uint32_t dwordCount, int shaderType);

// Real Xenos control-flow opcodes (xenos::ControlFlowOpcode). The shader
// microcode's control-flow program is a sequence of these, each either
// executing a block of ALU/fetch instructions (the 8 "EXEC-family"
// values -- see IsExecFamily) or controlling flow in a way this decoder
// does not resolve (loops, calls, jumps) -- it disassembles those as
// plain opcode + raw field text, it does not simulate them.
enum class ControlFlowOpcode : uint32_t
{
    kNop = 0,
    kExec = 1,
    kExecEnd = 2,
    kCondExec = 3,
    kCondExecEnd = 4,
    kCondExecPred = 5,
    kCondExecPredEnd = 6,
    kLoopStart = 7,
    kLoopEnd = 8,
    kCondCall = 9,
    kReturn = 10,
    kCondJmp = 11,
    kAlloc = 12,
    kCondExecPredClean = 13,
    kCondExecPredCleanEnd = 14,
    kMarkVsFetchDone = 15,
};

// True for the 8 real opcodes whose address/count fields reference a
// block of ALU/fetch instructions to execute (real Xenia
// IsControlFlowOpcodeExec semantics).
bool IsExecFamily(ControlFlowOpcode op);

// One decoded real control-flow instruction. address/count only have a
// defined meaning for EXEC-family opcodes (see IsExecFamily) -- they are
// still decoded and reported for every opcode, since the raw fields are
// real regardless of whether this decoder resolves their semantics.
struct ControlFlowInstruction
{
    ControlFlowOpcode opcode;
    uint32_t address;  // real: slot index into the same flat microcode array
    uint32_t count;    // real: number of ALU/fetch instructions this block executes
    uint32_t sequence; // real: 2 bits per instruction, bit (2*j) = ALU(0)/fetch(1) for instruction j
};

// Unpacks 2 logical 48-bit control-flow instructions from 3 raw
// microcode dwords (real Xenos packing -- confirmed exact against
// Xenia's own UnpackControlFlowInstructions).
void UnpackControlFlowPair(uint32_t d0, uint32_t d1, uint32_t d2,
    ControlFlowInstruction& outA, ControlFlowInstruction& outB);
