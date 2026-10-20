// SPDX-License-Identifier: GPL-3.0-only
#include "disc_image.h"
#include "ps2_runtime.h"
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc!=2) {
        std::cerr << "Usage: OpenKai2 <path-to-Budokai-2-USA.iso>\n"
                  << "The original game ISO is required on every boot.\n";
        return 2;
    }
    try {
        openkai2::DiscOptions options;
        size_t verified=0;
        options.progress=[&verified](const std::string& message) {
            if (message.rfind("Verifying ",0)==0 && ++verified>1 && verified%128!=0) return;
            std::cout << "[disc] " << message << std::endl;
        };
        const auto disc=openkai2::prepare_disc(argv[1],openkai2::default_cache_directory(),options);
        std::cout << "[disc] Verified " << disc.files.size() << " files (" << disc.bytes_verified
                  << " bytes); refreshed " << disc.files_repaired << " cached files.\n"
                  << "[disc] ISO SHA-256: " << disc.image_sha256 << std::endl;
        PS2Runtime runtime;
        if (!runtime.initialize("OpenKai2 | Dragon Ball Z: Budokai 2")) return 1;
        if (!runtime.loadELF(disc.executable.string())) return 1;
        auto paths=PS2Runtime::getIoPaths();
        paths.cdImage=std::filesystem::absolute(argv[1]);
        PS2Runtime::setIoPaths(paths);
        runtime.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[OpenKai2] " << error.what() << std::endl;
        return 1;
    }
}
