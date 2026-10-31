// SPDX-License-Identifier: GPL-3.0-only
#include "mods/importer.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace openkai2::mods {
namespace fs = std::filesystem;
namespace {
constexpr uint64_t sector = 2048;

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
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
std::string safe_name(const std::string &name) {
    std::string safe;
    for (unsigned char c : name)
        safe += std::isalnum(c) || c == '.' || c == '-' || c == '_' ? char(c) : '_';
    while (!safe.empty() && safe.front() == '.')
        safe.erase(safe.begin());
    return safe.empty() ? "entry.bin" : safe;
}
fs::path disc_folder(const fs::path &root, const std::string &disc_path) {
    const auto key = IsoIndex::key(disc_path);
    if (!key || key->empty())
        throw std::runtime_error("Not a disc path: " + disc_path);
    return root / from_utf8(*key);
}
bool entry_file(const fs::path &file, size_t &index) {
    const auto name = utf8(file.filename());
    size_t digits = 0;
    while (digits < name.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(name[digits])))
        ++digits;
    if (!digits || (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))))
        return false;
    index = size_t(std::stoul(name.substr(0, digits)));
    return true;
}
std::string pad(size_t value) {
    auto text = std::to_string(value);
    return std::string(text.size() < 4 ? 4 - text.size() : 0, '0') + text;
}
}

const char *content_name(ContentKind kind) {
    switch (kind) {
    case ContentKind::Character: return "character";
    case ContentKind::Moveset: return "moveset";
    case ContentKind::Stage: return "stage";
    default: return "other";
    }
}

SourceDisc::SourceDisc(const fs::path &image) : image_(image), stream_(image, std::ios::binary) {
    if (!stream_)
        throw std::runtime_error("Cannot open " + image.string());
    image_size_ = fs::file_size(image);
    const IsoIndex index(image);
    for (const auto &entry : index.entries()) {
        if (entry.directory || entry.multi_extent)
            continue;
        const Location location{uint64_t(entry.sector) * sector, entry.size};
        if (location.offset > image_size_ || location.size > image_size_ - location.offset)
            continue;
        SourceFile file;
        file.path = entry.path.generic_string();
        file.size = entry.size;
        locations_.emplace(upper(file.path), location);
        const ReadFn reader = [&](uint64_t offset, uint8_t *out, size_t bytes) {
            read(location.offset + offset, out, bytes);
        };
        try {
            if (is_afs(reader, location.size)) {
                auto archive = parse_afs(reader, location.size);
                file.archive = true;
                file.entries = archive.entries.size();
                archives_.emplace(upper(file.path), std::move(archive.entries));
            }
        } catch (const std::runtime_error &) {
        }
        files_.push_back(std::move(file));
    }
    std::sort(files_.begin(), files_.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
}

SourceDisc::Location SourceDisc::locate(const std::string &path) const {
    const auto key = IsoIndex::key(path);
    const auto found = key ? locations_.find(*key) : locations_.end();
    if (found == locations_.end())
        throw std::runtime_error("The disc has no file " + path);
    return found->second;
}

void SourceDisc::read(uint64_t offset, uint8_t *destination, size_t bytes) {
    if (offset > image_size_ || bytes > image_size_ - offset)
        throw std::runtime_error("Read past the end of " + image_.string());
    stream_.clear();
    stream_.seekg(std::streamoff(offset));
    stream_.read(reinterpret_cast<char *>(destination), std::streamsize(bytes));
    if (size_t(stream_.gcount()) != bytes)
        throw std::runtime_error("Cannot read " + image_.string());
}

const std::vector<AfsEntry> &SourceDisc::entries(const std::string &path) {
    static const std::vector<AfsEntry> none;
    const auto key = IsoIndex::key(path);
    const auto found = key ? archives_.find(*key) : archives_.end();
    return found == archives_.end() ? none : found->second;
}

std::vector<uint8_t> SourceDisc::read_file(const std::string &path) {
    const auto location = locate(path);
    std::vector<uint8_t> bytes(size_t(location.size));
    read(location.offset, bytes.data(), bytes.size());
    return bytes;
}

std::vector<uint8_t> SourceDisc::read_entry(const std::string &path, size_t index) {
    const auto location = locate(path);
    const auto &list = entries(path);
    if (index >= list.size())
        throw std::runtime_error(path + " has no entry " + std::to_string(index));
    const auto &entry = list[index];
    if (entry.offset > location.size || entry.size > location.size - entry.offset)
        throw std::runtime_error(path + " entry " + std::to_string(index) + " lies outside the archive");
    std::vector<uint8_t> bytes(entry.size);
    read(location.offset + entry.offset, bytes.data(), bytes.size());
    return bytes;
}

namespace {
constexpr size_t chunk = 1u << 20;

fs::path store(const fs::path &mod, const ImportTarget &target,
               const std::function<void(std::ostream &, const std::string &)> &write) {
    if (!fs::is_directory(mod))
        throw std::runtime_error("The mod folder does not exist: " + mod.string());
    fs::path destination;
    std::error_code error;
    std::vector<fs::path> replaced;
    switch (target.mode) {
    case ImportTarget::Mode::File:
        destination = disc_folder(mod / "files", target.file);
        break;
    case ImportTarget::Mode::Entry: {
        const auto folder = disc_folder(mod / "afs", target.file);
        if (fs::is_directory(folder, error))
            for (const auto &item : fs::directory_iterator(folder, error)) {
                size_t index = 0;
                if (item.is_regular_file(error) && entry_file(item.path(), index) && index == target.entry)
                    replaced.push_back(item.path());
            }
        destination = folder / from_utf8(pad(target.entry) + "_" + safe_name(target.name));
        break;
    }
    case ImportTarget::Mode::NewEntry: {
        const auto folder = disc_folder(mod / "afs", target.file) / "add";
        auto name = safe_name(target.name);
        if (name.size() > 31)
            name = name.substr(0, 31);
        destination = folder / from_utf8(name);
        const auto dot = name.rfind('.');
        const auto stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
        const auto extension = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
        for (unsigned n = 2; fs::exists(destination, error); ++n) {
            const auto suffix = "_" + std::to_string(n);
            destination = folder / from_utf8(stem.substr(0, 31 - std::min<size_t>(31, suffix.size() + extension.size())) +
                                             suffix + extension);
        }
        break;
    }
    }
    fs::create_directories(destination.parent_path());
    const auto temporary = fs::path(destination).concat(".tmp");
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Cannot write " + destination.string());
        write(out, destination.string());
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write " + destination.string() + "; the disk may be full");
    } catch (...) {
        fs::remove(temporary, error);
        throw;
    }
    for (const auto &old : replaced)
        if (old != destination)
            fs::remove(old, error);
    fs::rename(temporary, destination);
    std::ofstream log(mod / "imports.txt", std::ios::app);
    log << content_name(target.kind) << '\t' << utf8(fs::relative(destination, mod)) << '\t' << target.source << '\n';
    return destination;
}
void check(std::ostream &out, const std::string &name) {
    if (!out)
        throw std::runtime_error("Cannot write " + name + "; the disk may be full");
}
}

SourceDisc::Range SourceDisc::range(const std::string &path, std::optional<size_t> index) {
    const auto location = locate(path);
    if (!index)
        return {location.offset, location.size};
    const auto &list = entries(path);
    if (*index >= list.size())
        throw std::runtime_error(path + " has no entry " + std::to_string(*index));
    const auto &entry = list[*index];
    if (entry.offset > location.size || entry.size > location.size - entry.offset)
        throw std::runtime_error(path + " entry " + std::to_string(*index) + " lies outside the archive");
    return {location.offset + entry.offset, entry.size};
}

void SourceDisc::copy(const Range &range, std::ostream &out, const Progress &progress) {
    std::vector<uint8_t> buffer(size_t(std::min<uint64_t>(range.size, chunk)));
    for (uint64_t done = 0; done < range.size;) {
        const auto bytes = size_t(std::min<uint64_t>(chunk, range.size - done));
        read(range.offset + done, buffer.data(), bytes);
        out.write(reinterpret_cast<const char *>(buffer.data()), std::streamsize(bytes));
        if (!out)
            throw std::runtime_error("Cannot write the copy; the disk may be full");
        done += bytes;
        if (progress)
            progress(bytes);
    }
}

fs::path import_bytes(const fs::path &mod, const ImportTarget &target, const std::vector<uint8_t> &bytes) {
    return store(mod, target, [&](std::ostream &out, const std::string &name) {
        out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        check(out, name);
    });
}

fs::path import_from_disc(SourceDisc &disc, const std::string &path, std::optional<size_t> entry, const fs::path &mod,
                          const ImportTarget &target, const Progress &progress) {
    const auto range = disc.range(path, entry);
    return store(mod, target, [&](std::ostream &out, const std::string &) { disc.copy(range, out, progress); });
}

fs::path import_host_file(const fs::path &mod, const ImportTarget &target, const fs::path &source,
                          const Progress &progress) {
    std::ifstream in(source, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open " + source.string());
    auto named = target;
    if (named.name.empty())
        named.name = utf8(source.filename());
    if (named.source.empty())
        named.source = utf8(source);
    return store(mod, named, [&](std::ostream &out, const std::string &name) {
        std::vector<char> buffer(chunk);
        while (in) {
            in.read(buffer.data(), std::streamsize(buffer.size()));
            const auto bytes = size_t(in.gcount());
            if (!bytes)
                break;
            out.write(buffer.data(), std::streamsize(bytes));
            check(out, name);
            if (progress)
                progress(bytes);
        }
        if (in.bad())
            throw std::runtime_error("Cannot read " + source.string());
    });
}

std::vector<ImportRecord> read_imports(const fs::path &mod) {
    std::vector<ImportRecord> records;
    std::ifstream in(mod / "imports.txt");
    for (std::string line; std::getline(in, line);) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (second == std::string::npos)
            continue;
        records.push_back({line.substr(0, first), line.substr(first + 1, second - first - 1), line.substr(second + 1)});
    }
    return records;
}

}
