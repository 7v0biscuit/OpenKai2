// SPDX-License-Identifier: GPL-3.0-only
#include "select_screen_checks.h"
#include "mods/select_screen.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// Runs the translated select-screen code on the original 30-cell wheel and on
// one with added cells, and compares it with the behaviour of the original.
namespace openkai2 {
void verify_select_screen(PS2Runtime &runtime, uint8_t *ram) {
    auto require = [](bool ok, const std::string &why) {
        if (!ok)
            throw std::runtime_error("Select screen: " + why);
    };
    struct Restore {
        PS2Runtime &runtime;
        std::vector<std::pair<uint32_t, PS2Runtime::RecompiledFunction>> entries;
        ~Restore() {
            for (auto i = entries.rbegin(); i != entries.rend(); ++i)
                runtime.replaceFunction(i->first, i->second);
            mods::reset_select_screen();
        }
        void replace(uint32_t at, PS2Runtime::RecompiledFunction fn) {
            entries.emplace_back(at, runtime.lookupFunction(at));
            runtime.replaceFunction(at, fn);
        }
    } restore{runtime, {}};
    constexpr uint32_t state = 0x630000u, returned = 0x29f00u, sp = 0x1fffde0u, packet = 0x640000u;
    constexpr uint32_t sheet = 0x650000u, sprites = 0x651000u, sprite_data = 0x660000u;
    constexpr uint32_t grid = mods::roster_select_grid;
    auto &memory = runtime.memory();
    const auto run = [&](uint32_t pc, uint32_t a0, uint32_t a1) {
        R5900Context c{};
        c.pc = pc;
        c.r[4] = _mm_cvtsi32_si128(int32_t(a0));
        c.r[5] = _mm_cvtsi32_si128(int32_t(a1));
        c.r[28] = _mm_cvtsi32_si128(0x3ac4f0u);
        c.r[29] = _mm_cvtsi32_si128(sp);
        c.r[31] = _mm_cvtsi32_si128(returned);
        for (unsigned step = 0; step < 64u && c.pc != returned; ++step) {
            const auto fn = runtime.lookupFunction(c.pc);
            require(fn != nullptr, "reached an untranslated continuation");
            try {
                fn(ram, &c, &runtime);
            } catch (const EeDispatcherTransfer &) {
            }
        }
        require(c.pc == returned, "did not return");
    };
    std::vector<uint8_t> saved(ram + grid, ram + grid + 64);
    std::vector<uint8_t> savedCells(ram + mods::select_id_cells, ram + mods::select_id_cells + 64);
    struct Tables {
        uint8_t *ram;
        std::vector<uint8_t> grid, cells;
        ~Tables() {
            std::memcpy(ram + mods::roster_select_grid, grid.data(), grid.size());
            std::memcpy(ram + mods::select_id_cells, cells.data(), cells.size());
        }
    } tables{ram, saved, savedCells};

    // The original cell builder: characters, unlocked cell numbers, the count,
    // the cursor's place among unlocked cells and each cell's wheel angle.
    const auto build_cells = [&](uint32_t cells, uint64_t mask, uint32_t cursor) {
        std::memset(ram + state, 0, 0x1000u);
        std::memset(ram + state + 208 + 48 * cells, 0xa5, 64);
        std::memcpy(ram + state + 112, &mask, 8);
        memory.write32(state + 192, cursor);
        run(0x2aa110u, state, 0);
        int32_t count = 0, place = -1;
        for (uint32_t i = 0; i < cells; ++i) {
            const uint32_t cell = state + 208 + 48 * i;
            const bool open = (mask >> i) & 1u;
            require(ram[cell + 32] == ram[grid + i], "cell " + std::to_string(i) + " holds the wrong character");
            require(int8_t(ram[cell + 33]) == (open ? int32_t(i) : -1), "cell " + std::to_string(i) + " lock is wrong");
            if (open && i == cursor)
                place = count;
            count += open;
        }
        require(int32_t(memory.read32(state + 196)) == count, "the unlocked count is wrong");
        int32_t k = 0;
        for (uint32_t i = 0; i < cells; ++i)
            if ((mask >> i) & 1u)
                require(int32_t(memory.read32(state + 208 + 48 * i + 36)) == (k++ * 65536) / count,
                        "cell " + std::to_string(i) + " has the wrong wheel angle");
        if (place < 0) {
            uint32_t first = 0;
            while (!((mask >> first) & 1u))
                ++first;
            require(memory.read32(state + 192) == first && ram[state + 128] == ram[grid + first],
                    "a locked cursor did not move to the first open cell");
            place = 0;
        }
        require(int32_t(memory.read32(state + 200)) == place, "the cursor's place on the wheel is wrong");
        for (unsigned i = 0; i < 64; ++i)
            require(ram[state + 208 + 48 * cells + i] == 0xa5, "wrote past the cells");
    };

    for (uint32_t i = 0; i < 64; ++i)
        ram[grid + i] = uint8_t(i < 29 ? i : i == 29 ? mods::select_random_id : 0xff);
    mods::reset_select_screen();
    build_cells(30, 0x3fffffffull, 5);
    build_cells(30, 0x2000000aull, 2); // locked cursor
    build_cells(30, (1ull << 29) | (1ull << 7), 29);

    // Three added cells before Random.
    std::vector<mods::RosterCharacter> added(3);
    const uint32_t slots[3] = {13, 14, 44};
    for (unsigned i = 0; i < 3; ++i) {
        added[i].slot = slots[i];
        added[i].base = 4;
        added[i].new_cell = true;
    }
    added[2].icon_cell = 7;
    std::memset(ram + mods::select_id_cells, 0xff, 64);
    ram[mods::select_id_cells + 4] = 4;
    require(mods::configure_select_screen(ram, PS2_RAM_SIZE, added).size() == 4 && mods::select_cells() == 33,
            "added cells were not configured");
    require(ram[grid + 29] == 13 && ram[grid + 31] == 44 && ram[grid + 32] == mods::select_random_id,
            "added cells are not before Random");
    const uint64_t open = 0x3fffffffull | mods::select_extra_mask() | mods::select_random_bit();
    require(open == 0x1ffffffffull, "the added cells are not open");
    build_cells(33, open, 31);
    build_cells(33, open & ~(1ull << 30), 30);

    // Portraits: sprite 36 + cell, with added cells borrowing one.
    restore.replace(0x1a9210u, [](uint8_t *, R5900Context *c, PS2Runtime *) {
        c->r[2] = _mm_cvtsi32_si128(int32_t(packet));
        c->pc = getRegU32(c, 31);
    });
    restore.replace(0x1a91c0u, [](uint8_t *, R5900Context *c, PS2Runtime *) { c->pc = getRegU32(c, 31); });
    memory.write32(state + 76, sheet);
    memory.write32(sheet + 12, sprites);
    for (uint32_t k = 0; k < 100; ++k) {
        memory.write32(sprites + k * 64 + 60, sprite_data + k * 16);
        memory.write32(sprite_data + k * 16 + 4, k);
    }
    const auto portrait = [&](uint32_t cell) {
        const uint32_t at = state + 208 + 48 * cell;
        ram[at + 33] = uint8_t(cell);
        std::memset(ram + packet, 0, 128);
        run(0x2a9e10u, state, at);
        return uint32_t(ram[packet + 24] | ram[packet + 25] << 8);
    };
    require(portrait(3) == 39 && portrait(32) == 36 + 29, "an original cell or Random lost its portrait");
    require(portrait(29) == 36 + 4 && portrait(31) == 36 + 7, "an added cell did not borrow its portrait");
    mods::reset_select_screen();
    require(portrait(29) == 36 + 29 && portrait(3) == 39, "the unmodded wheel changed its portraits");
}
} // namespace openkai2
