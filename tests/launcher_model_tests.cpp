// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_settings.h"
#include "launcher/saves.h"
#include "launcher/settings.h"
#include "launcher/update.h"
#include "disc_image.h"
#include "simulation_pacing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace openkai2::launcher;
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid operation was accepted");
}
void put(const fs::path &p, const std::vector<uint8_t> &b) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    require(bool(out), "Fixture write failed");
}
std::vector<uint8_t> bytes(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void word(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (i * 8));
}
int main() {
    try {
        const auto root =
            fs::temp_directory_path() /
            ("openkai2-launcher-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path p;
            ~Cleanup() {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
        } cleanup{root};
        fs::create_directories(root);
        put(root/"empty",{});put(root/"abc",{'a','b','c'});
        require(openkai2::sha256_file(root/"empty")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Empty SHA-256 oracle mismatch");
        require(openkai2::sha256_file(root/"abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 oracle mismatch");
        Settings s;
        s.iso = fs::u8path(u8"C:/Games/été 日本語/game image.iso");
        s.fps = 144;
        s.width = 2560;
        s.height = 1440;
        s.windowMode = 1;
        s.antialiasing = 1;
        s.textureFilter = 2;
        s.upscaler = 3;
        s.aspect = 1;
        s.vsync = false;
        s.autoBackups = false;
        s.audio = false;
        s.volume = 75;
        s.renderer = "gpu";
        s.autoUpdates = false;
        s.updateChannel = 0;
        write_settings(root / "settings.ini", s);
        auto read = read_settings(root / "settings.ini");
        require(read.iso == s.iso && read.fps == 144 && read.width == 2560 && read.height == 1440 &&
                    read.windowMode == 1 && read.antialiasing == 1 && read.textureFilter == 2 &&
                    read.upscaler == 3 && read.aspect == 1 && !read.vsync && !read.autoBackups &&
                    !read.audio && read.volume == 75 &&
                    read.renderer == "gpu" && !read.autoUpdates && read.updateChannel == 0,
                "Settings roundtrip changed values");
        require(read_settings(root / "absent.ini").fps == 60, "Default pacing is not 60 FPS");
        require(read_settings(root / "absent.ini").audio && read_settings(root / "absent.ini").volume == 100,
                "Game audio is not on at full volume by default");
        s.volume = 101;
        rejects([&] { write_settings(root / "settings.ini", s); });
        s.volume = 75;
        auto original = bytes(root / "settings.ini");
        s.fps = 241;
        rejects([&] { write_settings(root / "settings.ini", s); });
        require(bytes(root / "settings.ini") == original,
                "Rejected settings overwrote existing values");
        put(root / "bad.ini", {'v', 'e', 'r', 's', 'i', 'o', 'n', ' ', '1', '\n', 'f', 'p', 's',
                               ' ', '-', '1', '\n'});
        rejects([&] { read_settings(root / "bad.ini"); });
        require(quote_argument(L"C:\\Game files\\") == L"\"C:\\Game files\\\\\"",
                "Trailing slash quoting failed");
        require(quote_argument(L"a\"b") == L"\"a\\\"b\"", "Embedded quote escaping failed");
        const std::vector<std::string> orderedVersions{"1.0.0-alpha", "1.0.0-alpha.1",
            "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
            "1.0.0-rc.1", "1.0.0", "1.0.1", "1.2.0", "1.10.0", "2.0.0"};
        for (size_t i = 1; i < orderedVersions.size(); ++i) {
            require(compare_versions(orderedVersions[i-1], orderedVersions[i]) < 0 &&
                    compare_versions(orderedVersions[i], orderedVersions[i-1]) > 0,
                    "Semantic version ordering is incorrect");
        }
        require(compare_versions("1.0.0+build.01", "1.0.0+other") == 0,
                "Build metadata changed version precedence");
        for (const auto &bad : {"1.0", "v1.0.0", "01.0.0", "1.0.0-01", "1.0.0-", "1.0.0+", "1.0.0-x..y"})
            rejects([&] { compare_versions(bad, "1.0.0"); });
        auto releaseJson = [](const std::string &tag, bool pre, bool draft = false) {
            std::string json = "{\"tag_name\":\"" + tag + "\",\"prerelease\":" +
                (pre ? "true" : "false") + ",\"draft\":" + (draft ? "true" : "false") + ",\"assets\":[";
            bool first = true;
            for (const std::string name : {"OpenKai2.exe", "OpenKai2-windows-x64.zip", "SHA256SUMS.txt"}) {
                if (!first) json += ',';
                first = false;
                json += "{\"name\":\"" + name + "\",\"size\":123,\"browser_download_url\":\"https://github.com/7v0biscuit/OpenKai2/releases/download/" + tag + "/" + name + "\"}";
            }
            return json + "]}";
        };
        const auto feed = "[" + releaseJson("v0.1.26", false) + "," +
            releaseJson("v0.2.0", true) + "," + releaseJson("v9.0.0", false, true) + "]";
        auto stable = select_release(feed, "0.1.25", false);
        auto development = select_release(feed, "0.1.25", true);
        require(stable && stable->version == "0.1.26" && development &&
                development->version == "0.2.0", "Update channels selected the wrong release");
        require(!select_release(feed, "0.2.0", true), "Updater would downgrade/reinstall this version");
        require(!select_release("[" + releaseJson("v1.0.0-rc.1", false) + "]", "0.1.25", false),
                "Prerelease tag bypassed stable channel");
        require(!select_release("[]", "0.1.25", true), "Empty release feed created an update");
        auto hostile = "[" + releaseJson("v0.1.26", false) + "]";
        const auto host = hostile.find("https://github.com/");
        hostile.replace(host, 18, "https://evil.test/");
        require(!select_release(hostile, "0.1.25", true), "Untrusted release asset was accepted");
        for (const std::string invalidJson : {"[", "[] trailing", "[{\"draft\":false,\"draft\":true}]", "[null]"})
            rejects([&] { select_release(invalidJson, "0.1.25", true); });
        require(!trusted_asset_url("https://github.com/7v0biscuit/OpenKai2/releases/download/v1.0.0/OpenKai2.exe?other", "v1.0.0", "OpenKai2.exe"), "Asset query bypassed trusted URL");
        const std::string digest(64, 'a');
        require(checksum_for(digest + "  OpenKai2.exe\r\n", "OpenKai2.exe") == digest,
                "Release checksums were parsed incorrectly");
        rejects([&] { checksum_for(digest + "  OpenKai2.exe\n" + digest + "  OpenKai2.exe\n", "OpenKai2.exe"); });
        rejects([&] { checksum_for(digest + "  Other.exe\n", "OpenKai2.exe"); });
        rejects([&] { checksum_for(std::string(64, 'z') + "  OpenKai2.exe\n", "OpenKai2.exe"); });
        const auto card = root / "game/mc0", backups = root / "backups";
        put(card / "BASLUS-20779/save.dat", {1, 2, 3});
        put(card / "OTHER/game.dat", {8, 9});
        const auto archive = backup_card(card, backups);
        require(!archive.empty(), "Backup was not created");
        const auto snapshot = read_save(archive);
        require(snapshot.size() == 2 && snapshot[0].data == std::vector<uint8_t>({1, 2, 3}),
                "ZIP backup changed save bytes");
        put(root / "incoming/BASLUS-20779/save.dat", {4, 5, 6, 7});
        import_save(card, root / "incoming", backups);
        require(bytes(card / "BASLUS-20779/save.dat") == std::vector<uint8_t>({4, 5, 6, 7}) &&
                    bytes(card / "OTHER/game.dat") == std::vector<uint8_t>({8, 9}),
                "Import damaged unrelated saves");
        import_save(card, archive, backups, true);
        require(bytes(card / "BASLUS-20779/save.dat") == std::vector<uint8_t>({1, 2, 3}),
                "Restore did not recover original bytes");
        require(list_backups(backups).size() == 3,
                "Imports/restores did not create recovery backups");
        auto damaged = bytes(archive);
        damaged[30 + std::string("BASLUS-20779/save.dat").size()] ^= 1;
        put(root / "damaged.zip", damaged);
        auto before = read_card(card);
        rejects([&] { import_save(card, root / "damaged.zip", backups, true); });
        require(bytes(card / "BASLUS-20779/save.dat") == before[0].data,
                "Invalid backup changed the card");
        auto traversal = bytes(archive);
        const std::string from = "BASLUS-20779", to = "../ESCAPE123";
        require(from.size() == to.size(), "Traversal fixture size");
        for (size_t pos = 0; pos + from.size() <= traversal.size(); ++pos)
            if (std::equal(from.begin(), from.end(), traversal.begin() + pos))
                std::copy(to.begin(), to.end(), traversal.begin() + pos);
        put(root / "unsafe.zip", traversal);
        rejects([&] { import_save(card, root / "unsafe.zip", backups); });
        require(!fs::exists(root / "ESCAPE123"), "ZIP import escaped its card");
        // Independent PSU fixture: directory, dot entries, one file and 1024-byte padding.
        std::vector<uint8_t> psu(3072);
        auto entry = [&](size_t at, unsigned mode, unsigned length, const std::string &name) {
            word(psu, at, mode);
            word(psu, at + 4, length);
            std::copy(name.begin(), name.end(), psu.begin() + at + 64);
        };
        entry(0, 0x8427, 3, "BASLUS-20779");
        entry(512, 0x8427, 0, ".");
        entry(1024, 0x8427, 0, "..");
        entry(1536, 0x8497, 3, "save.dat");
        psu[2048] = 11;
        psu[2049] = 12;
        psu[2050] = 13;
        put(root / "save.psu", psu);
        auto imported = read_save(root / "save.psu");
        require(imported.size() == 1 && imported[0].name == "BASLUS-20779/save.dat" &&
                    imported[0].data == std::vector<uint8_t>({11, 12, 13}),
                "PSU layout was parsed incorrectly");
        import_save(card, root / "save.psu", backups);
        require(bytes(card / "BASLUS-20779/save.dat") == imported[0].data,
                "PSU import did not commit");
        psu.resize(2050);
        put(root / "truncated.psu", psu);
        rejects([&] { import_save(card, root / "truncated.psu", backups); });
        require(bytes(card / "BASLUS-20779/save.dat") == imported[0].data,
                "Truncated PSU damaged current save");
        std::error_code ec;
        fs::create_directory_symlink(root / "incoming", root / "linked", ec);
        if (!ec)
            rejects([&] { import_save(card, root / "linked", backups); });
        const auto tv = openkai2::display_rectangle(1920, 1080, 512, 448, 0);
        const auto wide = openkai2::display_rectangle(1920, 1080, 512, 448, 1);
        const auto stretch = openkai2::display_rectangle(1280, 1024, 512, 448, 2);
        const auto native = openkai2::display_rectangle(1920, 1080, 512, 448, 3);
        require(std::abs(tv.width - 1440) < 0.01f && std::abs(tv.x - 240) < 0.01f &&
                    tv.height == 1080,
                "Original TV aspect is incorrect");
        require(wide.width == 1920 && wide.height == 1080 && wide.x == 0,
                "Widescreen aspect is incorrect");
        const auto video = openkai2::display_rectangle(1920,1080,640,448,1);
        require(video.width==1440 && video.x==240 && video.height==1080,
                "Widescreen stretched the original movie surface");
        require(stretch.width == 1280 && stretch.height == 1024,
                "Stretch mode did not fill the window");
        require(std::abs(native.width / native.height - 512.0f / 448.0f) < 0.0001f,
                "Native pixel aspect changed");
        // The same guest deadline sequence is independent of the presentation poll rate.
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        const auto origin = Clock::time_point{} + 1s;
        constexpr auto period = 16667us;
        auto scheduled = origin;
        for (unsigned i = 0; i < 600; ++i) {
            auto actual = scheduled + 100us;
            require(openkai2::field_pacing_anchor(scheduled, actual) == scheduled,
                    "Normal pacing drifted");
            scheduled += period;
        }
        const auto delayed = origin + 500ms;
        auto anchor = openkai2::field_pacing_anchor(origin, delayed);
        require(anchor == delayed && anchor + period > delayed,
                "Stalled simulation catches up in a burst");
        for (unsigned fps : {30u, 60u, 120u, 240u}) {
            auto now = delayed;
            auto next = anchor + period;
            for (unsigned poll = 0; poll < fps; ++poll) {
                (void)poll;
                require(next > now, "Window polls advanced simulation");
            }
            require(next == delayed + period, "FPS selection changed guest pacing");
        }
        std::cout << "Launcher checks passed: Unicode settings, argument quoting, lossless ZIP/PSU "
                     "import, backups, restore, invalid archives, path traversal, links, and "
                     "independent simulation pacing, update channels, semantic versions and checksums.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
