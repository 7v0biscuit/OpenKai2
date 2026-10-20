// SPDX-License-Identifier: GPL-3.0-only
#include "disc_image.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void json_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char ch : value) {
        if (ch=='"' || ch=='\\') out << '\\' << char(ch);
        else if (ch<32) out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << unsigned(ch) << std::dec;
        else out << char(ch);
    }
    out << '"';
}
}

int main(int argc, char* argv[]) {
    if (argc!=2 && !(argc==4 && std::string(argv[2])=="--manifest")) {
        std::cerr << "Usage: openkai2_disc_verify <path-to-Budokai-2-USA.iso> [--manifest output.json]\n";
        return 2;
    }
    try {
        openkai2::DiscOptions options;
        options.progress=[](const std::string& message) { std::cout << message << std::endl; };
        const auto disc=openkai2::prepare_disc(argv[1],openkai2::default_cache_directory(),options);
        if (argc==4) {
            std::ofstream manifest(argv[3],std::ios::binary|std::ios::trunc);
            if (!manifest) throw std::runtime_error("Cannot write verification manifest");
            manifest << "{\n  \"iso_sha256\": "; json_string(manifest,disc.image_sha256);
            manifest << ",\n  \"root\": "; json_string(manifest,disc.root.generic_string());
            manifest << ",\n  \"executable\": "; json_string(manifest,disc.executable.generic_string());
            manifest << ",\n  \"files\": [";
            for (size_t i=0; i<disc.files.size(); ++i) {
                const auto& file=disc.files[i];
                manifest << (i ? ",\n    {\"path\": " : "\n    {\"path\": ");
                json_string(manifest,file.path.generic_string());
                manifest << ", \"size\": " << file.size << ", \"sha256\": ";
                json_string(manifest,file.sha256); manifest << '}';
            }
            manifest << "\n  ]\n}\n";
            manifest.close();
            if (!manifest) throw std::runtime_error("Verification manifest write failed");
        }
        std::cout << "Verified " << disc.files.size() << " disc files; repaired "
                  << disc.files_repaired << " cache entries.\nISO SHA-256: " << disc.image_sha256 << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
