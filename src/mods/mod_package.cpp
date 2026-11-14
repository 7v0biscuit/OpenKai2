// SPDX-License-Identifier: GPL-3.0-only
#include "mods/mod_package.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace openkai2::mods {
namespace fs = std::filesystem;
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
std::string trim(std::string text) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}
std::string utf8(const fs::path &path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
fs::path from_utf8(const std::string &text) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
#else
    return fs::u8path(text);
#endif
}

struct Bits {
    const uint8_t *in;
    size_t size, pos = 0;
    uint32_t buffer = 0;
    int count = 0;
    int take(int n) {
        while (count < n) {
            if (pos >= size)
                throw std::runtime_error("The mod package is cut short");
            buffer |= uint32_t(in[pos++]) << count;
            count += 8;
        }
        const int value = int(buffer & ((1u << n) - 1u));
        buffer >>= n;
        count -= n;
        return value;
    }
};

struct Huffman {
    std::array<short, 16> count{};
    std::vector<short> symbol;
};

void build(Huffman &table, const short *lengths, int n) {
    table.count.fill(0);
    table.symbol.assign(size_t(n), 0);
    for (int i = 0; i < n; ++i)
        ++table.count[size_t(lengths[i])];
    table.count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left = (left << 1) - table.count[size_t(len)];
        if (left < 0)
            throw std::runtime_error("The mod package has a damaged compressed file");
    }
    std::array<short, 16> offsets{};
    for (int len = 1; len < 15; ++len)
        offsets[size_t(len + 1)] = short(offsets[size_t(len)] + table.count[size_t(len)]);
    for (int i = 0; i < n; ++i)
        if (lengths[i])
            table.symbol[size_t(offsets[size_t(lengths[i])]++)] = short(i);
}

int decode(Bits &bits, const Huffman &table) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= bits.take(1);
        const int count = table.count[size_t(len)];
        if (code - count < first)
            return table.symbol[size_t(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    throw std::runtime_error("The mod package has a damaged compressed file");
}

constexpr short length_base[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr short length_extra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr short distance_base[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                   193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr short distance_extra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void inflate_codes(Bits &bits, std::vector<uint8_t> &out, size_t expected, const Huffman &lengths, const Huffman &distances) {
    for (;;) {
        int symbol = decode(bits, lengths);
        if (symbol < 256) {
            if (out.size() >= expected)
                throw std::runtime_error("A file in the mod package is larger than it says");
            out.push_back(uint8_t(symbol));
            continue;
        }
        if (symbol == 256)
            return;
        symbol -= 257;
        if (symbol >= 29)
            throw std::runtime_error("The mod package has a damaged compressed file");
        const size_t length = size_t(length_base[symbol] + bits.take(length_extra[symbol]));
        const int code = decode(bits, distances);
        if (code >= 30)
            throw std::runtime_error("The mod package has a damaged compressed file");
        const size_t distance = size_t(distance_base[code] + bits.take(distance_extra[code]));
        if (distance > out.size() || out.size() + length > expected)
            throw std::runtime_error("The mod package has a damaged compressed file");
        for (size_t i = 0; i < length; ++i)
            out.push_back(out[out.size() - distance]);
    }
}

constexpr uint32_t zip_local = 0x04034b50u, zip_central = 0x02014b50u, zip_end = 0x06054b50u;

void put16(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
}
void put32(std::vector<uint8_t> &out, uint32_t value) {
    put16(out, value & 0xFFFFu);
    put16(out, value >> 16);
}
uint32_t get16(const std::vector<uint8_t> &b, size_t at) {
    if (at + 2 > b.size())
        throw std::runtime_error("The mod package is cut short");
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8;
}
uint32_t get32(const std::vector<uint8_t> &b, size_t at) { return get16(b, at) | get16(b, at + 2) << 16; }

std::vector<uint8_t> read_range(std::ifstream &in, uint64_t at, size_t size) {
    std::vector<uint8_t> bytes(size);
    in.clear();
    in.seekg(std::streamoff(at));
    if (size && !in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(size)))
        throw std::runtime_error("The mod package is cut short");
    return bytes;
}

std::string safe_path(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos)
        throw std::runtime_error("The mod package names a file outside the mod: " + name);
    std::string out;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = std::min(name.find('/', start), name.size());
        const std::string part = name.substr(start, end - start);
        if (part == "..")
            throw std::runtime_error("The mod package names a file outside the mod: " + name);
        if (!part.empty() && part != ".")
            out += (out.empty() ? "" : "/") + part;
        start = end + 1;
    }
    return out;
}

std::string ini_value(const std::string &text, const std::string &key) {
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        const std::string line = text.substr(start, end - start);
        const auto equals = line.find('=');
        if (equals != std::string::npos && trim(line.substr(0, equals)) == key)
            return trim(line.substr(equals + 1));
        start = end + 1;
    }
    return {};
}

std::string folder_id(const std::string &name) {
    std::string id;
    for (unsigned char c : name)
        if (std::isalnum(c) || c == '-' || c == '_')
            id += char(c);
        else if (c == ' ')
            id += '_';
    return id.empty() ? "mod" : id;
}

void add_slots(const fs::path &file, const std::string &kind, std::map<std::string, std::string> &keys,
               std::map<std::string, std::string> &labels) {
    std::ifstream in(file);
    std::string section;
    for (std::string line; std::getline(in, line);) {
        const std::string clean = lower(trim(line));
        if (!clean.empty() && clean.front() == '[') {
            section = clean;
            continue;
        }
        const auto equals = clean.find('=');
        if (equals == std::string::npos || trim(clean.substr(0, equals)) != "slot")
            continue;
        std::string value = trim(clean.substr(equals + 1));
        if (const auto comment = value.find(';'); comment != std::string::npos)
            value = trim(value.substr(0, comment));
        const std::string key = kind + ":" + section + ":" + value;
        keys[key] = key;
        labels[key] = (kind == "stage" ? "stage slot " : "character slot ") + value;
    }
}

std::map<std::string, std::string> touched(const ModInfo &mod, std::map<std::string, std::string> &labels,
                                           std::set<std::string> &archives) {
    std::map<std::string, std::string> keys;
    std::error_code error;
    const auto files = mod.folder / "files";
    if (fs::is_directory(files, error))
        for (fs::recursive_directory_iterator it(files, error), end; !error && it != end; it.increment(error))
            if (it->is_regular_file(error)) {
                const std::string path = lower(utf8(fs::relative(it->path(), files, error)));
                keys["file:" + path] = path;
                labels["file:" + path] = path;
                archives.insert(path);
            }
    const auto afs = mod.folder / "afs";
    if (fs::is_directory(afs, error))
        for (fs::recursive_directory_iterator it(afs, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_regular_file(error))
                continue;
            const std::string path = lower(utf8(fs::relative(it->path(), afs, error)));
            const auto slash = path.rfind('/');
            if (slash == std::string::npos)
                continue;
            const std::string archive = path.substr(0, slash), name = path.substr(slash + 1);
            if (archive.size() < 4 || archive.compare(archive.size() - 4, 4, ".afs") != 0 || name.empty() ||
                !std::isdigit(static_cast<unsigned char>(name.front())))
                continue;
            size_t digits = 0;
            while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits])))
                ++digits;
            const std::string key = "entry:" + archive + "#" + std::to_string(std::stoul(name.substr(0, digits)));
            keys[key] = archive;
            labels[key] = archive + " entry " + std::to_string(std::stoul(name.substr(0, digits)));
        }
    add_slots(mod.folder / "roster.ini", "character", keys, labels);
    add_slots(mod.folder / "stages.ini", "stage", keys, labels);
    return keys;
}

}

uint32_t crc32_of(const uint8_t *data, size_t size, uint32_t crc) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

std::vector<uint8_t> inflate_raw(const uint8_t *data, size_t size, size_t expected) {
    Bits bits{data, size};
    std::vector<uint8_t> out;
    out.reserve(expected);
    int last = 0;
    while (!last) {
        last = bits.take(1);
        const int type = bits.take(2);
        if (type == 0) {
            bits.buffer = 0;
            bits.count = 0;
            if (bits.pos + 4 > size)
                throw std::runtime_error("The mod package is cut short");
            const size_t length = size_t(data[bits.pos]) | size_t(data[bits.pos + 1]) << 8;
            const size_t check = size_t(data[bits.pos + 2]) | size_t(data[bits.pos + 3]) << 8;
            bits.pos += 4;
            if ((length ^ 0xFFFFu) != check || bits.pos + length > size || out.size() + length > expected)
                throw std::runtime_error("The mod package has a damaged compressed file");
            out.insert(out.end(), data + bits.pos, data + bits.pos + length);
            bits.pos += length;
        } else if (type == 1) {
            static const auto fixed = [] {
                std::array<short, 288> lengths{};
                for (int i = 0; i < 288; ++i)
                    lengths[size_t(i)] = short(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
                std::array<short, 30> distances;
                distances.fill(5);
                std::pair<Huffman, Huffman> tables;
                build(tables.first, lengths.data(), 288);
                build(tables.second, distances.data(), 30);
                return tables;
            }();
            inflate_codes(bits, out, expected, fixed.first, fixed.second);
        } else if (type == 2) {
            static constexpr short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            const int nlen = bits.take(5) + 257, ndist = bits.take(5) + 1, ncode = bits.take(4) + 4;
            if (nlen > 286 || ndist > 30)
                throw std::runtime_error("The mod package has a damaged compressed file");
            std::array<short, 320> lengths{};
            for (int i = 0; i < ncode; ++i)
                lengths[size_t(order[i])] = short(bits.take(3));
            Huffman codes;
            build(codes, lengths.data(), 19);
            lengths.fill(0);
            for (int index = 0; index < nlen + ndist;) {
                int symbol = decode(bits, codes);
                if (symbol < 16) {
                    lengths[size_t(index++)] = short(symbol);
                    continue;
                }
                short value = 0;
                int repeat;
                if (symbol == 16) {
                    if (!index)
                        throw std::runtime_error("The mod package has a damaged compressed file");
                    value = lengths[size_t(index - 1)];
                    repeat = 3 + bits.take(2);
                } else if (symbol == 17)
                    repeat = 3 + bits.take(3);
                else
                    repeat = 11 + bits.take(7);
                if (index + repeat > nlen + ndist)
                    throw std::runtime_error("The mod package has a damaged compressed file");
                while (repeat--)
                    lengths[size_t(index++)] = value;
            }
            Huffman literal, distance;
            build(literal, lengths.data(), nlen);
            build(distance, lengths.data() + nlen, ndist);
            inflate_codes(bits, out, expected, literal, distance);
        } else
            throw std::runtime_error("The mod package has a damaged compressed file");
    }
    if (out.size() != expected)
        throw std::runtime_error("A file in the mod package is smaller than it says");
    return out;
}

std::vector<ModConflict> mod_conflicts(const std::vector<ModInfo> &mods) {
    std::vector<ModConflict> found;
    std::map<std::string, size_t> owner;
    std::map<std::string, std::string> labels;
    std::map<std::pair<size_t, size_t>, std::vector<std::string>> pairs;
    std::vector<std::set<std::string>> archives(mods.size());
    std::vector<std::map<std::string, std::string>> keys(mods.size());
    for (size_t i = 0; i < mods.size(); ++i)
        if (mods[i].enabled)
            keys[i] = touched(mods[i], labels, archives[i]);
    for (size_t i = 0; i < mods.size(); ++i) {
        if (!mods[i].enabled)
            continue;
        for (const auto &[key, archive] : keys[i]) {
            if (const auto earlier = owner.find(key); earlier != owner.end())
                pairs[{earlier->second, i}].push_back(labels[key]);
            owner[key] = i;
        }
        for (const auto &archive : archives[i])
            for (size_t j = 0; j < i; ++j) {
                if (!mods[j].enabled)
                    continue;
                for (const auto &[key, entryArchive] : keys[j])
                    if (key.rfind("entry:", 0) == 0 && entryArchive == archive) {
                        pairs[{j, i}].push_back(archive + " (replaced whole)");
                        break;
                    }
            }
    }
    for (auto &[pair, what] : pairs) {
        std::sort(what.begin(), what.end());
        what.erase(std::unique(what.begin(), what.end()), what.end());
        ModConflict conflict;
        conflict.earlier = mods[pair.first].name;
        conflict.later = mods[pair.second].name;
        conflict.count = what.size();
        conflict.what = what.front();
        found.push_back(std::move(conflict));
    }
    return found;
}

void export_mod(const ModInfo &mod, const fs::path &package) {
    std::vector<std::pair<std::string, fs::path>> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(mod.folder, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            files.push_back({utf8(fs::relative(it->path(), mod.folder, error)), it->path()});
    if (files.empty())
        throw std::runtime_error("The mod has no files to export");
    std::sort(files.begin(), files.end());
    auto temporary = package;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Cannot write " + utf8(package));
        std::vector<uint8_t> directory;
        uint64_t offset = 0;
        for (const auto &[name, path] : files) {
            std::ifstream in(path, std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (data.size() >= 0xFFFFFFFFull || offset >= 0xFFFFFFFFull)
                throw std::runtime_error("The mod is too large to export as one package");
            const uint32_t crc = crc32_of(data.data(), data.size());
            std::vector<uint8_t> head;
            for (auto *block : {&head, &directory}) {
                const bool central = block == &directory;
                put32(*block, central ? zip_central : zip_local);
                if (central)
                    put16(*block, 20);
                put16(*block, 20);
                put16(*block, 0x0800);
                put16(*block, 0);
                put16(*block, 0);
                put16(*block, 0x21);
                put32(*block, crc);
                put32(*block, uint32_t(data.size()));
                put32(*block, uint32_t(data.size()));
                put16(*block, uint32_t(name.size()));
                put16(*block, 0);
                if (central) {
                    put16(*block, 0);
                    put16(*block, 0);
                    put16(*block, 0);
                    put32(*block, 0);
                    put32(*block, uint32_t(offset));
                }
                block->insert(block->end(), name.begin(), name.end());
            }
            out.write(reinterpret_cast<const char *>(head.data()), std::streamsize(head.size()));
            out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
            offset += head.size() + data.size();
        }
        if (files.size() > 0xFFFFu || offset >= 0xFFFFFFFFull)
            throw std::runtime_error("The mod is too large to export as one package");
        std::vector<uint8_t> tail;
        put32(tail, zip_end);
        put16(tail, 0);
        put16(tail, 0);
        put16(tail, uint32_t(files.size()));
        put16(tail, uint32_t(files.size()));
        put32(tail, uint32_t(directory.size()));
        put32(tail, uint32_t(offset));
        put16(tail, 0);
        out.write(reinterpret_cast<const char *>(directory.data()), std::streamsize(directory.size()));
        out.write(reinterpret_cast<const char *>(tail.data()), std::streamsize(tail.size()));
        if (!out)
            throw std::runtime_error("Cannot write " + utf8(package));
    }
    fs::rename(temporary, package);
}

fs::path install_mod(const fs::path &directory, const fs::path &package) {
    std::ifstream in(package, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open " + utf8(package));
    in.seekg(0, std::ios::end);
    const uint64_t size = uint64_t(in.tellg());
    const size_t window = size_t(std::min<uint64_t>(size, 65557));
    const auto tail = read_range(in, size - window, window);
    size_t end = std::string::npos;
    for (size_t at = tail.size() >= 22 ? tail.size() - 22 : 0;; --at) {
        if (get32(tail, at) == zip_end) {
            end = at;
            break;
        }
        if (!at)
            break;
    }
    if (end == std::string::npos)
        throw std::runtime_error("This is not a mod package (no ZIP directory found)");
    const uint32_t count = get16(tail, end + 10), directorySize = get32(tail, end + 12), directoryAt = get32(tail, end + 16);
    if (uint64_t(directoryAt) + directorySize > size)
        throw std::runtime_error("The mod package is cut short");
    const auto central = read_range(in, directoryAt, directorySize);
    struct Item {
        std::string name;
        uint32_t method, crc, packed, unpacked, local;
    };
    std::vector<Item> items;
    uint64_t total = 0;
    for (size_t at = 0, i = 0; i < count; ++i) {
        if (get32(central, at) != zip_central)
            throw std::runtime_error("The mod package's directory is damaged");
        Item item{};
        const uint32_t flags = get16(central, at + 8);
        item.method = get16(central, at + 10);
        item.crc = get32(central, at + 16);
        item.packed = get32(central, at + 20);
        item.unpacked = get32(central, at + 24);
        const uint32_t nameLength = get16(central, at + 28), extra = get16(central, at + 30), comment = get16(central, at + 32);
        item.local = get32(central, at + 42);
        if (at + 46 + nameLength > central.size())
            throw std::runtime_error("The mod package's directory is damaged");
        item.name.assign(central.begin() + long(at + 46), central.begin() + long(at + 46 + nameLength));
        at += 46 + nameLength + extra + comment;
        if (flags & 1u)
            throw std::runtime_error("The mod package is encrypted");
        if (!item.name.empty() && (item.name.back() == '/' || item.name.back() == '\\'))
            continue;
        if (item.method != 0 && item.method != 8)
            throw std::runtime_error("The mod package uses an unsupported compression method");
        item.name = safe_path(item.name);
        total += item.unpacked;
        items.push_back(std::move(item));
    }
    if (items.empty())
        throw std::runtime_error("The mod package is empty");
    if (total > (uint64_t(4) << 30))
        throw std::runtime_error("The mod package unpacks to more than 4 GB");
    std::string root;
    {
        const auto slash = items.front().name.find('/');
        const std::string first = slash == std::string::npos ? std::string() : items.front().name.substr(0, slash + 1);
        const bool shared = !first.empty() && std::all_of(items.begin(), items.end(), [&](const Item &item) {
            return item.name.rfind(first, 0) == 0;
        });
        const bool topLevelMod = std::any_of(items.begin(), items.end(), [](const Item &item) {
            const auto name = lower(item.name);
            return name == "mod.ini" || name.rfind("files/", 0) == 0 || name.rfind("afs/", 0) == 0;
        });
        if (shared && !topLevelMod)
            root = first;
    }
    bool hasContent = false;
    std::string modIni;
    for (auto &item : items) {
        item.name = item.name.substr(root.size());
        const auto name = lower(item.name);
        hasContent = hasContent || name.rfind("files/", 0) == 0 || name.rfind("afs/", 0) == 0 || name == "roster.ini" ||
                     name == "stages.ini";
    }
    if (!hasContent)
        throw std::runtime_error("The package holds no mod content (files/, afs/, roster.ini or stages.ini)");
    const auto unpack = [&](const Item &item) {
        const auto local = read_range(in, item.local, 30);
        if (get32(local, 0) != zip_local)
            throw std::runtime_error("The mod package is damaged at " + item.name);
        const uint64_t dataAt = uint64_t(item.local) + 30 + get16(local, 26) + get16(local, 28);
        const auto packed = read_range(in, dataAt, item.packed);
        auto data = item.method == 0 ? packed : inflate_raw(packed.data(), packed.size(), item.unpacked);
        if (data.size() != item.unpacked || crc32_of(data.data(), data.size()) != item.crc)
            throw std::runtime_error("The mod package is damaged at " + item.name);
        return data;
    };
    for (const auto &item : items)
        if (lower(item.name) == "mod.ini") {
            const auto data = unpack(item);
            modIni.assign(data.begin(), data.end());
        }
    std::string name = ini_value(modIni, "name");
    if (name.empty())
        name = utf8(package.stem());
    fs::create_directories(directory);
    std::mt19937_64 random{std::random_device{}()};
    const auto staging = directory / from_utf8(".installing-" + std::to_string(random()));
    try {
        for (const auto &item : items) {
            const auto target = staging / from_utf8(item.name);
            fs::create_directories(target.parent_path());
            const auto data = unpack(item);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
            if (!out)
                throw std::runtime_error("Cannot write " + item.name);
        }
        if (modIni.empty()) {
            std::ofstream ini(staging / "mod.ini");
            ini << "name=" << name << "\nauthor=\nversion=1.0\ndescription=\n";
        }
        auto folder = directory / from_utf8(folder_id(name));
        for (unsigned n = 2; fs::exists(folder); ++n)
            folder = directory / from_utf8(folder_id(name) + "_" + std::to_string(n));
        fs::rename(staging, folder);
        save_load_order(directory, scan_mods(directory));
        return folder;
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
}

}
