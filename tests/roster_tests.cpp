// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "mods/afs.h"
#include "mods/roster.h"
#include "mods/select_alternates.h"
#include "mods/select_screen.h"
#include <cstring>
#include <iostream>

using namespace openkai2::test;
using namespace openkai2::mods;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}
void put32(Bytes &bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = uint8_t(value >> (8 * i));
}
uint32_t get32(const uint8_t *bytes) { return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | uint32_t(bytes[3]) << 24; }
void file(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
uint8_t *character(Bytes &memory, uint32_t id) { return memory.data() + roster_records + id * roster_record_bytes; }

// Guest memory holding the parts of the USA roster the code checks.
Bytes memory_image() {
    Bytes memory(roster_select_grid + 64, 0);
    std::memcpy(character(memory, 0), "GOKOU", 5);
    std::memcpy(character(memory, 4), "KULILIN", 7);
    std::memcpy(character(memory, 13), "ZARBON", 6);
    std::memcpy(character(memory, roster_count - 1), "BUU (TEN + YAM)", 15);
    put32(memory, roster_records + 0x74, 171);
    put32(memory, roster_records + 0x1dc, 97);
    put32(memory, roster_records + 0x1bc, 0xffffffffu);
    put32(memory, roster_records + 4 * roster_record_bytes + 0x74, 213);
    memory[roster_costumes] = 2; // Goku has two costumes; unused IDs have none
    const uint8_t grid[] = {0, 4};
    std::memcpy(memory.data() + roster_select_grid, grid, 2);
    std::memset(memory.data() + roster_select_grid + 2, 6, roster_select_cells - 2);
    memory[roster_select_grid + roster_select_cells] = 0x3f;
    std::memset(memory.data() + roster_id_cells, 0xff, 64);
    memory[roster_id_cells + 0] = 0;
    memory[roster_id_cells + 4] = 1;
    memory[roster_id_cells + 0x3f] = 29;
    return memory;
}

// An ISO whose executable is that memory as one ELF segment, with a
// DATA_CMN.AFS of 300 empty entries beside it.
Bytes disc_image(const Bytes &memory) {
    const uint32_t start = roster_records, bytes = uint32_t(memory.size() - start);
    Bytes elf(0x100 + bytes, 0);
    std::memcpy(elf.data(), "\x7f" "ELF", 4);
    put32(elf, 28, 52);
    elf[42] = 32;
    elf[44] = 1;
    put32(elf, 52, 1);
    put32(elf, 56, 0x100);
    put32(elf, 60, start);
    put32(elf, 68, bytes);
    put32(elf, 72, bytes);
    std::memcpy(elf.data() + 0x100, memory.data() + start, bytes);

    const uint32_t entries = 300, elfSector = 40, elfSectors = uint32_t((elf.size() + sector - 1) / sector);
    const uint32_t afsSector = elfSector + elfSectors, afsBytes = uint32_t(2 * sector);
    Bytes image((afsSector + 3) * sector, 0);
    const auto base = fixture();
    std::copy(base.begin(), base.begin() + 18 * sector, image.begin());
    both32(image, 16 * sector + 80, afsSector + 3);
    records(image, 18, {record(std::string(1, '\0'), 18, sector, 2), record(std::string(1, '\1'), 18, sector, 2),
                        record("SLUS_207.79;1", elfSector, uint32_t(elf.size())), record("USR", 20, sector, 2)});
    records(image, 20, {record(std::string(1, '\0'), 20, sector, 2), record(std::string(1, '\1'), 18, sector, 2),
                        record("DATA_CMN.AFS;1", afsSector, afsBytes)});
    std::copy(elf.begin(), elf.end(), image.begin() + elfSector * sector);
    const size_t afs = afsSector * sector;
    std::memcpy(image.data() + afs, "AFS\0", 4);
    put32(image, afs + 4, entries);
    for (uint32_t i = 0; i < entries; ++i)
        put32(image, afs + 8 + i * 8, 2 * sector);
    return image;
}
} // namespace

int main() {
    try {
        const auto root = fs::temp_directory_path() / "openkai2-roster-tests";
        fs::remove_all(root);
        fs::create_directories(root);

        // The import tool reads characters, their files and the grid from a disc.
        const auto iso = root / "budokai2.iso";
        write(iso, disc_image(memory_image()));
        SourceDisc source(iso);
        const auto roster = read_disc_roster(source);
        require(roster && roster->characters.size() == 2 && roster->characters[0].name == "Goku" &&
                    roster->characters[1].name == "Krillin" && roster->grid[1] == 4,
                "The disc roster was not read");
        const auto &goku = roster->characters[0].files;
        require(goku.size() == 2 && goku[0] == std::pair<uint32_t, uint32_t>{0x74, 171} &&
                    goku[1] == std::pair<uint32_t, uint32_t>{0x1dc, 97},
                "A character's files were not found, or -1 and out-of-range values were kept");

        // A mod fills Zarbon's unused record from Goku's and puts it on the grid.
        const auto mod = root / "mods" / "SSJ4";
        fs::create_directories(mod);
        RosterCharacter added;
        added.slot = *free_roster_slot({});
        added.base = 0;
        added.name = "SSJ4 GOKU";
        added.select_cell = 1;
        append_roster(mod, added, {{0x74, "add:ssj4.bin"}, {0x1dc, "412"}});
        require(added.slot == 1, "The first unused slot is not ID 1");
        DiscOverlay::Summary overlay;
        overlay.added["USR/DATA_CMN.AFS"]["ssj4.bin"] = 300;
        auto characters = load_roster({mod}, overlay);
        require(characters.size() == 1 && characters[0].fields.size() == 2 && characters[0].fields[0].value == 300,
                "add: was not resolved to the added entry");
        require(free_roster_slot(roster_slots_in_use({mod})) == 13u, "A filled slot was offered again");

        auto memory = memory_image();
        const auto log = apply_roster(memory.data(), memory.size(), characters);
        const auto *slot = character(memory, 1);
        require(log.size() == 1 && std::string(reinterpret_cast<const char *>(slot)) == "SSJ4 GOKU" &&
                    get32(slot + 0x74) == 300 && get32(slot + 0x1dc) == 412 && get32(slot + 0x1bc) == 0xffffffffu &&
                    memory[roster_select_grid + 1] == 1 && memory[roster_select_grid] == 0,
                "The roster edit was not applied");
        require(memory[roster_costumes + 1] == 2, "A filled slot kept a costume count of 0");

        // A shared spot: R2 on spot 2 switches Krillin to Zarbon's filled ID and back.
        file(root / "share" / "roster.ini", "[character]\nslot = 13\nbase = 4\nname = KRILLIN 2\nalternate = 1\n");
        auto shared = load_roster({root / "share"}, overlay);
        Bytes ram(0x400000, 0);
        auto game = memory_image();
        std::copy(game.begin(), game.end(), ram.begin());
        apply_roster(ram.data(), ram.size(), shared);
        const auto alternates = build_alternates(ram.data(), ram.size(), shared);
        require(!alternates.empty() && alternates.spots[1] == std::vector<uint8_t>{4, 13} &&
                    alternates.spots[0] == std::vector<uint8_t>{0},
                "Shared spots were not built");
        const uint32_t state = 0x200000;
        put32(ram, select_state_pointers, 0x80000000u | state); // a kernel-segment pointer
        ram[state + 88] = 0;
        put32(ram, state + 192, 1);
        ram[state + 128] = 4;
        ram[state + 129] = 1;
        require(!cycle_alternate(ram.data(), ram.size(), 0, 0x0100, alternates), "L2 switched a character");
        require(!cycle_alternate(ram.data(), ram.size(), 1, alternate_button, alternates),
                "Player 2 switched without a select screen");
        const auto line = cycle_alternate(ram.data(), ram.size(), 0, alternate_button, alternates);
        require(line && ram[roster_select_grid + 1] == 13 && ram[state + 128] == 13 && ram[state + 129] == 0,
                "R2 did not switch the shared spot");
        require(cycle_alternate(ram.data(), ram.size(), 0, alternate_button, alternates) &&
                    ram[roster_select_grid + 1] == 4 && ram[state + 128] == 4,
                "Switching did not wrap back to the original");
        ram[state + 88] = 1;
        require(!cycle_alternate(ram.data(), ram.size(), 0, alternate_button, alternates),
                "A state block for another player was written");
        ram[state + 88] = 0;
        ram[state + 128] = 6;
        require(!cycle_alternate(ram.data(), ram.size(), 0, alternate_button, alternates),
                "A state that disagrees with the grid was written");

        // Without new spots the select screen keeps its original numbers.
        auto plain = memory_image();
        require(configure_select_screen(plain.data(), plain.size(), characters).empty() && select_cells() == 30 &&
                    select_last() == 29 && select_offset(1600) == 1600 && select_offset(1648) == 1648 &&
                    select_offset(1652) == 1652 && select_offset(1664) == 1664 && select_icon(29) == 29 &&
                    select_icon(5) == 5 && select_random_bit() == (uint64_t(1) << 29) && select_extra_mask() == 0 &&
                    preview_id(44) == 36 && preview_row(44) == 44 && preview_row(3) == 3,
                "The unmodded select screen changed");

        // Two new spots go before Random, which moves to the end.
        file(root / "wheel" / "roster.ini", "[character]\nslot = 13\nbase = 4\nnew_cell = yes\n"
                                            "[character]\nslot = 44\nbase = 0\nnew_cell = yes\nicon = 6\n");
        auto wheel = load_roster({root / "wheel"}, overlay);
        auto spots = memory_image();
        apply_roster(spots.data(), spots.size(), wheel);
        const auto lines = configure_select_screen(spots.data(), spots.size(), wheel);
        const uint8_t *grid = spots.data() + roster_select_grid, *cells = spots.data() + roster_id_cells;
        require(lines.size() == 3 && select_cells() == 32 && grid[29] == 13 && grid[30] == 44 && grid[31] == 0x3f &&
                    cells[13] == 29 && cells[44] == 30 && cells[0x3f] == 31,
                "New spots were not added before Random");
        require(select_offset(1600) == 208 + 48 * 31 && select_offset(1648) == 208 + 48 * 32 &&
                    select_offset(1652) == 212 + 48 * 32 && select_offset(1664) == 224 + 48 * 32,
                "The fields after the cells did not move");
        require(select_icon(29) == 1 && select_icon(30) == 6 && select_icon(31) == 29 && select_icon(28) == 28,
                "New spots do not borrow the right portraits");
        require(select_random_bit() == (uint64_t(1) << 31) && select_extra_mask() == ((uint64_t(1) << 29) | (uint64_t(1) << 30)),
                "Unlock bits do not cover the new spots");
        require(preview_id(44) == 0 && preview_row(44) == 0 && preview_id(45) == 36 && preview_row(13) == 13,
                "A filled ID above 36 does not preview as its base");
        reset_select_screen();

        // Bad input names the line; other executables are never patched.
        file(root / "bad" / "roster.ini", "[character]\nslot = 13\nselect = 29\n");
        bool rejected = false;
        try {
            load_roster({root / "bad"}, overlay);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("roster.ini:3") != std::string::npos;
        }
        require(rejected, "The Random cell was accepted as a select cell");
        file(root / "missing" / "roster.ini", "[character]\nslot = 13\nfile.74 = add:nothing.bin\n");
        rejected = false;
        try {
            load_roster({root / "missing"}, overlay);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("no enabled mod adds") != std::string::npos;
        }
        require(rejected, "A missing added entry was accepted");
        Bytes other(memory.size(), 0);
        rejected = false;
        try {
            apply_roster(other.data(), other.size(), characters);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "A roster was written into an unknown executable");
        fs::remove_all(root);
        std::cout << "Roster passed: disc roster, character files, unused slots, roster.ini, add: entries, "
                     "select cells, shared spots, added spots and executable checks.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
