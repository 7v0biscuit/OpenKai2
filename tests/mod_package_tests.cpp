// SPDX-License-Identifier: GPL-3.0-only
#include "mods/mod_package.h"
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace openkai2::mods;

namespace {
const unsigned char shared_pack[] = {80,75,3,4,20,0,0,0,8,0,0,0,33,0,47,43,179,151,33,0,0,0,31,0,0,0,19,0,0,0,83,104,97,114,101,100,32,80,97,99,107,47,109,111,100,46,105,110,105,203,75,204,77,181,13,206,72,44,74,77,81,8,72,76,206,230,74,44,45,201,200,47,178,45,73,45,46,73,45,226,2,0,80,75,3,4,20,0,0,0,8,0,0,0,33,0,90,93,21,240,55,1,0,0,136,19,0,0,46,0,0,0,83,104,97,114,101,100,32,80,97,99,107,47,97,102,115,47,85,83,82,47,68,65,84,65,95,67,77,78,46,65,70,83,47,48,52,49,50,95,103,111,107,117,46,98,105,110,99,96,231,19,149,81,214,50,180,176,119,243,13,137,78,202,44,40,175,107,237,153,60,107,225,138,245,219,246,30,57,125,233,230,131,231,239,190,50,114,240,139,201,170,104,27,89,58,184,251,133,198,36,103,21,86,212,183,245,78,153,189,104,229,134,237,251,142,158,185,124,235,225,139,247,223,152,56,5,196,229,84,117,140,173,28,61,252,195,98,83,178,139,42,27,218,251,166,206,89,188,106,227,142,253,199,206,94,185,253,232,229,135,239,204,92,130,18,242,106,186,38,214,78,158,1,225,113,169,57,197,85,141,29,253,211,230,46,89,189,105,231,129,227,231,174,222,121,252,234,227,15,22,110,33,73,5,117,61,83,27,103,175,192,136,248,180,220,146,234,166,206,9,211,231,45,93,179,121,215,193,19,231,175,221,125,242,250,211,79,86,30,97,41,69,13,125,51,91,23,239,160,200,132,244,188,210,154,230,174,137,51,230,47,91,187,101,247,161,147,23,174,223,123,250,230,243,47,54,94,17,105,37,77,3,115,59,87,159,224,168,196,140,252,178,218,150,238,73,51,23,44,95,183,117,207,225,83,23,111,220,127,246,246,11,195,168,215,71,189,62,234,245,81,175,143,122,125,212,235,163,94,31,245,250,168,215,71,189,62,84,188,238,232,228,236,50,138,71,241,40,30,197,163,120,20,143,226,161,141,1,80,75,3,4,20,0,0,0,0,0,0,0,33,0,68,148,43,18,22,0,0,0,22,0,0,0,22,0,0,0,83,104,97,114,101,100,32,80,97,99,107,47,114,111,115,116,101,114,46,105,110,105,91,99,104,97,114,97,99,116,101,114,93,10,115,108,111,116,32,61,32,49,51,10,80,75,1,2,20,3,20,0,0,0,8,0,0,0,33,0,47,43,179,151,33,0,0,0,31,0,0,0,19,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,83,104,97,114,101,100,32,80,97,99,107,47,109,111,100,46,105,110,105,80,75,1,2,20,3,20,0,0,0,8,0,0,0,33,0,90,93,21,240,55,1,0,0,136,19,0,0,46,0,0,0,0,0,0,0,0,0,0,0,128,1,82,0,0,0,83,104,97,114,101,100,32,80,97,99,107,47,97,102,115,47,85,83,82,47,68,65,84,65,95,67,77,78,46,65,70,83,47,48,52,49,50,95,103,111,107,117,46,98,105,110,80,75,1,2,20,3,20,0,0,0,0,0,0,0,33,0,68,148,43,18,22,0,0,0,22,0,0,0,22,0,0,0,0,0,0,0,0,0,0,0,128,1,213,1,0,0,83,104,97,114,101,100,32,80,97,99,107,47,114,111,115,116,101,114,46,105,110,105,80,75,5,6,0,0,0,0,3,0,3,0,225,0,0,0,31,2,0,0,0,0};
const unsigned char escaping_pack[] = {80,75,3,4,20,0,0,0,0,0,4,116,110,93,131,22,220,140,1,0,0,0,1,0,0,0,15,0,0,0,102,105,108,101,115,47,85,83,82,47,88,46,66,73,78,120,80,75,3,4,20,0,0,0,0,0,4,116,110,93,251,57,43,130,3,0,0,0,3,0,0,0,13,0,0,0,46,46,47,101,115,99,97,112,101,46,116,120,116,98,97,100,80,75,1,2,20,3,20,0,0,0,0,0,4,116,110,93,131,22,220,140,1,0,0,0,1,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,102,105,108,101,115,47,85,83,82,47,88,46,66,73,78,80,75,1,2,20,3,20,0,0,0,0,0,4,116,110,93,251,57,43,130,3,0,0,0,3,0,0,0,13,0,0,0,0,0,0,0,0,0,0,0,128,1,46,0,0,0,46,46,47,101,115,99,97,112,101,46,116,120,116,80,75,5,6,0,0,0,0,2,0,2,0,120,0,0,0,92,0,0,0,0,0};

void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}
std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_bytes(const fs::path &path, const unsigned char *data, size_t size) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data), std::streamsize(size));
}
}

int main() {
    try {
        const std::string digits = "123456789";
        require(crc32_of(reinterpret_cast<const uint8_t *>(digits.data()), digits.size()) == 0xCBF43926u,
                "CRC-32 check value is wrong");
        const auto root = fs::temp_directory_path() / ("openkai2-mod-packages-" + std::to_string(std::random_device{}()));
        fs::remove_all(root);
        const auto mods = root / "mods";
        fs::create_directories(mods);

        write_bytes(root / "shared.zip", shared_pack, sizeof shared_pack);
        const auto installed = install_mod(mods, root / "shared.zip");
        require(installed.filename() == "Shared_Pack", "Installed mod was not named after its mod.ini");
        const auto entry = read(installed / "afs/USR/DATA_CMN.AFS/0412_goku.bin");
        require(entry.size() == 5000 && entry.compare(3000, 4, "ABCD") == 0 && uint8_t(entry[1]) == 7,
                "A deflate-compressed file did not unpack exactly");
        require(read(installed / "roster.ini").find("slot = 13") != std::string::npos, "A stored file did not unpack");
        require(!fs::exists(installed / "Shared Pack"), "The package's top folder was not removed");

        write_bytes(root / "escape.zip", escaping_pack, sizeof escaping_pack);
        bool refused = false;
        try {
            install_mod(mods, root / "escape.zip");
        } catch (const std::exception &) {
            refused = true;
        }
        require(refused && !fs::exists(mods.parent_path() / "escape.txt") && !fs::exists(mods / "escape.txt"),
                "A package with a ../ path was installed");
        for (const auto &item : fs::directory_iterator(mods))
            require(item.path().filename().string().rfind(".installing-", 0) != 0, "A failed install left files behind");

        const auto second = mods / "Second";
        write(second / "mod.ini", "name=Second mod\n");
        write(second / "afs/USR/DATA_CMN.AFS/0412_other.bin", "y");
        write(second / "afs/USR/DATA_CMN.AFS/add/new.bin", "z");
        write(second / "roster.ini", "[character]\nslot = 13 ; taken\n");
        save_load_order(mods, scan_mods(mods));
        auto list = scan_mods(mods);
        auto conflicts = mod_conflicts(list);
        require(conflicts.size() == 1 && conflicts[0].count == 2 && conflicts[0].earlier == "Shared Pack" &&
                    conflicts[0].later == "Second mod",
                "Two mods changing the same entry and character slot were not reported once with both changes");
        for (auto &mod : list)
            mod.enabled = mod.name != "Second mod";
        require(mod_conflicts(list).empty(), "A disabled mod was reported as a conflict");

        const auto third = mods / "Third";
        write(third / "mod.ini", "name=Third\n");
        write(third / "files/USR/DATA_CMN.AFS", "whole");
        save_load_order(mods, scan_mods(mods));
        list = scan_mods(mods);
        conflicts = mod_conflicts(list);
        bool whole = false;
        for (const auto &conflict : conflicts)
            whole = whole || (conflict.later == "Third" && conflict.what.find("replaced whole") != std::string::npos);
        require(whole, "Replacing an archive another mod edits was not reported");

        ModInfo shared;
        for (const auto &mod : list)
            if (mod.name == "Shared Pack")
                shared = mod;
        export_mod(shared, root / "shared.okmod");
        const auto reinstalled = install_mod(mods, root / "shared.okmod");
        require(reinstalled.filename() == "Shared_Pack_2", "A second install did not get its own folder");
        require(read(reinstalled / "afs/USR/DATA_CMN.AFS/0412_goku.bin") == entry, "Export then install changed a file");
        require(read(reinstalled / "mod.ini") == read(installed / "mod.ini"), "Export then install changed mod.ini");
        fs::remove_all(root);
        std::cout << "Mod packages passed: deflate and stored installs, top-folder packages, path traversal refused, "
                     "export round trip, entry, slot and whole-archive conflicts.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "mod package tests failed: " << error.what() << '\n';
        return 1;
    }
}
