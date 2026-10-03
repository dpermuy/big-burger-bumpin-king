#pragma once
#include <cstdint>

// Bit-exact match to Xenia's reg::VGT_DRAW_INITIATOR (real register 0x21FC).
struct DrawInitiator
{
    uint32_t primType;      // bits 0-5
    uint32_t sourceSelect;  // bits 6-7: 0=kDMA (indexed), 1=kImmediate (unsupported), 2=kAutoIndex
    uint32_t indexSize;     // bit 11: 0=16-bit, 1=32-bit
    uint32_t numIndices;    // bits 16-31
};

// Bit-exact match to Xenia's xe_gpu_vertex_fetch_t. Shared address space
// with texture fetch constants (type distinguishes which); vertex fetch
// uses type == 3.
struct VertexFetchConstant
{
    uint32_t type;      // dword_0 bits 0-1
    uint32_t address;   // dword_0 bits 2-31, in dwords (byte address = address << 2)
    uint32_t endian;    // dword_1 bits 0-1
    uint32_t size;      // dword_1 bits 2-25, in 32-bit words
};

// Real Xenos GPU register state. Owns a flat register bank written to by
// GpuCommandTracer's PM4 TYPE0 and SET_CONSTANT packet handling
// (host/gpu_trace.cpp). No PM4-parsing knowledge lives here -- this is
// pure storage plus decoded bit-layout views on top of it, so later
// milestones add a new accessor here without ever touching storage.
class GpuRegisterState
{
public:
    // Exact real Xenia register file size: the highest real named
    // register is 0x5002 (SHADER_CONSTANT_FLUSH_FETCH_2), confirmed
    // against Xenia's register_table.inc. 0x5000 undercounted this by
    // three real registers (SHADER_CONSTANT_FLUSH_FETCH_0-2), which were
    // observed being silently dropped in a live run -- fixed per final
    // review finding I2.
    static constexpr uint32_t kRegisterCount = 0x5003;

    // Out-of-range index: logged once (rate-limited) and ignored. A game
    // writing beyond this project's current margin is a real, informative
    // signal -- not a reason to corrupt memory or crash.
    void WriteRegister(uint32_t index, uint32_t value);

    // Out-of-range index: logged once (rate-limited) and returns 0.
    uint32_t ReadRegister(uint32_t index) const;

    // Real register index for VGT_DRAW_INITIATOR (confirmed against
    // Xenia's register_table.inc) -- falls inside the already-observed
    // real TYPE0 range 0x2000-0x2312 (Finding 35, prior investigation).
    static constexpr uint32_t kDrawInitiatorRegister = 0x21FC;
    DrawInitiator GetDrawInitiator() const;

    // Real base register for vertex/texture fetch constants (confirmed:
    // XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 = 0x4800). 96 slots, 2 dwords
    // each. slot >= 96 returns a zeroed struct.
    static constexpr uint32_t kVertexFetchConstantBase = 0x4800;
    static constexpr uint32_t kVertexFetchConstantSlotCount = 96;
    VertexFetchConstant GetVertexFetchConstant(uint32_t slot) const;

private:
    uint32_t regs_[kRegisterCount] = {};
    mutable bool loggedOutOfRange_ = false;
};
