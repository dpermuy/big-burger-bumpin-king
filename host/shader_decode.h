#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
