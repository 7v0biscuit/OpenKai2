// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/content_unlock.h"
#include "launcher/capsule_catalog.h"
#include "launcher/saves.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace openkai2::launcher;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid save operation was accepted");
}
void put(const fs::path &p, const std::vector<uint8_t> &b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()), b.size());
    require(bool(f), "Fixture write failed");
}
std::vector<uint8_t> read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void outer_sum(std::vector<uint8_t> &data) {
    for (unsigned j = 0; j < 16u; ++j) {
        uint8_t s = 0;
        for (size_t i = 0x20u + j; i < data.size(); i += 16u)
            s = uint8_t(s + data[i]);
        data[0x10u + j] = s;
    }
}
} // namespace
int main() {
    try {
        const auto root = fs::temp_directory_path() /
                          ("openkai2-unlock-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path p;
            ~Cleanup() {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
        } cleanup{root};
        std::vector<uint8_t> plain(0xab0u);
        const std::string header = "#SPF 1.0";
        std::copy(header.begin(), header.end(), plain.begin());
        plain[12] = 0x25;
        plain[13] = 0x39;
        plain[14] = 0x74;
        plain[15] = 0xf0;
        for (size_t i = 0x30u; i < plain.size(); ++i)
            plain[i] = uint8_t(i * 37u + (i >> 3u));
        // Mixture of missing and already-owned unlocks; quantities must survive.
        for (unsigned i = 0; i < 520u; ++i)
            plain[0x89eu + i] = i % 3u ? 0u : 7u;
        std::fill(plain.begin() + 0x90u, plain.begin() + 0x98u, 0u);
        plain[0xb4] = plain[0xb5] = 0u;
        auto encrypted = encode_progress_save(plain);
        const auto decoded = decode_progress_save(encrypted);
        require(std::equal(plain.begin() + 0x30u, plain.end(), decoded.begin() + 0x30u),
                "Save payload did not survive its codec");
        require(encode_progress_save(decoded) == encrypted, "Encoding an unchanged save changed bytes");
        auto unlocked = decoded;
        require(unlock_progress_content(unlocked), "Locked fixture reported no changes");
        constexpr unsigned bits[] = {0, 2,  28, 26, 3,  27, 5,  6,  4,  7,  8,  24, 21, 33, 35,
                                     9, 10, 12, 11, 15, 16, 17, 18, 25, 20, 32, 29, 30, 31};
        for (unsigned bit : bits)
            require(unlocked[0x90u + bit / 8u] & (1u << (bit & 7u)), "A character remains locked");
        require(unlocked[0xb4] == 255u && unlocked[0xb5] == 1u, "A stage remains locked");
        for (unsigned i = 0; i < 29u; ++i)
            require(unlocked[0x89eu + 0x1cdu + i] == std::max<uint8_t>(1u, decoded[0x89eu + 0x1cdu + i]),
                    "Character capsule quantity changed incorrectly");
        for (unsigned i = 0; i < 9u; ++i)
            require(unlocked[0x89eu + 0x1feu + i] == std::max<uint8_t>(1u, decoded[0x89eu + 0x1feu + i]),
                    "Stage capsule quantity changed incorrectly");
        unsigned capsules = 0;
        for (unsigned item = 0; item < 520u; ++item) {
            const auto capacity = capsule_capacity(item);
            if (capacity) ++capsules;
            require(unlocked[0x89eu + item] == std::max<uint8_t>(uint8_t(capacity), decoded[0x89eu + item]),
                    "Capsule ownership differs from its supported capacity");
        }
        require(capsules == 404u && unlocked[0xb0u] == std::max<uint8_t>(2u, decoded[0xb0u]),
                "The full capsule catalog or tournament level was not unlocked");
        for (size_t i = 0; i < plain.size(); ++i) {
            const bool allowed = (i >= 0x90u && i < 0x98u) || (i >= 0xb4u && i < 0xb6u) ||
                                 i == 0xb0u ||
                                 (i >= 0x89eu && i < 0x89eu + 520u && capsule_capacity(unsigned(i - 0x89eu)));
            if (!allowed)
                require(unlocked[i] == decoded[i],
                        "Unlocks changed an unrelated story, inventory or preference byte");
        }
        require(!unlock_progress_content(unlocked), "Unlocks are not idempotent");
        const auto output = encode_progress_save(unlocked);
        decode_progress_save(output);
        auto invalid = encrypted;
        invalid[100] ^= 1u;
        rejects([&] { decode_progress_save(invalid); });
        outer_sum(invalid);
        rejects([&] { decode_progress_save(invalid); }); // Valid outer sum, damaged inner sum.
        invalid = encrypted;
        invalid.resize(2735u);
        rejects([&] { decode_progress_save(invalid); });
        invalid = encrypted;
        invalid[0] = '!';
        rejects([&] { decode_progress_save(invalid); });
        const auto card = root / "mc0", backups = root / "backups",
                   progress = card / "BASLUS-20779DBZ2/BASLUS-20779DBZ2";
        put(progress, encrypted);
        put(card / "BASLUS-20779DBZ2/icon.sys", {1, 2, 3});
        put(card / "another-game/data", {9, 8, 7});
        require(unlock_card_content(card, backups), "Save tool did not install unlocks");
        require(read(progress) == output &&
                    read(card / "another-game/data") == std::vector<uint8_t>({9, 8, 7}),
                "Save tool changed another file");
        auto copies = list_backups(backups);
        require(copies.size() == 1u, "Unlocks did not create exactly one backup");
        require(!unlock_card_content(card, backups) && list_backups(backups).size() == 1u,
                "Repeated unlocks wrote a redundant backup");
        import_save(card, copies.front(), root / "restore-backups", true);
        require(read(progress) == encrypted, "Unlock backup did not restore the exact original save");
        put(progress, invalid);
        const auto backupCount = list_backups(backups).size();
        rejects([&] { unlock_card_content(card, backups); });
        require(read(progress) == invalid && list_backups(backups).size() == backupCount,
                "Rejected save changed card or backups");
        fs::remove(progress);
        rejects([&] { unlock_card_content(card, backups); });
        const auto deleteCard = root / "delete-card", deleteBackups = root / "delete-backups";
        put(deleteCard / "game-a/progress", {1, 2});
        put(deleteCard / "game-b/progress", {3, 4});
        put(deleteCard / "game-c/progress", {5, 6});
        put(deleteCard / "card-metadata", {7});
        require(list_card_saves(deleteCard).size() == 3u, "Game-save listing included individual files");
        rejects([&] { delete_card_saves(deleteCard, deleteBackups, {"game-a", "missing"}); });
        rejects([&] { delete_card_saves(deleteCard, deleteBackups, {"../game-a"}); });
        require(fs::exists(deleteCard / "game-a/progress") && list_backups(deleteBackups).empty(),
                "Invalid deletion changed the card or created a backup");
        delete_card_saves(deleteCard, deleteBackups, {"GAME-A", "game-b"});
        require(!fs::exists(deleteCard / "game-a") && !fs::exists(deleteCard / "game-b") &&
                    read(deleteCard / "game-c/progress") == std::vector<uint8_t>({5, 6}) &&
                    read(deleteCard / "card-metadata") == std::vector<uint8_t>({7}),
                "Multi-delete changed an unselected game save");
        const auto deletedSnapshot = list_backups(deleteBackups);
        require(deletedSnapshot.size() == 1u, "Multi-delete did not create one card backup");
        import_save(deleteCard, deletedSnapshot.front(), root / "delete-restore-backups", true);
        require(list_card_saves(deleteCard).size() == 3u, "Deleted saves were not recoverable from the backup");
        const auto extraBackup = backup_card(deleteCard, deleteBackups);
        const auto thirdBackup = backup_card(deleteCard, deleteBackups);
        put(root / "outside.zip", {9});
        rejects([&] { delete_backup_files(deleteBackups, {extraBackup, root / "outside.zip"}); });
        require(fs::exists(extraBackup), "Invalid backup selection partially deleted valid selections");
        delete_backup_files(deleteBackups, {extraBackup, thirdBackup});
        require(list_backups(deleteBackups) == deletedSnapshot && fs::exists(root / "outside.zip"),
                "Multi-delete removed an unselected backup");
        std::cout << "Content unlock checks passed: 404 capsules, 29 characters, nine stages, two checksums, "
                     "unrelated-byte preservation, atomic card backup and exact restoration.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
