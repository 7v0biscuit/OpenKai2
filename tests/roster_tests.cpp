// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "mods/afs.h"
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include "mods/roster.h"
#include "mods/select_alternates.h"
#include "mods/select_screen.h"
#include "mods/stages.h"
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

Bytes memory_image() {
    Bytes memory(roster_select_grid + 64, 0);
    for (uint32_t id = 0; id < roster_count; ++id) {
        const auto name = "VEGETA " + std::to_string(id);
        std::memcpy(character(memory, id), name.data(), name.size());
    }
    std::memcpy(memory.data() + 0x3A40C0u, "BASLUS-20779DBZ2", 16);
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
    memory[roster_costumes] = 2;
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

using Layout = std::vector<std::pair<uint32_t, uint32_t>>;
const Layout plainLayout{{0x14, 0x00080008}};

Bytes costume_model(uint32_t parts, uint32_t textures, bool twist = false, const Layout &layout = plainLayout) {
    const uint32_t mesh = 0x40, rows = 0x70, end = mesh + rows + parts * 32;
    const uint32_t count = uint32_t(layout.size()), table = (count * 4 + 15) / 16 * 16, pack = 0x20 + table + count * 48;
    Bytes model(end + pack, 0);
    std::memcpy(model.data(), "#AMB ", 5);
    put32(model, 12, 3);
    put32(model, 16, 2);
    put32(model, 20, 0x20);
    put32(model, 0x20, mesh);
    put32(model, 0x24, end - mesh);
    put32(model, 0x28, 1);
    put32(model, 0x30, end);
    put32(model, 0x34, pack);
    put32(model, 0x38, 2);
    std::memcpy(model.data() + mesh, "#AMO0", 5);
    put32(model, mesh + 16, parts);
    put32(model, mesh + 20, rows);
    for (uint32_t i = 0; i < parts; ++i) {
        const uint32_t at = mesh + rows + i * 32;
        put32(model, at, i);
        put32(model, at + 4, 0x1000 + i * 0x130);
        put32(model, at + 8, i + 1 < parts ? rows + (i + 1) * 32 : 0);
        put32(model, at + 16, i ? rows + (i - 1) * 32 : 0);
    }
    if (twist)
        put32(model, mesh + rows + (parts - 1) * 32 + 16, rows);
    std::memcpy(model.data() + end, "#AMT ", 5);
    put32(model, end + 12, textures);
    put32(model, end + 16, count);
    put32(model, end + 20, 0x20);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t header = 0x20 + table + i * 48;
        put32(model, end + 0x20 + i * 4, header);
        put32(model, end + header, i);
        put32(model, end + header + 8, layout[i].first);
        put32(model, end + header + 12, layout[i].second);
    }
    return model;
}
}

int main() {
    try {
        const auto root = fs::temp_directory_path() / "openkai2-roster-tests";
        fs::remove_all(root);
        fs::create_directories(root);
        {


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
            put32(third, records + 0x1a0 * i + 8, roster_records + 0x4000 + 32 * i);
            put32(third, roster_records + 0x4000 + 32 * i, 500 + i);
        }
        put32(third, records + 0x1a0 * 10 + 4, 7);
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

        const Layout gokuLayout{{0x14, 0x00080008}, {0x13, 0x00060006}};
        const Layout budokai3Layout{{0x14, 0x00080008}, {0x13, 0x00060006}, {0x14, 0x00070007}};
        write(root / "game.iso", disc_image(memory_image(), "SLUS_207.79", roster_records,
                                            {{171, costume_model(5, 1, false, gokuLayout)},
                                             {213, costume_model(6, 1, false, {{0x13, 0x00060006}})}}));
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
        b3memory[budokai3_records + 4] = 3;
        b3memory[budokai3_records + 5] = 1;
        put32(b3memory, budokai3_records + 8, lists);
        for (const auto [k, entry] : {std::pair{0u, 264u}, {1u, 265u}, {2u, 264u}})
            put32(b3memory, lists + 12 * k, entry);
        const uint32_t ss4 = budokai3_records + 91 * budokai3_record_bytes;
        put32(b3memory, ss4, budokai3_names + 91 * budokai3_name_bytes);
        b3memory[ss4 + 4] = b3memory[ss4 + 5] = 1;
        put32(b3memory, ss4 + 8, lists + 0x100);
        put32(b3memory, lists + 0x100, 266);
        auto materialModel = costume_model(5, 2, false, budokai3Layout);
        put32(materialModel, 0x40 + 32, 1);
        put32(materialModel, 0x40 + 48, 0x50);
        for (const auto &[at, flags] : {std::pair{0x50u, 0x1B5u}, {0x60u, 0x3A7u}}) {
            put32(materialModel, 0x40 + at, flags);
            put32(materialModel, 0x40 + at + 4, 0x29BD);
            put32(materialModel, 0x40 + at + 8, 3);
        }
        put32(materialModel, 0x40 + 0x70, 0x9000020C);
        put32(materialModel, 0x40 + 0x74, 0x50);
        put32(materialModel, 0x40 + 0x80, 0x6000020F);
        put32(materialModel, 0x40 + 0x84, 0x54);
        write(root / "budokai3.iso", disc_image(b3memory, "SLUS_209.98", budokai3_names,
                                                {{264, costume_model(5, 2, false, budokai3Layout)},
                                                 {267, materialModel},
                                                 {265, costume_model(5, 2, true, {{0x14, 0x00070007}})}}));
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
        auto wearers = *gameRoster;
        read_model_shapes(gameDisc, wearers);
        require(wearers.shapes.size() == 2 && wearers.shapes.at(0).parts.size() == 5 &&
                    fitting_wearers(*models, models->characters[0].id, wearers) == std::vector<uint32_t>{0, 4} &&
                    fitting_wearers(*models, models->characters[1].id, wearers).empty() &&
                    textures_fit(*model_shape(budokai3, 264), *model_shape(gameDisc, 171)) &&
                    !textures_fit(*model_shape(gameDisc, 171), *model_shape(budokai3, 264)) &&
                    !textures_fit(*model_shape(budokai3, 265), *model_shape(gameDisc, 171)),
                "The characters that can wear a model were not found");
        const auto shapesReport = disc_report(budokai3);
        require(shapesReport.find("Costume model shapes") != std::string::npos &&
                    shapesReport.find("Goku, model 1") != std::string::npos &&
                    shapesReport.find("Goku, model 1, entry 264:\n    header: 4F4D4123") != std::string::npos &&
                    shapesReport.find("    part  4: 00000004") != std::string::npos,
                "The disc report does not list the model shapes");
        const auto gameReport = disc_report(gameDisc);
        require(gameReport.find("Krillin, entry 213:\n    textures: 544D4123") != std::string::npos &&
                    gameReport.find("Check values of Krillin's") != std::string::npos &&
                    gameReport.find("Krillin, entry 213:\n    mesh 304 bytes, check") != std::string::npos &&
                    gameReport.find("texture pack ends: 544D4123") != std::string::npos,
                "The disc report does not show Krillin's textures and check values");
        const auto converted = model_for_budokai2(budokai3, 264);
        auto expected = costume_model(5, 2, false, budokai3Layout);
        expected[0x40 + 0x70 + 5 * 32 + 12] = 1;
        require(converted == expected, "The model's textures were not marked as Budokai 2's, or more changed");

        Bytes stageMemory(stage_records + stage_count * stage_record_bytes, 0);
        const char *stageNames[] = {"ISLAND", "TOWN", "ROOM TIME MIND", "BUDO", "R&R BASE", "SNOW", "GARDEN",
                                    "NAMEC", "KAIOH SHIN", "ISLAND B", "TOWN B", "R&R BASE B", "SNOW B"};
        for (uint32_t i = 0; i < stage_count; ++i) {
            const uint32_t record = stage_records + i * stage_record_bytes;
            std::memcpy(stageMemory.data() + record, stageNames[i], std::strlen(stageNames[i]));
            for (uint32_t k = 0; k < 8; ++k)
                put32(stageMemory, record + stage_files_begin + 4 * k, 2 + (i % 9) * 8 + k);
            put32(stageMemory, record + stage_variant, i >= 9 ? 1 : 0);
        }
        write(root / "stages.iso", disc_image(stageMemory, "SLUS_207.79", stage_records));
        SourceDisc stageDisc(root / "stages.iso");
        const auto stages = read_disc_stages(stageDisc);
        require(stages && stages->size() == 13 && (*stages)[2].internal == "ROOM TIME MIND" && !(*stages)[2].variant &&
                    (*stages)[9].variant && (*stages)[0].files.size() == 8 &&
                    (*stages)[0].files[3] == std::pair<uint32_t, uint32_t>{0x4C, 5} &&
                    !read_disc_stages(budokai3),
                "The stage table was not read");
        Bytes b3stages = b3memory;
        b3stages.resize(budokai3_stages + budokai3_stage_count * budokai3_stage_bytes);
        for (uint32_t i = 0; i < budokai3_stage_count; ++i) {
            const uint32_t record = budokai3_stages + i * budokai3_stage_bytes;
            const auto name = i == 0 ? std::string("BUDO") : i == 1   ? std::string("MIND & TIME B")
                                  : i == 6   ? std::string("GOKU")
                                             : "STAGE" + std::to_string(i);
            std::memcpy(b3stages.data() + record, name.data(), name.size());
            put32(b3stages, record + 0x10, 100 + 2 * i);
            put32(b3stages, record + 0x14, i == 2 ? 0 : 101 + 2 * i);
        }
        const auto amb = [](const std::vector<Bytes> &files) {
            Bytes pack(0x20 + 16 * files.size(), 0);
            std::memcpy(pack.data(), "#AMB", 4);
            put32(pack, 16, uint32_t(files.size()));
            put32(pack, 20, 0x20);
            for (size_t i = 0; i < files.size(); ++i) {
                pack.resize((pack.size() + 15) / 16 * 16, 0);
                put32(pack, 0x20 + 16 * i, uint32_t(pack.size()));
                put32(pack, 0x24 + 16 * i, uint32_t(files[i].size()));
                pack.insert(pack.end(), files[i].begin(), files[i].end());
            }
            return pack;
        };
        const auto marked = [](const char *mark, uint8_t fill) {
            Bytes file(32, fill);
            std::memcpy(file.data(), mark, 4);
            return file;
        };
        Bytes stageTextures = marked("#AMT", 0);
        put32(stageTextures, 12, 2);
        const auto stageAmb = amb({stageTextures});
        const auto firstPack = amb({marked("#MDD", 1), marked("#MAD", 2), marked("#MAS", 3), marked("#SPX", 4),
                                    amb({marked("#AME", 9), stageTextures, marked("#AME", 10)})});
        const auto secondPack = amb({Bytes(20, 6), marked("IECS", 7), marked("IECS", 8)});
        write(root / "b3stages.iso", disc_image(b3stages, "SLUS_209.98", budokai3_names, {{100, firstPack}, {101, secondPack}}));
        SourceDisc b3stageDisc(root / "b3stages.iso");
        const auto b3list = read_disc_stages(b3stageDisc);
        require(b3list && b3list->size() == budokai3_stage_count && (*b3list)[0].internal == "BUDO" &&
                    (*b3list)[1].variant && (*b3list)[0].files == std::vector<std::pair<uint32_t, uint32_t>>{{0x10, 100}, {0x14, 101}} &&
                    (*b3list)[2].files.size() == 1,
                "Budokai 3's stage table was not read");
        require(stage_display_name("ROOM TIME MIND") == "Hyperbolic Time Chamber" &&
                    stage_display_name("MIND & TIME B") == "Hyperbolic Time Chamber B" &&
                    stage_display_name("TOWN B") == "Town B" && stage_display_name("R&R BASE") == "R&R Base",
                "Stage names were not shown as expected");
        const auto stageFiles = budokai3_stage_files(b3stageDisc, (*b3list)[0]);
        std::vector<uint32_t> offsets;
        for (const auto &file : stageFiles)
            offsets.push_back(file.first);
        require(offsets == std::vector<uint32_t>{0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C} &&
                    stageFiles[0].second[4] == 1 && stageFiles[3].second.size() == 0x70 && get32(stageFiles[3].second.data() + 16) == 1 &&
                    get32(stageFiles[3].second.data() + 0x24) == 32 && unloadable_pack_files(stageFiles[3].second).empty() &&
                    std::memcmp(stageFiles[3].second.data(), "#AMB", 4) == 0 && stageFiles[4].second[4] == 4 &&
                    get32(stageFiles[3].second.data() + get32(stageFiles[3].second.data() + 0x20) + 12) == 1 &&
                    stageFiles[5].second == Bytes(20, 6) && stageFiles[7].second[4] == 8,
                "A Budokai 3 stage was not unpacked into Budokai 2's files");
        require(unloadable_pack_files(stageAmb).empty() && !budokai3_stage_problem(b3stageDisc, (*b3list)[0]) &&
                    unloadable_pack_files(amb({stageTextures, marked("#AMX", 0)})) == std::vector<std::string>{"#AMX"} &&
                    budokai3_stage_problem(b3stageDisc, (*b3list)[2]),
                "Stage files Budokai 2 can't load were not found, or a loadable stage was refused");
        {
            auto pack = amb({marked("#AME", 1), marked("#AMG", 2), Bytes(20, 3), marked("#AMO", 4)});
            put32(pack, 0x3C, 0x1234);
            put32(pack, 8, uint32_t(pack.size()));
            const auto kept = without_unloadable_files(pack);
            const auto files = [&](const Bytes &bytes) {
                std::vector<std::string> marks;
                for (uint32_t i = 0; i < get32(bytes.data() + 16); ++i) {
                    const uint32_t at = get32(bytes.data() + 0x20 + 16 * i);
                    require(at % 16 == 0 && at + get32(bytes.data() + 0x24 + 16 * i) <= bytes.size(), "A kept file ran past its pack");
                    marks.emplace_back(reinterpret_cast<const char *>(bytes.data() + at), 4);
                }
                return marks;
            };
            require(files(kept) == std::vector<std::string>{"#AMG", "#AMO"} && get32(kept.data() + 8) == kept.size() && get32(kept.data() + 0x2C) == 0x1234 &&
                        kept.size() < pack.size() && without_unloadable_files(stageAmb) == stageAmb &&
                        without_unloadable_files(Bytes(20, 6)) == Bytes(20, 6),
                    "Files Budokai 2 can't load were not dropped from a pack, or the rest moved wrongly");
            const auto shorter = without_unloadable_files(amb({marked("#AMG", 2), marked("#AME", 1)}));
            require(get32(shorter.data() + 0x30) == 0 && get32(shorter.data() + 0x34) == 0 && shorter.size() == 0x60,
                    "A dropped file's record was left behind");
        }
        bool unpackRefused = false;
        try {
            budokai3_stage_files(b3stageDisc, (*b3list)[2]);
        } catch (const std::runtime_error &) {
            unpackRefused = true;
        }
        require(unpackRefused, "A stage without two packs was unpacked");

        {
            const auto stageMod = root / "mods" / "Stage";
            const auto added_dir = stageMod / "afs" / "USR" / "DATA_CMN.AFS" / "add";
            fs::create_directories(added_dir);
            std::vector<std::pair<uint32_t, std::string>> fields;
            DiscOverlay::Summary stageOverlay;
            for (uint32_t k = 0; k < 8; ++k) {
                const auto name = "budo_" + std::to_string(k) + ".bin";
                write(added_dir / name, Bytes(4, 1));
                fields.push_back({0x40 + 4 * k, "add:" + name});
                stageOverlay.added["USR/DATA_CMN.AFS"][name] = 500 + k;
            }
            write(added_dir / "budo_3.bin", amb({stageTextures, marked("#AMX", 0)}));
            append_stage(stageMod, 10, "BUDO 3", fields);
            std::vector<std::string> notes;
            require(load_stage_mods({stageMod}, stageOverlay, &notes).empty() && notes.size() == 1 &&
                        notes[0].find("#AMX") != std::string::npos,
                    "A stage with files Budokai 2 can't load was applied");
            write(added_dir / "budo_3.bin", stageAmb);
            const auto loaded = load_stage_mods({stageMod}, stageOverlay);
            auto table = stageMemory;
            const auto lines = apply_stage_mods(table.data(), table.size(), loaded);
            const uint32_t record = stage_records + 10 * stage_record_bytes;
            require(loaded.size() == 1 && lines.size() == 1 && std::string(reinterpret_cast<char *>(table.data() + record)) == "BUDO 3" &&
                        get32(table.data() + record + 0x40) == 500 && get32(table.data() + record + 0x5C) == 507 &&
                        get32(table.data() + record + stage_variant) == 0 &&
                        get32(table.data() + record - stage_record_bytes + 0x40) == get32(stageMemory.data() + record - stage_record_bytes + 0x40),
                    "A stage mod did not fill its B stage, or changed another");
            const auto sections = read_stage_sections(stageMod);
            require(sections.size() == 1 && sections[0].slot == 10 && sections[0].name == "BUDO 3" && sections[0].added.size() == 8,
                    "A mod's stages were misread");
            bool slotRefused = false;
            try {
                append_stage(stageMod, 3, "BUDO 3", fields);
                load_stage_mods({stageMod}, stageOverlay);
            } catch (const std::runtime_error &) {
                slotRefused = true;
            }
            require(slotRefused, "A stage mod replaced a stage with files of its own");
            require(remove_stage(stageMod, 3) == 0 && fs::exists(added_dir / "budo_0.bin") &&
                        remove_stage(stageMod, 10) == 8 && read_stage_sections(stageMod).empty() &&
                        !fs::exists(added_dir / "budo_0.bin"),
                    "Removing a stage removed files another still uses, or left its own");
            fs::remove_all(stageMod);
        }

        const auto flagged = model_for_budokai2(budokai3, 267);
        require(get32(flagged.data() + 0x90) == 0x21B5 && get32(flagged.data() + 0xA0) == 0x3A7 &&
                    material_records(flagged, true).size() == 2 && material_records(flagged, false).size() == 1 &&
                    model_materials(budokai3, 267).find("material at mesh +000050: 000001B5 000029BD 00000003") !=
                        std::string::npos,
                "Budokai 3 materials were not given Budokai 2's flag");
        require(get32(flagged.data() + 0xB0) == 0x9000020C && draw_records(flagged).size() == 1,
                "Budokai 3 draw records were changed or not found");

        const auto differences = mesh_differences(budokai3, 267, budokai3, 264);
        require(differences.find("00000000 -> 000001B5") != std::string::npos &&
                    differences.find("00000000 -> 000029BD") != std::string::npos &&
                    differences.find("0 of them both vertex-like floats") != std::string::npos,
                "The mesh comparison does not list the material record");

        const auto b3model = costume_model(5, 2, false, budokai3Layout), b2model = costume_model(5, 1, false, gokuLayout);
        const size_t mesh = 0x40, meshBytes = 0x70 + 5 * 32, textures = mesh + meshBytes;
        const auto piece = [](const Bytes &model, size_t from, size_t bytes) {
            return Bytes(model.begin() + from, model.begin() + from + bytes);
        };
        require(model_version(budokai3, 264, gameDisc, 171, ModelVersion::converted) == expected &&
                    model_version(budokai3, 264, gameDisc, 171, ModelVersion::unchanged) == b3model,
                "The converted or unchanged model is wrong");
        const auto withOurTextures = model_version(budokai3, 264, gameDisc, 171, ModelVersion::budokai2_textures);
        const auto withOurMesh = model_version(budokai3, 264, gameDisc, 171, ModelVersion::budokai2_mesh);
        const auto ourTextures = piece(b2model, textures, 0x20), theirTextures = piece(expected, textures, 0x20);
        require(get32(withOurTextures.data() + 16) == 2 && get32(withOurTextures.data() + 0x20) == 0x40 &&
                    piece(withOurTextures, 0x40, meshBytes) == piece(b3model, mesh, meshBytes) &&
                    piece(withOurTextures, get32(withOurTextures.data() + 0x30), 0x20) == ourTextures &&
                    get32(withOurTextures.data() + 0x34) == get32(b2model.data() + 0x34) &&
                    get32(withOurTextures.data() + 0x30) % 64 == 0,
                "The Budokai 3 mesh with Budokai 2 textures is wrong");
        const uint32_t matched = get32(withOurMesh.data() + 0x30);
        require(piece(withOurMesh, 0x40, meshBytes) == piece(b2model, mesh, meshBytes) &&
                    get32(withOurMesh.data() + matched + 12) == 1 && get32(withOurMesh.data() + matched + 16) == 2 &&
                    piece(withOurMesh, matched, 12) == piece(theirTextures, 0, 12),
                "The Budokai 2 mesh with Budokai 3 textures is wrong");
        const auto lit = model_version(budokai3, 264, gameDisc, 171, ModelVersion::budokai2_lighting);
        const auto wholeLit = model_version(budokai3, 264, gameDisc, 171, ModelVersion::converted_budokai2_lighting);
        const auto packCount = [](const Bytes &model) { return get32(model.data() + get32(model.data() + 0x30) + 16); };
        require(piece(lit, 0x40, meshBytes) == piece(b2model, mesh, meshBytes) &&
                    packCount(lit) == packCount(withOurMesh) &&
                    piece(wholeLit, 0x40, meshBytes) == piece(b3model, mesh, meshBytes) &&
                    packCount(wholeLit) == packCount(expected),
                "The versions keeping Budokai 2's shading textures are wrong");

        using Slots = std::vector<std::array<uint32_t, 2>>;
        const Slots t1{{0x14, 8}}, body{{0x14, 8}, {0x13, 6}};
        require(match_textures(Slots{{0x13, 6}, {0x14, 8}, {0x14, 7}, {0x13, 6}}, body) == std::vector<size_t>{1, 3} &&
                    match_textures(Slots{{0x13, 6}, {0x14, 8}}, body).empty() && match_textures(t1, body).empty(),
                "Textures were matched wrongly");
        const auto texturePack = [&](const Layout &layout, uint8_t fill) {
            auto model = costume_model(1, 2, false, layout);
            const uint32_t at = get32(model.data() + 0x30);
            Bytes pack(model.begin() + at, model.end());
            for (uint32_t i = 0; i < layout.size(); ++i) {
                const uint32_t header = get32(pack.data() + 0x20 + i * 4);
                put32(pack, header + 20, uint32_t(pack.size()));
                put32(pack, header + 24, 16);
                pack.insert(pack.end(), 16, uint8_t(fill + i));
                put32(pack, header + 36, uint32_t(pack.size()));
                put32(pack, header + 40, 16);
                pack.insert(pack.end(), 16, uint8_t(fill + 0x40 + i));
            }
            return pack;
        };
        const auto theirPack = texturePack({{0x13, 6}, {0x14, 8}, {0x14, 7}, {0x13, 6}}, 0x10);
        const auto rebuilt = match_pack(theirPack, texturePack({{0x14, 8}, {0x13, 6}}, 0x80));
        const uint32_t second = get32(rebuilt.data() + 0x24);
        require(get32(rebuilt.data() + 16) == 2 && get32(rebuilt.data() + 12) == 1 && get32(rebuilt.data() + second) == 1 &&
                    get32(rebuilt.data() + second + 8) == 0x13 &&
                    rebuilt[get32(rebuilt.data() + second + 20)] == 0x13 &&
                    rebuilt[get32(rebuilt.data() + second + 36)] == 0x53 && get32(rebuilt.data() + second + 20) % 16 == 0,
                "The matched texture pack is wrong");
        require(model_shape(budokai3, 264) && !model_shape(budokai3, 266), "A model's shape was misread");

        const auto sentPack = [&](const Layout &layout, uint8_t fill, int shading) {
            auto model = costume_model(1, 2, false, layout);
            const uint32_t at = get32(model.data() + 0x30);
            Bytes pack(model.begin() + at, model.end());
            pack.resize((pack.size() + 15) / 16 * 16, 0);
            for (uint32_t i = 0; i < layout.size(); ++i) {
                const uint32_t header = get32(pack.data() + 0x20 + i * 4);
                if (int(i) == shading)
                    put32(pack, header + 4, 0x80000001);
                for (const uint32_t field : {20u, 36u}) {
                    put32(pack, header + field, uint32_t(pack.size()));
                    put32(pack, header + field + 4, 32);
                    Bytes sent(32, 0);
                    put32(sent, 12, 0x50000003);
                    pack.insert(pack.end(), sent.begin(), sent.end());
                    pack.insert(pack.end(), 32, uint8_t(fill + (field == 36 ? 0x40 : 0) + i));
                }
            }
            return pack;
        };
        const auto sentTheirs = sentPack({{0x14, 8}, {0x13, 6}}, 0x10, 1);
        const auto sentOurs = sentPack({{0x14, 8}, {0x13, 6}}, 0x80, 1);
        const auto keptAll = match_pack(sentTheirs, sentOurs), keptLight = match_pack(sentTheirs, sentOurs, true);
        const auto texel = [](const Bytes &pack, uint32_t texture, uint32_t field, uint32_t at) {
            return pack[get32(pack.data() + get32(pack.data() + 0x20 + texture * 4) + field) + at];
        };
        require(texel(keptAll, 0, 20, 63) == 0x10 && texel(keptAll, 0, 36, 63) == 0x50 &&
                    get32(keptAll.data() + get32(keptAll.data() + 0x20) + 36) -
                            get32(keptAll.data() + get32(keptAll.data() + 0x20) + 20) == 64 &&
                    texel(keptAll, 1, 20, 40) == 0x11 && texel(keptLight, 1, 20, 40) == 0x81 &&
                    texel(keptLight, 1, 36, 63) == 0xC1 && texel(keptLight, 0, 20, 40) == 0x10,
                "Texture data was cut short, or the shading texture was not kept");
        const auto relit = with_budokai2_lighting(sentTheirs, sentOurs);
        require(texel(relit, 1, 20, 40) == 0x81 && texel(relit, 0, 20, 40) == 0x10,
                "The model's shading texture was not replaced by the wearer's");

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

        {
            const auto two = root / "mods" / "Two";
            const auto added_dir = two / "afs" / "USR" / "DATA_CMN.AFS" / "add";
            fs::create_directories(added_dir);
            for (const char *name : {"a.bin", "b.bin", "shared.bin"})
                write(added_dir / name, Bytes(4, 1));
            {
                std::ofstream log(two / "imports.txt");
                log << "character\tafs/USR/DATA_CMN.AFS/add/a.bin\tsrc#1\n"
                    << "character\tafs/USR/DATA_CMN.AFS/add/b.bin\tsrc#2\n";
            }
            RosterCharacter first, second;
            first.slot = 1;
            first.base = 4;
            first.name = "KULILIN A";
            first.alternate_cells = {6};
            second.slot = 13;
            second.base = 0;
            second.name = "GOKOU B";
            second.new_cell = true;
            append_roster(two, first, {{0x74, "add:a.bin"}, {0x78, "add:shared.bin"}});
            append_roster(two, second, {{0x74, "add:b.bin"}, {0x78, "add:shared.bin"}, {0x7c, "412"}});
            const auto sections = read_roster_sections(two);
            require(sections.size() == 2 && sections[0].slot == 1 && sections[0].name == "KULILIN A" &&
                        sections[0].base == 4u && sections[0].shared_cells == std::vector<uint32_t>{6} &&
                        sections[0].added == std::vector<std::string>{"a.bin", "shared.bin"} && sections[1].new_cell &&
                        sections[1].added.size() == 2,
                    "A mod's roster sections were misread");
            require(remove_roster_character(two, 1) == 1u && !fs::exists(added_dir / "a.bin") &&
                        fs::exists(added_dir / "shared.bin") && fs::exists(added_dir / "b.bin"),
                    "Removing a character removed the wrong files");
            const auto left = read_roster_sections(two);
            std::ifstream log(two / "imports.txt");
            const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
            require(left.size() == 1 && left[0].slot == 13 && text.find("a.bin") == std::string::npos &&
                        text.find("b.bin") != std::string::npos,
                    "Removing a character left its section or record, or dropped another's");
            bool refused = false;
            try {
                remove_roster_character(two, 1);
            } catch (const std::runtime_error &) {
                refused = true;
            }
            require(refused, "Removing a character the mod does not have was accepted");
            fs::remove_all(two);
        }

        auto memory = memory_image();
        for (const auto &table : roster_id_tables)
            for (uint32_t i = 0; i < table.bytes; ++i)
                memory[table.address + i] = uint8_t(0x10 + i);
        memory[roster_id_tables[0].address + roster_id_tables[0].bytes] = 0xff;
        const auto log = apply_roster(memory.data(), memory.size(), characters);
        const auto *slot = character(memory, 1);
        require(log.size() == 1 && std::string(reinterpret_cast<const char *>(slot)) == "SSJ4 GOKU" &&
                    get32(slot + 0x74) == 300 && get32(slot + 0x1dc) == 412 && get32(slot + 0x1bc) == 0xffffffffu &&
                    memory[roster_select_grid + 1] == 1 && memory[roster_select_grid] == 0,
                "The roster edit was not applied");
        require(memory[roster_costumes + 1] == 2, "A filled slot kept a costume count of 0");
        for (const auto &table : roster_id_tables)
            require(std::equal(memory.begin() + table.address, memory.begin() + table.address + table.bytes,
                               memory.begin() + table.address + table.bytes),
                    std::string("A filled slot did not take its base's ") + table.name);
        require(log[0].find("with its base's move effects, voice, voice set, name plate and hair") != std::string::npos,
                "The startup log does not say which tables were copied");

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
        put32(ram, select_state_pointers, 0x80000000u | state);
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

        auto plain = memory_image();
        require(configure_select_screen(plain.data(), plain.size(), characters).empty() && select_cells() == 30 &&
                    select_last() == 29 && select_offset(1600) == 1600 && select_offset(1648) == 1648 &&
                    select_offset(1652) == 1652 && select_offset(1664) == 1664 && select_icon(29) == 29 &&
                    select_icon(5) == 5 && select_random_bit() == (uint64_t(1) << 29) && select_extra_mask() == 0 &&
                    preview_id(44) == 36 && preview_row(44) == 44 && preview_row(3) == 3,
                "The unmodded select screen changed");

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
        require(preview_id(44) == 0 && preview_row(44) == 0 && preview_id(45) == 36 && preview_row(13) == 4 &&
                    preview_row(14) == 14,
                "A filled ID does not take its base's preview");
        require(id_table_row(44) == 0 && id_table_row(13) == 13 && id_table_row(45) == 45 &&
                    id_table_row(4) == 4,
                "A filled ID past the move-effects table does not read its base's row");
        reset_select_screen();
        require(id_table_row(44) == 44, "A reset select screen kept a move-effects row");

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
        }
        fs::remove_all(root);
        std::cout << "Roster passed: disc roster, character files, unused slots, roster.ini, add: entries, "
                     "select cells, shared spots, added spots and executable checks.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
