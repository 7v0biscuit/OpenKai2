// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/speedrun_session.h"
#include "launcher/content_unlock.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace openkai2::launcher {
SpeedrunCards::~SpeedrunCards() {
    if (!directory_.empty()) {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }
}
std::filesystem::path SpeedrunCards::prepare(const std::filesystem::path &card, speedrun::RunProfile profile) {
    if (!profile.enabled)
        return card;
    if (!directory_.empty())
        throw std::runtime_error("A speedrun memory card is already active");
    static std::atomic<unsigned> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    directory_ = card.parent_path() / "speedrun-cards" /
        ("session-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
    std::filesystem::create_directories(directory_ / "mc0");
    if (!profile.new_save) {
        const auto copy = [](const std::filesystem::path &from, const std::filesystem::path &to) {
            if (!std::filesystem::is_directory(from))
                return;
            std::filesystem::create_directories(to);
            for (const auto &entry : std::filesystem::directory_iterator(from))
                if (!entry.is_symlink())
                    std::filesystem::copy(entry.path(), to / entry.path().filename(),
                                          std::filesystem::copy_options::recursive |
                                              std::filesystem::copy_options::skip_symlinks);
        };
        copy(card, directory_ / "mc0");
        copy(card.parent_path() / "mc1", directory_ / "mc1");
    }
    const auto save = directory_ / "mc0" / "BASLUS-20779DBZ2" / "BASLUS-20779DBZ2";
    if (std::filesystem::is_regular_file(save)) {
        std::ifstream input(save, std::ios::binary);
        const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(input)), {});
        input.close();
        auto decoded = decode_progress_save(encoded);
        speedrun::prepare_run_save(decoded, profile);
        const auto updated = encode_progress_save(decoded);
        std::ofstream output(save, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(updated.data()), std::streamsize(updated.size()));
        if (!output)
            throw std::runtime_error("Cannot prepare the speedrun memory card");
    } else if (profile.existing_save) {
        throw std::runtime_error("This category needs an existing Budokai 2 save with its required content unlocked. Create or import one before starting.");
    }
    return directory_ / "mc0";
}
}
