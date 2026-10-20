// SPDX-License-Identifier: GPL-3.0-only
#include "sound_checks.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include <stdexcept>

namespace openkai2 {
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

void verify_sound_registers() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    for (const uint32_t core : {0u, 1u}) {
        const auto offset = core * 0x400u;
        const auto attr = 0xBF90019Au + offset;
        const auto stat = 0xBF900344u + offset;
        const auto dma = core ? 0xBF801508u : 0xBF8010C8u;
        const auto otherStat = core ? 0xBF900344u : 0xBF900744u;
        memory.reset();
        memory.write16(attr - 2u, 0xABCDu);
        memory.write16(attr, 0x8030u);
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 transfer engine did not become ready");
        require(memory.read16(otherStat) == 0u, "SPU2 transfer state leaked into the other core");
        memory.write32(dma - 4u, 0x00010010u);
        memory.write32(dma, 0x01000201u);
        require((memory.read32(dma) & 0x01000000u) == 0u, "SPU2 DMA did not finish");
        const auto interrupt = memory.takeDmaStart();
        require(interrupt && interrupt->irq == (core ? 0x28 : 0x24), "SPU2 DMA interrupt was lost");
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 DMA completion was lost");
        memory.write16(attr, 0x8000u);
        require((memory.read16(stat) & 0x7FFu) == 0u, "SPU2 reset is stuck on stale DMA status");
        require(memory.read16(attr - 2u) == 0xABCDu, "SPU2 ATTR write corrupted MMIX");
        require(memory.read16(stat & 0x1FFFFFFFu) == memory.read16(stat), "SPU2 KSEG alias differs");
        // Exercise byte and word accesses to the same upper-halfword ATTR.
        memory.write8(attr, 0x20u);
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 byte write did not enable transfer");
        memory.write32(attr - 2u, 0x80001234u);
        require(memory.read16(stat) == 0u && memory.read16(attr - 2u) == 0x1234u,
                "SPU2 word write did not acknowledge transfer status");
    }

    // Execute a guest reset-and-poll sequence after a completed transfer.
    // A stale status bit makes the real IOP interpreter loop indefinitely.
    memory.reset();
    memory.write32(0x1F8010C8u, 0x01000201u);
    const uint32_t program[] = {
        0x3C08BF90u, // lui t0, 0xbf90
        0xA500019Au, // sh zero, 0x19a(t0)
        0x34098000u, // ori t1, zero, 0x8000
        0xA509019Au, // sh t1, 0x19a(t0)
        0x95020344u, // lhu v0, 0x344(t0)
        0x00000000u, // load delay
        0x304207FFu, // andi v0, v0, 0x7ff
        0x1440FFFCu, // bnez v0, poll
        0x00000000u, // branch delay
        0x24030001u, // addiu v1, zero, 1
        0x03E00008u, // jr ra
        0x00000000u,
    };
    require(memory.writeRam(0x2000u, program, sizeof(program)), "Cannot write SPU2 guest regression");
    IopCpuCore cpu(memory);
    IopCpuState state{};
    state.pc = 0x2000u;
    state.gpr[31] = 0x4000u;
    for (unsigned steps = 0; steps < 100u && state.pc != 0x4000u; ++steps)
        require(cpu.executeInstruction(state) && !state.exception, "SPU2 guest regression raised an exception");
    require(state.pc == 0x4000u && state.gpr[3] == 1u, "IOP sound initialization did not leave its status poll");
}
} // namespace openkai2
