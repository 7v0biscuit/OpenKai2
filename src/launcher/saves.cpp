// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/saves.h"
#include "launcher/content_unlock.h"
#include "launcher/paths.h"
#include "launcher/session.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace openkai2::launcher {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t maxBytes = 64u * 1024u * 1024u;
constexpr size_t maxFiles = 4096;
using Bytes = std::vector<uint8_t>;
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
bool link(const fs::path &p) {
#ifdef _WIN32
    const auto flags = GetFileAttributesW(p.c_str());
    if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return fs::is_symlink(fs::symlink_status(p));
}
void safe_parents(const fs::path &path) {
    auto p = fs::absolute(path).lexically_normal();
    for (; !p.empty();) {
        if (link(p))
            fail("Save paths cannot contain links");
        auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
}
std::string fold(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void valid_name(const std::string &name) {
    if (name.empty() || name.size() > 1024 || name.front() == '/' || name.back() == '/')
        fail("Invalid save path");
    size_t begin = 0;
    while (begin < name.size()) {
        const auto end = name.find('/', begin);
        const auto part = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == ".." || part.size() > 240 ||
            part.back() == '.' || part.back() == ' ')
            fail("Unsafe save path");
        for (unsigned char c : part)
            if (c < 32 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|')
                fail("Unsafe save filename");
        const auto base = fold(part.substr(0, part.find('.')));
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
            (base.size() == 4 && (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") &&
             base[3] >= '0' && base[3] <= '9'))
            fail("Reserved save filename");
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
}
void validate(const SaveFiles &files) {
    if (files.size() > maxFiles)
        fail("Save contains too many files");
    uint64_t bytes = 0;
    std::set<std::string> names;
    for (const auto &f : files) {
        valid_name(f.name);
        if (!names.insert(fold(f.name)).second)
            fail("Save contains conflicting filenames");
        bytes += f.data.size();
        if (bytes > maxBytes)
            fail("Save exceeds 64 MB");
    }
    for (const auto &n : names) {
        auto p = n.find('/');
        while (p != std::string::npos) {
            if (names.count(n.substr(0, p)))
                fail("Save file conflicts with a directory");
            p = n.find('/', p + 1);
        }
    }
}
Bytes load(const fs::path &p) {
    safe_parents(p);
    if (!fs::is_regular_file(p))
        fail("Save source is not a regular file");
    const auto size = fs::file_size(p);
    if (size > maxBytes + 16u * 1024u * 1024u)
        fail("Save archive exceeds the size limit");
    Bytes b(static_cast<size_t>(size));
    std::ifstream in(p, std::ios::binary);
    if (!in.read(reinterpret_cast<char *>(b.data()), std::streamsize(b.size())))
        fail("Cannot read save file");
    return b;
}
uint16_t u16(const Bytes &b, size_t at) {
    if (at > b.size() || b.size() - at < 2)
        fail("Truncated save archive");
    return uint16_t(b[at]) | uint16_t(b[at + 1]) << 8u;
}
uint32_t u32(const Bytes &b, size_t at) { return u16(b, at) | (uint32_t(u16(b, at + 2)) << 16u); }
void p16(Bytes &b, uint16_t n) {
    b.push_back(uint8_t(n));
    b.push_back(uint8_t(n >> 8));
}
void p32(Bytes &b, uint32_t n) {
    p16(b, uint16_t(n));
    p16(b, uint16_t(n >> 16));
}
uint32_t crc(const Bytes &b) {
    uint32_t v = ~0u;
    for (auto c : b) {
        v ^= c;
        for (unsigned i = 0; i < 8; ++i)
            v = (v >> 1) ^ ((0u - (v & 1u)) & 0xedb88320u);
    }
    return ~v;
}
std::string unique() {
    static std::atomic<unsigned> serial{0};
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count()) +
           "-" + std::to_string(serial.fetch_add(1));
}
void write_zip(const fs::path &path, const SaveFiles &files) {
    validate(files);
    Bytes b, central;
    for (const auto &f : files) {
        const auto offset = uint32_t(b.size()), sum = crc(f.data), size = uint32_t(f.data.size());
        p32(b, 0x04034b50);
        p16(b, 20);
        p16(b, 0x800);
        p16(b, 0);
        p16(b, 0);
        p16(b, 0x21);
        p32(b, sum);
        p32(b, size);
        p32(b, size);
        p16(b, uint16_t(f.name.size()));
        p16(b, 0);
        b.insert(b.end(), f.name.begin(), f.name.end());
        b.insert(b.end(), f.data.begin(), f.data.end());
        p32(central, 0x02014b50);
        p16(central, 20);
        p16(central, 20);
        p16(central, 0x800);
        p16(central, 0);
        p16(central, 0);
        p16(central, 0x21);
        p32(central, sum);
        p32(central, size);
        p32(central, size);
        p16(central, uint16_t(f.name.size()));
        p16(central, 0);
        p16(central, 0);
        p16(central, 0);
        p16(central, 0);
        p32(central, 0);
        p32(central, offset);
        central.insert(central.end(), f.name.begin(), f.name.end());
    }
    const auto centralOffset = uint32_t(b.size());
    b.insert(b.end(), central.begin(), central.end());
    p32(b, 0x06054b50);
    p16(b, 0);
    p16(b, 0);
    p16(b, uint16_t(files.size()));
    p16(b, uint16_t(files.size()));
    p32(b, uint32_t(central.size()));
    p32(b, centralOffset);
    p16(b, 0);
    auto tmp = path;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size())))
        fail("Cannot write save backup");
    out.close();
    if (!out)
        fail("Cannot finish save backup");
    fs::rename(tmp, path);
}
SaveFiles zip(const Bytes &b) {
    if (b.size() < 22 || u32(b, b.size() - 22) != 0x06054b50)
        fail("Unsupported ZIP: use an OpenKai2 backup or import an extracted save folder");
    const auto end = b.size() - 22;
    const auto count = u16(b, end + 10);
    const auto centralBytes = u32(b, end + 12), centralOffset = u32(b, end + 16);
    if (u16(b, end + 4) || u16(b, end + 6) || u16(b, end + 8) != count || count > maxFiles ||
        uint64_t(centralOffset) + centralBytes != end)
        fail("Invalid ZIP directory");
    SaveFiles files;
    size_t at = centralOffset;
    uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (u32(b, at) != 0x02014b50 || u16(b, at + 10) != 0 || (u16(b, at + 8) & ~0x800u))
            fail("Compressed ZIPs must be extracted before import");
        const auto sum = u32(b, at + 16), size = u32(b, at + 24), packed = u32(b, at + 20),
                   local = u32(b, at + 42);
        const auto n = u16(b, at + 28), extra = u16(b, at + 30), comment = u16(b, at + 32);
        if (size != packed || uint64_t(at) + 46 + n + extra + comment > end ||
            (total += size) > maxBytes)
            fail("Invalid ZIP entry");
        std::string name(reinterpret_cast<const char *>(b.data() + at + 46), n);
        valid_name(name);
        if (u32(b, local) != 0x04034b50 || u16(b, local + 8) != 0 ||
            u16(b, local + 6) != u16(b, at + 8) || u32(b, local + 14) != sum ||
            u32(b, local + 18) != size || u32(b, local + 22) != size)
            fail("ZIP header does not match its directory");
        const auto ln = u16(b, local + 26), le = u16(b, local + 28);
        const uint64_t payload = uint64_t(local) + 30 + ln + le;
        if (payload + size > centralOffset || ln != n ||
            std::string(reinterpret_cast<const char *>(b.data() + local + 30), ln) != name)
            fail("Invalid ZIP file bounds");
        SaveFile f{name, Bytes(b.begin() + size_t(payload), b.begin() + size_t(payload) + size)};
        if (crc(f.data) != sum)
            fail("Backup checksum failed");
        files.push_back(std::move(f));
        at += 46 + n + extra + comment;
    }
    if (at != end)
        fail("Invalid ZIP directory size");
    validate(files);
    return files;
}
SaveFiles psu(const Bytes &b) {
    if (b.size() < 1536 || (u16(b, 0) & 0x20u) == 0u)
        fail("Invalid PSU save header");
    auto name = [&](size_t p) {
        if (p > b.size() || b.size() - p < 96)
            fail("Truncated PSU entry");
        size_t n = 0;
        while (n < 32 && b[p + 64 + n])
            ++n;
        if (!n || n == 32)
            fail("Invalid PSU filename");
        return std::string(reinterpret_cast<const char *>(b.data() + p + 64), n);
    };
    const auto directory = name(0);
    valid_name(directory);
    if (directory.find('/') != std::string::npos)
        fail("Invalid PSU save directory");
    const auto count = u32(b, 4);
    if (count < 2 || count > maxFiles + 2)
        fail("Invalid PSU file count");
    if (name(512) != "." || name(1024) != "..")
        fail("Invalid PSU directory entries");
    SaveFiles files;
    size_t pos = 1536;
    uint64_t total = 0;
    for (uint32_t i = 2; i < count; ++i) {
        if (pos > b.size() || b.size() - pos < 512 || (u16(b, pos) & 0x10u) == 0u)
            fail("Invalid PSU file entry");
        const auto size = u32(b, pos + 4);
        const auto n = name(pos);
        valid_name(n);
        if (n.find('/') != std::string::npos)
            fail("Invalid PSU filename");
        pos += 512;
        if ((total += size) > maxBytes || size > b.size() - pos)
            fail("Truncated PSU file");
        files.push_back({directory + "/" + n, Bytes(b.begin() + pos, b.begin() + pos + size)});
        const uint64_t advance = (uint64_t(size) + 1023u) & ~uint64_t(1023u);
        if (advance > b.size() - pos)
            fail("Truncated PSU padding");
        pos += size_t(advance);
    }
    if (pos != b.size())
        fail("Unexpected trailing PSU data");
    validate(files);
    return files;
}
} // namespace
SaveFiles read_card(const fs::path &card) {
    safe_parents(card);
    if (!fs::exists(card))
        return {};
    if (!fs::is_directory(card))
        fail("Memory card path is not a directory");
    SaveFiles files;
    uint64_t total = 0;
    for (const auto &e : fs::recursive_directory_iterator(card)) {
        if (link(e.path()))
            fail("Save folders cannot contain links");
        if (e.is_directory())
            continue;
        if (!e.is_regular_file())
            fail("Save folder contains a special file");
        if (files.size() >= maxFiles || (total += e.file_size()) > maxBytes)
            fail("Save folder exceeds the size limit");
        files.push_back({path_utf8(e.path().lexically_relative(card)), load(e.path())});
    }
    std::sort(files.begin(), files.end(),
              [](const SaveFile &a, const SaveFile &b) { return a.name < b.name; });
    validate(files);
    return files;
}
SaveFiles read_save(const fs::path &source) {
    safe_parents(source);
    if (fs::is_directory(source)) {
        auto files = read_card(source);
        // Accept either a complete mc0 folder or one exported game directory.
        if (std::any_of(files.begin(), files.end(),
                        [](const SaveFile &f) { return f.name == "icon.sys"; }))
            for (auto &f : files)
                f.name = path_utf8(source.filename()) + "/" + f.name;
        validate(files);
        return files;
    }
    const auto bytes = load(source);
    const auto ext = fold(source.extension().string());
    if (ext == ".zip")
        return zip(bytes);
    if (ext == ".psu")
        return psu(bytes);
    fail("Import a PSU save, an OpenKai2 ZIP backup, or an extracted memory-card folder");
}
std::vector<fs::path> list_backups(const fs::path &backups) {
    safe_parents(backups);
    std::vector<fs::path> result;
    if (!fs::exists(backups))
        return result;
    for (const auto &e : fs::directory_iterator(backups))
        if (!link(e.path()) && e.is_regular_file() && e.path().extension() == ".zip")
            result.push_back(e.path());
    std::sort(result.rbegin(), result.rend());
    return result;
}
fs::path backup_card(const fs::path &card, const fs::path &backups) {
    SessionLock session;
    const auto files = read_card(card);
    if (files.empty())
        return {};
    safe_parents(backups);
    fs::create_directories(backups);
    auto path = backups / ("save-" + unique() + ".zip");
    write_zip(path, files);
    return path;
}
namespace {
void commit_card(const fs::path& card,const fs::path& backups,const SaveFiles& final) {
    backup_card(card, backups);
    safe_parents(card);
    fs::create_directories(card.parent_path());
    auto staging = card.parent_path() / (".save-import-" + unique());
    auto rollback = card.parent_path() / (".save-previous-" + unique());
    bool moved = false;
    try {
        fs::create_directory(staging);
        for (const auto &f : final) {
            const auto p = staging / fs::u8path(f.name);
            if (fs::exists(p))
                throw std::runtime_error("Save files resolve to the same destination");
            fs::create_directories(p.parent_path());
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (!out.write(reinterpret_cast<const char *>(f.data.data()),
                           std::streamsize(f.data.size())))
                fail("Cannot stage imported save");
            out.close();
            if (!out)
                fail("Cannot finish imported save");
        }
        if (fs::exists(card)) {
            fs::rename(card, rollback);
            moved = true;
        }
        try {
            fs::rename(staging, card);
        } catch (...) {
            if (moved) {
                fs::rename(rollback, card);
                moved = false;
            }
            throw;
        }
        if (moved) {
            std::error_code ec;
            fs::remove_all(rollback, ec);
        }
    } catch (...) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        throw;
    }
}
} // namespace
void import_save(const fs::path &card, const fs::path &source, const fs::path &backups,
                 bool restore) {
    SessionLock session;
    auto incoming = read_save(source);
    if (incoming.empty())
        fail("The selected save is empty");
    auto old = read_card(card);
    std::map<std::string, SaveFile> combined;
    if (!restore)
        for (auto &f : old)
            combined.emplace(fold(f.name), std::move(f));
    for (auto &f : incoming)
        combined[fold(f.name)] = std::move(f);
    SaveFiles final;
    for (auto &item : combined)
        final.push_back(std::move(item.second));
    validate(final);
    commit_card(card,backups,final);
}
bool unlock_card_content(const fs::path& card,const fs::path& backups) {
    SessionLock session;
    auto files=read_card(card);
    auto progress=std::find_if(files.begin(),files.end(),[](const SaveFile& file){
        return fold(file.name)=="baslus-20779dbz2/baslus-20779dbz2";
    });
    if(progress==files.end())fail("Create a Budokai 2 USA save in the game before unlocking content");
    auto decoded=decode_progress_save(progress->data);
    if(!unlock_progress_content(decoded))return false;
    progress->data=encode_progress_save(decoded);
    // Validate the exact bytes which will be installed, before creating a
    // backup or replacing anything on the existing memory card.
    decode_progress_save(progress->data);
    commit_card(card,backups,files);
    return true;
}

} // namespace openkai2::launcher
