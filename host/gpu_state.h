#pragma once
#include <cstdint>

// Real Xenos GPU register state. Owns a flat register bank written to by
// GpuCommandTracer's PM4 TYPE0 and SET_CONSTANT packet handling
// (host/gpu_trace.cpp). No PM4-parsing knowledge lives here -- this is
// pure storage plus decoded bit-layout views on top of it, so later
// milestones add a new accessor here without ever touching storage.
class GpuRegisterState
{
public:
    // Covers every real sub-bank confirmed in Xenia's own source with
    // margin: REGISTERS (0x2000+), ALU (0x4000+), FETCH (0x4800+, 96
    // slots x 2 dwords = 192 dwords), BOOL (0x4900+), LOOP (0x4908+).
    static constexpr uint32_t kRegisterCount = 0x5000;

    // Out-of-range index: logged once (rate-limited) and ignored. A game
    // writing beyond this project's current margin is a real, informative
    // signal -- not a reason to corrupt memory or crash.
    void WriteRegister(uint32_t index, uint32_t value);

    // Out-of-range index: logged once (rate-limited) and returns 0.
    uint32_t ReadRegister(uint32_t index) const;

private:
    uint32_t regs_[kRegisterCount] = {};
    mutable bool loggedOutOfRange_ = false;
};
