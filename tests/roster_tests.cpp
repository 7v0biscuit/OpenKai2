// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "mods/afs.h"
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include "mods/roster.h"
#include "mods/select_alternates.h"
#include "mods/select_screen.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

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
    // Every record has a name, as in the game; a few are set below.
    for (uint32_t id = 0; id < roster_count; ++id) {
        const auto name = "VEGETA " + std::to_string(id);
        std::memcpy(character(memory, id), name.data(), name.size());
    }
    std::memcpy(memory.data() + 0x3A40C0u, "BASLUS-20779DBZ2", 16); // the memory card folder
    std::memset(character(memory, 0), 0, roster_name_bytes);
    std::memcpy(character(memory, 0), "GOKOU", 5);
    for (const auto &[id, name] : {std::pair<uint32_t, const char *>{4, "KULILIN"}, {13, "ZARBON"},
                                   {roster_count - 1, "BUU (TEN + YAM)"}}) {
        std::memset(character(memory, id), 0, roster_name_bytes);
        std::memcpy(character(memory, id), name, std::strlen(name));
    }
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

// An ISO whose executable is the memory from `start` as one ELF segment,
// with a DATA_CMN.AFS of 300 entries beside it: entry 0 is a small #AMB and
// `files` gives others; the rest are empty.
Bytes disc_image(const Bytes &memory, const std::string &executable = "SLUS_207.79", uint32_t start = roster_records,
                 const std::map<uint32_t, Bytes> &files = {}) {
    const uint32_t bytes = uint32_t(memory.size() - start);
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

    // The archive: its entry table, entry 0 at 3072, then the given files.
    const uint32_t entries = 300;
    Bytes afs(2 * sector, 0);
    std::memcpy(afs.data(), "AFS\0", 4);
    put32(afs, 4, entries);
    for (uint32_t i = 0; i < entries; ++i)
        put32(afs, 8 + i * 8, 2 * sector);
    const size_t pack = 3072;
    put32(afs, 8, 3072);
    put32(afs, 12, 64);
    std::memcpy(afs.data() + pack, "#AMB ", 5);
    put32(afs, pack + 16, 1);
    put32(afs, pack + 20, 0x20);
    put32(afs, pack + 0x20, 0x30);
    put32(afs, pack + 0x24, 16);
    std::memcpy(afs.data() + pack + 0x30, "#AMO", 4);
    for (const auto &[entry, data] : files) {
        put32(afs, 8 + entry * 8, uint32_t(afs.size()));
        put32(afs, 12 + entry * 8, uint32_t(data.size()));
        afs.insert(afs.end(), data.begin(), data.end());
        afs.resize((afs.size() + sector - 1) / sector * sector, 0);
    }

    const uint32_t elfSector = 40, elfSectors = uint32_t((elf.size() + sector - 1) / sector);
    const uint32_t afsSector = elfSector + elfSectors, afsSectors = uint32_t(afs.size() / sector);
    Bytes image((afsSector + afsSectors + 1) * sector, 0);
    const auto base = fixture();
    std::copy(base.begin(), base.begin() + 18 * sector, image.begin());
    both32(image, 16 * sector + 80, afsSector + afsSectors + 1);
    records(image, 18, {record(std::string(1, '\0'), 18, sector, 2), record(std::string(1, '\1'), 18, sector, 2),
                        record(executable + ";1", elfSector, uint32_t(elf.size())), record("SYSTEM.CNF;1", 22, 64),
                        record("USR", 20, sector, 2)});
    const std::string boot = "BOOT2 = cdrom0:\\" + executable + ";1\r\nVER = 1.00\r\n";
    std::copy(boot.begin(), boot.end(), image.begin() + 22 * sector);
    records(image, 20, {record(std::string(1, '\0'), 20, sector, 2), record(std::string(1, '\1'), 18, sector, 2),
                        record("DATA_CMN.AFS;1", afsSector, uint32_t(afs.size()))});
    std::copy(elf.begin(), elf.end(), image.begin() + elfSector * sector);
    std::copy(afs.begin(), afs.end(), image.begin() + afsSector * sector);
    return image;
}

// A costume model: an #AMB of a mesh whose parts link in a chain and its
// textures. `twist` relinks the last part.
Bytes costume_model(uint32_t parts, uint32_t textures, bool twist = false) {
    const uint32_t mesh = 0x40, rows = 0x70, end = mesh + rows + parts * 32;
    Bytes model(end + 0x20, 0);
    std::memcpy(model.data(), "#AMB ", 5);
    put32(model, 12, 3);
    put32(model, 16, 2);
    put32(model, 20, 0x20);
    put32(model, 0x20, mesh);
    put32(model, 0x24, end - mesh);
    put32(model, 0x28, 1);
    put32(model, 0x30, end);
    put32(model, 0x34, 0x20);
    put32(model, 0x38, 2);
    std::memcpy(model.data() + mesh, "#AMO0", 5);
    put32(model, mesh + 16, parts);
    put32(model, mesh + 20, rows);
    for (uint32_t i = 0; i < parts; ++i) {
        const uint32_t at = mesh + rows + i * 32;
        put32(model, at, i);
        put32(model, at + 4, 0x1000 + i * 0x130); // where the part's data is; free to differ
        put32(model, at + 8, i + 1 < parts ? rows + (i + 1) * 32 : 0);
        put32(model, at + 16, i ? rows + (i - 1) * 32 : 0);
    }
    if (twist)
        put32(model, mesh + rows + (parts - 1) * 32 + 16, rows);
    std::memcpy(model.data() + end, "#AMT ", 5);
    put32(model, end + 12, textures);
    return model;
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

        // The disc is identified by its memory card folder, and its table found.
        const auto identity = identify_disc(source);
        require(identity.game == DragonBallGame::budokai2 && identity.serial == "SLUS-20779" &&
                    identity.executable == "SLUS_207.79",
                "A Budokai 2 disc was not identified");
        const auto report = disc_report(source);
        if (std::getenv("OPENKAI2_SHOW_REPORT"))
            std::cerr << report;
        require(report.find("Game: Dragon Ball Z: Budokai 2") != std::string::npos &&
                    report.find("At 0x00389D40, 712 bytes apart, 53 names") != std::string::npos &&
                    report.find("USR/DATA_CMN.AFS") != std::string::npos &&
                    report.find("         0  00000030 00000010 00000000 00000000  23414d4f") != std::string::npos &&
                    report.find("#AMO version 0, USR/DATA_CMN.AFS entry 0 file 0:\n    +000 4F4D4123") != std::string::npos,
                "The disc report is missing the table or files");
        // Budokai 3 keeps its names in a list and its records point at them.
        auto third = memory_image();
        std::memset(third.data() + roster_records, 0, roster_count * roster_record_bytes);
        std::memcpy(third.data() + 0x3A40C0u, "BASLUS-21234DBZ3", 16);
        const char *thirdNames[] = {"GOKOU", "KULILIN", "PICCOLO", "VEGETA", "GOHAN", "TRUNKS", "CELL", "FREEZA",
                                    "OMEGA SHENRON", "BROLY"};
        const uint32_t list = roster_records + 0x100, records = roster_records + 0x2000;
        for (uint32_t i = 0; i < 10; ++i) {
            std::memcpy(third.data() + list + 32 * i, thirdNames[i], std::strlen(thirdNames[i]));
            put32(third, records + 0x1a0 * i, list + 32 * i);
            put32(third, records + 0x1a0 * i + 0x40, 100 + i);
            // and a pointer to a list of the character's files
            put32(third, records + 0x1a0 * i + 8, roster_records + 0x4000 + 32 * i);
            put32(third, roster_records + 0x4000 + 32 * i, 500 + i);
        }
        put32(third, records + 0x1a0 * 10 + 4, 7); // a record without a name ends the list
        write(root / "b3.iso", disc_image(third, "SLUS_212.34"));
        SourceDisc b3(root / "b3.iso");
        require(identify_disc(b3).game == DragonBallGame::budokai3 && !read_disc_roster(b3),
                "A Budokai 3 disc was not identified, or was read with Budokai 2's layout");
        const auto thirdReport = disc_report(b3);
        if (std::getenv("OPENKAI2_SHOW_REPORT"))
            std::cerr << thirdReport;
        char pointerTable[96];
        std::snprintf(pointerTable, sizeof pointerTable, "At 0x%08X, 416 bytes apart, 10 names, through pointers",
                      records);
        require(thirdReport.find(pointerTable) != std::string::npos &&
                    thirdReport.find("+0x040: 100 101 102") != std::string::npos &&
                    thirdReport.find("1 pointers  OMEGA SHENRON") != std::string::npos,
                "The disc report did not follow pointers to a name list");
        char span[64], files[64];
        std::snprintf(span, sizeof span, "Records from 0x%08X, 416 bytes each:", records);
        std::snprintf(files, sizeof files, "0x%08X 000001F5", roster_records + 0x4020);
        require(thirdReport.find(span) != std::string::npos && thirdReport.find(files) != std::string::npos &&
                    thirdReport.find("  -                  00000000 00000007") != std::string::npos &&
                    thirdReport.find("  0x00389E40  GOKOU") != std::string::npos,
                "The disc report did not show the records, the data they point at or the name list");
        auto plainGame = memory_image();
        std::memset(plainGame.data() + roster_records, 0, roster_count * roster_record_bytes);
        std::memset(plainGame.data() + 0x3A40C0u, 0, 16);
        write(root / "other.iso", disc_image(plainGame, "SLUS_299.99"));
        SourceDisc unrelated(root / "other.iso");
        require(identify_disc(unrelated).game == DragonBallGame::none, "A disc without Dragon Ball data was accepted");

        // Budokai 3's models are read from its table, and one fits a Budokai 2
        // character whose model links its parts the same way.
        write(root / "game.iso", disc_image(memory_image(), "SLUS_207.79", roster_records,
                                            {{171, costume_model(5, 1)}, {213, costume_model(6, 1)}}));
        SourceDisc gameDisc(root / "game.iso");
        const auto gameRoster = read_disc_roster(gameDisc);
        require(gameRoster && !gameRoster->models, "The game disc's roster was not read");
        const auto *gameGoku = gameRoster->find(0);
        require(costume_fields(gameDisc, *gameGoku) == std::vector<std::pair<uint32_t, uint32_t>>{{0x74, 171}},
                "Goku's costume model was not found, or a file that is not a model was taken for one");
        Bytes b3memory(budokai3_records + budokai3_count * budokai3_record_bytes, 0);
        std::memcpy(b3memory.data() + budokai3_names, "GOKOU", 5);
        std::memcpy(b3memory.data() + budokai3_names + 91 * budokai3_name_bytes, "GOKOU SS4", 9);
        std::memcpy(b3memory.data() + budokai3_names + 0x2000, "BASLUS-20998DBZ3", 16);
        const uint32_t lists = budokai3_names + 0x1000;
        put32(b3memory, budokai3_records, budokai3_names);
        b3memory[budokai3_records + 4] = 3; // three forms of one costume
        b3memory[budokai3_records + 5] = 1;
        put32(b3memory, budokai3_records + 8, lists);
        for (const auto [k, entry] : {std::pair{0u, 264u}, {1u, 265u}, {2u, 264u}})
            put32(b3memory, lists + 12 * k, entry);
        const uint32_t ss4 = budokai3_records + 91 * budokai3_record_bytes;
        put32(b3memory, ss4, budokai3_names + 91 * budokai3_name_bytes);
        b3memory[ss4 + 4] = b3memory[ss4 + 5] = 1;
        put32(b3memory, ss4 + 8, lists + 0x100);
        put32(b3memory, lists + 0x100, 266);
        write(root / "budokai3.iso", disc_image(b3memory, "SLUS_209.98", budokai3_names,
                                                {{264, costume_model(5, 2)}, {265, costume_model(5, 2, true)}}));
        SourceDisc budokai3(root / "budokai3.iso");
        require(identify_disc(budokai3).game == DragonBallGame::budokai3, "The Budokai 3 disc was not identified");
        const auto models = read_budokai3_models(budokai3);
        require(models && models->models && models->characters.size() == 2 &&
                    models->characters[0].name == "Goku, model 1" && models->characters[1].name == "Goku, model 2" &&
                    models->characters[0].files == std::vector<std::pair<uint32_t, uint32_t>>{{0, 264}},
                "Budokai 3's models were not read, or a record without a model was listed");
        require(!read_budokai3_models(gameDisc) && !read_disc_roster(budokai3), "A disc was read with the other game's table");
        require(model_shape(budokai3, 264) == model_shape(gameDisc, 171) &&
                    !(model_shape(budokai3, 265) == model_shape(gameDisc, 171)) &&
                    !(model_shape(gameDisc, 213) == model_shape(gameDisc, 171)) && !model_shape(budokai3, 266),
                "Model shapes were compared wrongly");
        const auto converted = model_for_budokai2(budokai3, 264);
        auto expected = costume_model(5, 2);
        expected[0x40 + 0x70 + 5 * 32 + 12] = 1;
        require(converted == expected, "The model's textures were not marked as Budokai 2's, or more changed");

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
