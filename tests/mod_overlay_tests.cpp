// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "disc_image.h"
#include "mods/afs.h"
#include "mods/disc_overlay.h"
#include "mods/importer.h"
#include "mods/mod_library.h"
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
// The base fixture plus USR/DATA.AFS: three named entries at sector 24.
Bytes disc() {
    auto image = fixture();
    records(image, 20, {record(std::string(1, '\0'), 20, sector, 2), record(std::string(1, '\1'), 18, sector, 2),
                        record("DATA.AFS;1", 24, 0x2800 + 144), record("DATA.BIN;1", 23, 6)});
    const size_t base = 24 * sector;
    std::memcpy(image.data() + base, "AFS\0", 4);
    put32(image, base + 4, 3);
    const uint32_t offsets[3] = {0x800, 0x1000, 0x2000}, sizes[3] = {100, 3000, 10};
    const char fill[3] = {'A', 'B', 'C'};
    const char *names[3] = {"goku.bin", "vegeta.bin", "stage.bin"};
    for (unsigned i = 0; i < 3; ++i) {
        put32(image, base + 8 + i * 8, offsets[i]);
        put32(image, base + 12 + i * 8, sizes[i]);
        std::memset(image.data() + base + offsets[i], fill[i], sizes[i]);
        std::memcpy(image.data() + base + 0x2800 + i * 48, names[i], std::strlen(names[i]));
        put32(image, base + 0x2800 + i * 48 + 44, sizes[i]);
    }
    put32(image, base + 32, 0x2800);
    put32(image, base + 36, 144);
    return image;
}
void file(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}
std::string slice(const std::string &disc, uint64_t at, uint64_t size) { return disc.substr(size_t(at), size_t(size)); }
std::string virtual_disc() {
    auto &overlay = DiscOverlay::global();
    std::string bytes(size_t(overlay.size()), '\0');
    require(overlay.read(0, bytes.data(), bytes.size()), "The virtual disc cannot be read");
    return bytes;
}
AfsArchive archive_at(const std::string &bytes, uint64_t start, uint64_t size) {
    const ReadFn read = [&](uint64_t offset, uint8_t *out, size_t count) {
        std::memcpy(out, bytes.data() + start + offset, count);
    };
    return parse_afs(read, size);
}
} // namespace

int main() {
    try {
        const auto root = fs::temp_directory_path() / "openkai2-mod-tests";
        fs::remove_all(root);
        fs::create_directories(root);
        { // Close all disc streams before removing their files on Windows.

        const auto image = root / "game.iso";
        write(image, disc());
        const auto mods = root / "mods";

        // An entry that fits where the original was is written in place.
        file(mods / "Small" / "afs" / "USR" / "DATA.AFS" / "0001_vegeta.bin", std::string(2000, 'X'));
        auto summary = DiscOverlay::global().configure(image, {mods / "Small"});
        openkai2::IsoIndex index(image);
        const auto *afs = index.find("cdrom0:\\USR\\DATA.AFS;1");
        require(afs && afs->sector == 24 && summary.replaced_entries == 1 && summary.relocated_files == 0,
                "An entry that fits was not replaced in place");
        auto bytes = virtual_disc();
        auto archive = archive_at(bytes, 24 * sector, afs->size);
        require(archive.entries[1].size == 2000 && slice(bytes, 24 * sector + 0x1000, 2000) == std::string(2000, 'X') &&
                    slice(bytes, 24 * sector + 0x800, 100) == std::string(100, 'A') && archive.entries[1].name == "vegeta.bin",
                "The in-place entry or its neighbours are wrong");

        // A larger entry, an added one and a whole-file replacement move the
        // archive past the end of the image; a later mod wins.
        file(mods / "Big" / "afs" / "USR" / "DATA.AFS" / "0000.bin", std::string(5000, 'Y'));
        file(mods / "Big" / "afs" / "USR" / "DATA.AFS" / "add" / "cell.bin", "new!!!!");
        file(mods / "Big" / "files" / "USR" / "DATA.BIN", "hello world!!");
        summary = DiscOverlay::global().configure(image, {mods / "Small", mods / "Big"});
        afs = index.find("USR/DATA.AFS");
        const auto *data = index.find("USR/DATA.BIN");
        require(afs && afs->sector >= 32 && data && data->sector == 23 && data->size == 13,
                "Relocated and replaced files were not found at their new locations");
        require(summary.replaced_entries == 2 && summary.added_entries == 1 && summary.relocated_files == 1 &&
                    summary.replaced_files == 1,
                "The overlay summary does not match the mods");
        bytes = virtual_disc();
        archive = archive_at(bytes, uint64_t(afs->sector) * sector, afs->size);
        const auto entry = [&](size_t i) {
            return slice(bytes, uint64_t(afs->sector) * sector + archive.entries[i].offset, archive.entries[i].size);
        };
        require(archive.entries.size() == 4 && entry(0) == std::string(5000, 'Y') && entry(1) == std::string(2000, 'X') &&
                    entry(2) == std::string(10, 'C') && entry(3) == "new!!!!" && archive.entries[3].name == "cell.bin" &&
                    archive.entries[2].name == "stage.bin",
                "The rebuilt archive lost or changed entries");
        require(slice(bytes, 23 * sector, 13) == "hello world!!", "The replaced file was not read back");

        // The patched directory records describe the same disc on their own.
        const auto expectedAfs = *afs;
        const auto written = root / "modded.iso";
        {
            std::ofstream out(written, std::ios::binary);
            out << bytes;
        }
        DiscOverlay::global().clear();
        openkai2::IsoIndex modded(written);
        const auto *movedAfs = modded.find("USR/DATA.AFS");
        const auto *movedData = modded.find("USR/DATA.BIN");
        require(movedAfs && movedAfs->sector == expectedAfs.sector && movedAfs->size == expectedAfs.size && movedData &&
                    movedData->size == 13,
                "The patched directory records do not match the overlay");

        // An entry number past the end of an archive is an error, not a guess.
        file(mods / "Bad" / "afs" / "USR" / "DATA.AFS" / "0009.bin", "x");
        bool rejected = false;
        try {
            DiscOverlay::global().configure(image, {mods / "Bad"});
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("has no entry 9") != std::string::npos;
        }
        require(rejected, "A missing entry number was accepted");
        DiscOverlay::global().clear();

        // The load order keeps disabled mods and appends new ones enabled.
        file(mods / "Small" / "mod.ini", "name=Small swap\nauthor=Tester\n");
        auto list = scan_mods(mods);
        require(list.size() == 3 && list[0].id == "Bad" && list[1].id == "Big" && list[2].name == "Small swap",
                "Mods were not listed in name order");
        list[0].enabled = false;
        std::swap(list[1], list[2]);
        save_load_order(mods, list);
        const auto enabled = enabled_mod_folders(mods);
        require(enabled.size() == 2 && enabled[0].filename() == "Small" && enabled[1].filename() == "Big",
                "The load order or enabled flags were not kept");
        const auto created = create_mod(mods, "Goku SSJ4!", "Me", "Test");
        require(created.filename() == "Goku_SSJ4" && scan_mods(mods).back().name == "Goku SSJ4!",
                "A created mod was not added to the load order");
        // Renaming changes the shown name only; the reports folder is not a mod;
        // deleting removes the folder and its load order line.
        file(created / "notes.txt", "original");
        const auto originalNotes = [&] {
            std::ifstream in(created / "notes.txt", std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        { // Abandoning a multi-file import leaves every original file alone.
            ModEdit edit(created);
            file(edit.folder() / "notes.txt", "partial import");
            file(edit.folder() / "stages.ini", "incomplete");
            require(scan_mods(mods).size() == 4, "A staged import appeared as an enabled mod");
        }
        require(originalNotes() == "original" && !fs::exists(created / "stages.ini"),
                "An unfinished mod edit changed the live mod");
        bool rolledBack = false;
        try {
            ModEdit edit(created);
            fs::remove_all(edit.folder()); // An installation failure after staging.
            edit.commit();
        } catch (const fs::filesystem_error &) { rolledBack = true; }
        require(rolledBack && originalNotes() == "original", "A failed mod commit lost the previous mod");
        {
            ModEdit edit(created);
            file(edit.folder() / "notes.txt", "complete import");
            file(edit.folder() / "stages.ini", "complete");
            edit.commit();
        }
        require(originalNotes() == "complete import" && fs::exists(created / "stages.ini"),
                "The complete mod edit was not installed");
        for (const auto &item : fs::directory_iterator(mods))
            require(item.path().filename().string().rfind(".import-", 0) != 0 &&
                    item.path().filename().string().rfind(".previous-", 0) != 0,
                    "A completed or abandoned mod edit left temporary folders");
        rename_mod(created, "SSJ4 Goku");
        fs::create_directories(mods / reports_folder);
        list = scan_mods(mods);
        require(created.filename() == "Goku_SSJ4" && list.back().name == "SSJ4 Goku" && list.back().author == "Me" &&
                    std::none_of(list.begin(), list.end(), [](const ModInfo &mod) { return mod.id == reports_folder; }),
                "A renamed mod lost its folder or details, or the reports folder was listed");
        delete_mod(mods, "Goku_SSJ4");
        list = scan_mods(mods);
        std::ifstream order(mods / "load-order.txt");
        const std::string orderText((std::istreambuf_iterator<char>(order)), std::istreambuf_iterator<char>());
        order.close();
        require(!fs::exists(created) && list.size() == 3 && orderText.find("Goku_SSJ4") == std::string::npos,
                "A deleted mod was left on disk or in the load order");
        bool refused = false;
        try {
            delete_mod(mods, "..");
        } catch (const std::runtime_error &) {
            refused = true;
        }
        require(refused && fs::exists(mods), "Deleting outside the mods folder was not refused");
        // The import tool reads another disc and writes its entries into a mod
        // that the overlay then applies to this one.
        SourceDisc source(image);
        const auto &listed = source.files();
        require(listed.size() == 4 && listed[2].path == "USR/DATA.AFS" && listed[2].archive && listed[2].entries == 3 &&
                    listed[3].path == "USR/DATA.BIN" && !listed[3].archive,
                "The source disc listing is wrong");
        require(source.entries("cdrom0:\\USR\\DATA.AFS;1")[2].name == "stage.bin" &&
                    source.read_entry("USR/DATA.AFS", 2) == std::vector<uint8_t>(10, 'C') &&
                    source.read_file("USR/DATA.BIN").size() == 6,
                "Source entries cannot be read");
        const auto port = create_mod(mods, "Port", "Me", "");
        ImportTarget swap{ImportTarget::Mode::Entry, "USR/DATA.AFS", 0, "goku.bin", ContentKind::Character, "B3"};
        import_bytes(port, swap, source.read_entry("USR/DATA.AFS", 2));
        import_bytes(port, swap, source.read_entry("USR/DATA.AFS", 1));
        ImportTarget extra{ImportTarget::Mode::NewEntry, "USR/DATA.AFS", 0, "stage.bin", ContentKind::Stage, "B3"};
        for (int i = 0; i < 3; ++i)
            import_bytes(port, extra, std::vector<uint8_t>(size_t(10 + i), uint8_t('0' + i)));
        // Streamed imports report every byte and leave no temporary file behind.
        uint64_t streamed = 0;
        ImportTarget copy{ImportTarget::Mode::NewEntry, "USR/DATA.AFS", 0, "vegeta.bin", ContentKind::Character, "B3"};
        const auto copied = import_from_disc(source, "USR/DATA.AFS", 1, port, copy, [&](uint64_t bytes) { streamed += bytes; });
        require(streamed == 3000 && fs::file_size(copied) == 3000 && !fs::exists(fs::path(copied).concat(".tmp")),
                "A streamed import was incomplete");
        fs::remove(copied);
        file(root / "scratch.bin", "from scratch");
        ImportTarget whole{ImportTarget::Mode::File, "USR/DATA.BIN", 0, "", ContentKind::Moveset, ""};
        import_host_file(port, whole, root / "scratch.bin");
        require(read_imports(port).size() == 7 && read_imports(port)[6].kind == "moveset" &&
                    !fs::exists(port / "afs" / "USR" / "DATA.AFS" / "0000_goku.bin.tmp"),
                "The imports were not recorded");
        DiscOverlay::global().configure(image, {port});
        afs = index.find("USR/DATA.AFS");
        bytes = virtual_disc();
        archive = archive_at(bytes, uint64_t(afs->sector) * sector, afs->size);
        require(archive.entries.size() == 6 && entry(0) == std::string(3000, 'B') && archive.entries[3].name == "stage.bin" &&
                    archive.entries[4].name == "stage_2.bin" && entry(5) == "222222222222" &&
                    slice(bytes, uint64_t(index.find("USR/DATA.BIN")->sector) * sector, 12) == "from scratch",
                "Imported entries were not applied");
        DiscOverlay::global().clear();
        }
        fs::remove_all(root);
        std::cout << "Mod overlay passed: in-place entries, relocated archives with added entries, file replacement, "
                     "patched directory records, invalid entries and load order.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
