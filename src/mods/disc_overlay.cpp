// SPDX-License-Identifier: GPL-3.0-only
#include "mods/disc_overlay.h"
#include "mods/afs.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace openkai2::mods {
namespace fs = std::filesystem;
namespace {
constexpr uint64_t sector_bytes = 2048u;
uint64_t align(uint64_t value) { return (value + sector_bytes - 1u) / sector_bytes * sector_bytes; }
std::vector<uint8_t> le32(uint32_t value) {
    return {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
}
// ISO 9660 stores 32-bit fields little-endian then big-endian.
std::vector<uint8_t> both32(uint32_t value) {
    auto bytes = le32(value);
    bytes.insert(bytes.end(), {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)});
    return bytes;
}
std::string utf8(const fs::path &path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
bool leading_index(const std::string &name, uint32_t &index) {
    size_t digits = 0;
    while (digits < name.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(name[digits])))
        ++digits;
    if (!digits || (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))))
        return false;
    index = uint32_t(std::stoul(name.substr(0, digits)));
    return true;
}
std::vector<fs::path> files_under(const fs::path &root) {
    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::is_directory(root, error))
        return files;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    return files;
}
} // namespace

struct DiscOverlay::Content {
    struct Part {
        uint64_t length;
        Piece piece;
    };
    std::vector<Part> parts;
    uint64_t size = 0;
    void append(uint64_t length, Piece piece) {
        if (!length)
            return;
        parts.push_back({length, std::move(piece)});
        size += length;
    }
    void pad(uint64_t to) {
        if (to > size)
            append(to - size, Piece{});
    }
    Content slice(uint64_t offset, uint64_t length) const {
        Content result;
        uint64_t at = 0;
        for (const auto &part : parts) {
            const uint64_t end = at + part.length;
            if (end > offset && at < offset + length) {
                const uint64_t from = std::max(at, offset), to = std::min(end, offset + length);
                Piece piece = part.piece;
                if (piece.kind != Piece::Kind::Zero)
                    piece.source += from - at;
                result.append(to - from, piece);
            }
            at = end;
        }
        return result;
    }
};

DiscOverlay &DiscOverlay::global() {
    static DiscOverlay overlay;
    return overlay;
}

void DiscOverlay::clear() {
    std::lock_guard lock(mutex_);
    active_ = false;
    image_.clear();
    image_size_ = size_ = 0;
    segments_.clear();
    entries_.clear();
    files_.clear();
    checked_.clear();
    checked_matches_ = false;
    file_streams_.clear();
    if (image_stream_.is_open())
        image_stream_.close();
}

bool DiscOverlay::active_for(const fs::path &image) const {
    std::lock_guard lock(mutex_);
    if (!active_ || image.empty())
        return false;
    if (image != checked_) {
        std::error_code error;
        checked_ = image;
        checked_matches_ = image == image_ || fs::equivalent(image, image_, error);
    }
    return checked_matches_;
}

const IsoEntry *DiscOverlay::find(const fs::path &image, const std::string &key) const {
    if (!active_for(image))
        return nullptr;
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

size_t DiscOverlay::add_file(const fs::path &file) {
    files_.push_back(file);
    file_streams_.emplace_back();
    return files_.size() - 1u;
}

void DiscOverlay::assign(uint64_t start, uint64_t length, Piece piece) {
    if (!length)
        return;
    const uint64_t end = start + length;
    const auto advanced = [](Piece p, uint64_t by) {
        if (p.kind != Piece::Kind::Zero)
            p.source += by;
        return p;
    };
    auto it = segments_.lower_bound(start);
    if (it != segments_.begin()) {
        auto previous = std::prev(it);
        const uint64_t previousEnd = previous->first + previous->second.length;
        if (previousEnd > start) {
            if (previousEnd > end)
                segments_.emplace(end, Segment{previousEnd - end, advanced(previous->second.piece, end - previous->first)});
            previous->second.length = start - previous->first;
        }
    }
    for (it = segments_.lower_bound(start); it != segments_.end() && it->first < end;) {
        const uint64_t segmentEnd = it->first + it->second.length;
        if (segmentEnd > end)
            segments_.emplace(end, Segment{segmentEnd - end, advanced(it->second.piece, end - it->first)});
        it = segments_.erase(it);
    }
    segments_.emplace(start, Segment{length, std::move(piece)});
}

void DiscOverlay::patch(uint64_t offset, std::vector<uint8_t> bytes) {
    const auto length = bytes.size();
    Piece piece;
    piece.kind = Piece::Kind::Memory;
    piece.memory = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    assign(offset, length, std::move(piece));
}

void DiscOverlay::patch_record(const IsoEntry &entry, uint32_t sector, uint64_t size) {
    if (!entry.record_offset)
        throw std::runtime_error("The disc root cannot be replaced");
    patch(entry.record_offset + 2u, both32(sector));
    patch(entry.record_offset + 10u, both32(uint32_t(size)));
}

bool DiscOverlay::read_piece(const Piece &piece, uint64_t offset, uint8_t *destination, size_t bytes) const {
    switch (piece.kind) {
    case Piece::Kind::Zero:
        std::memset(destination, 0, bytes);
        return true;
    case Piece::Kind::Memory:
        if (offset + bytes > piece.memory->size())
            return false;
        std::memcpy(destination, piece.memory->data() + offset, bytes);
        return true;
    case Piece::Kind::Image:
    case Piece::Kind::File: {
        std::ifstream *stream = &image_stream_;
        if (piece.kind == Piece::Kind::File) {
            auto &slot = file_streams_[piece.file];
            if (!slot)
                slot = std::make_unique<std::ifstream>(files_[piece.file], std::ios::binary);
            stream = slot.get();
        } else if (!image_stream_.is_open()) {
            image_stream_.open(image_, std::ios::binary);
        }
        stream->clear();
        stream->seekg(std::streamoff(offset));
        stream->read(reinterpret_cast<char *>(destination), std::streamsize(bytes));
        return size_t(stream->gcount()) == bytes;
    }
    }
    return false;
}

bool DiscOverlay::read(uint64_t offset, void *destination, size_t bytes) const {
    std::lock_guard lock(mutex_);
    if (!active_ || offset > size_ || bytes > size_ - offset)
        return false;
    auto *out = static_cast<uint8_t *>(destination);
    const uint64_t end = offset + bytes;
    for (uint64_t at = offset; at < end;) {
        auto next = segments_.upper_bound(at);
        if (next != segments_.begin()) {
            const auto current = std::prev(next);
            const uint64_t segmentEnd = current->first + current->second.length;
            if (at < segmentEnd) {
                const uint64_t count = std::min(end, segmentEnd) - at;
                if (!read_piece(current->second.piece, current->second.piece.source + (at - current->first), out + (at - offset), size_t(count)))
                    return false;
                at += count;
                continue;
            }
        }
        // Between segments the original image shows through.
        const uint64_t limit = std::min(end, next == segments_.end() ? end : next->first);
        Piece through;
        through.kind = at < image_size_ ? Piece::Kind::Image : Piece::Kind::Zero;
        const uint64_t count = (through.kind == Piece::Kind::Image ? std::min(limit, image_size_) : limit) - at;
        if (!read_piece(through, at, out + (at - offset), size_t(count)))
            return false;
        at += count;
    }
    return true;
}

DiscOverlay::Summary DiscOverlay::configure(const fs::path &image, const std::vector<fs::path> &mods) {
    clear();
    Summary summary;
    if (mods.empty())
        return summary;
    const IsoIndex index(image);
    std::lock_guard lock(mutex_);
    image_ = fs::absolute(image);
    image_size_ = size_ = align(fs::file_size(image_));
    std::map<std::string, const IsoEntry *> byKey;
    for (const auto &entry : index.entries())
        if (!entry.path.empty())
            if (const auto key = IsoIndex::key(utf8(entry.path)))
                byKey.emplace(*key, &entry);

    struct Edits {
        std::optional<size_t> replacement;
        std::map<uint32_t, size_t> entries;
        std::vector<std::pair<std::string, size_t>> added;
    };
    std::map<std::string, Edits> edits;
    const auto disc_file = [&](const fs::path &relative, const fs::path &source) -> std::string {
        const auto key = IsoIndex::key(utf8(relative)).value_or(std::string());
        const auto found = byKey.find(key);
        if (key.empty() || found == byKey.end())
            throw std::runtime_error(utf8(source) + ": " + utf8(relative) + " is not on the game disc");
        if (found->second->directory || found->second->multi_extent)
            throw std::runtime_error(utf8(source) + ": " + utf8(relative) + " cannot be replaced");
        return key;
    };
    for (const auto &mod : mods) {
        for (const auto &file : files_under(mod / "files")) {
            auto &edit = edits[disc_file(fs::relative(file, mod / "files"), file)];
            edit = Edits{};
            edit.replacement = add_file(file);
        }
        std::vector<std::pair<std::string, std::pair<std::string, size_t>>> added;
        for (const auto &file : files_under(mod / "afs")) {
            const bool appended = lower(utf8(file.parent_path().filename())) == "add";
            const auto archive = appended ? file.parent_path().parent_path() : file.parent_path();
            const auto key = disc_file(fs::relative(archive, mod / "afs"), file);
            if (appended) {
                added.push_back({key, {utf8(file.filename()), add_file(file)}});
                continue;
            }
            uint32_t entry = 0;
            if (!leading_index(utf8(file.filename()), entry)) {
                summary.notes.push_back("Skipped " + utf8(file) + ": its name must start with the entry number");
                continue;
            }
            edits[key].entries[entry] = add_file(file);
        }
        for (auto &[key, item] : added)
            edits[key].added.push_back(item);
    }

    const auto file_piece = [](size_t file) {
        Piece piece;
        piece.kind = Piece::Kind::File;
        piece.file = file;
        return piece;
    };
    const auto file_size = [&](size_t file) { return uint64_t(fs::file_size(files_[file])); };
    // Writes a file's new content where it was when it fits, otherwise past
    // the end of the disc, and points its directory record there.
    const auto place = [&](const std::string &key, const IsoEntry &entry, const Content &content) {
        if (content.size > 0xffffffffull)
            throw std::runtime_error(key + " would exceed 4 GiB");
        IsoEntry updated = entry;
        uint64_t start = uint64_t(entry.sector) * sector_bytes;
        if (content.size > align(entry.size)) {
            start = size_;
            size_ = align(start + content.size);
            updated.sector = uint32_t(start / sector_bytes);
            ++summary.relocated_files;
        }
        uint64_t at = start;
        for (const auto &part : content.parts) {
            if (part.piece.kind != Piece::Kind::Image || part.piece.source != at)
                assign(at, part.length, part.piece);
            at += part.length;
        }
        if (updated.sector == entry.sector && content.size < entry.size)
            assign(start + content.size, entry.size - content.size, Piece{});
        updated.size = content.size;
        if (updated.sector != entry.sector || updated.size != entry.size)
            patch_record(entry, updated.sector, updated.size);
        entries_[key] = updated;
    };
    const auto read_content = [&](const Content &content, uint64_t offset, uint8_t *destination, size_t bytes) {
        const auto part = content.slice(offset, bytes);
        if (part.size != bytes)
            throw std::runtime_error("Archive read is out of range");
        size_t done = 0;
        for (const auto &piece : part.parts) {
            if (!read_piece(piece.piece, piece.piece.source, destination + done, size_t(piece.length)))
                throw std::runtime_error("Cannot read mod data");
            done += size_t(piece.length);
        }
    };

    for (const auto &[key, edit] : edits) {
        const IsoEntry &entry = *byKey.at(key);
        Content base;
        if (edit.replacement) {
            base.append(file_size(*edit.replacement), file_piece(*edit.replacement));
            ++summary.replaced_files;
        } else {
            Piece original;
            original.kind = Piece::Kind::Image;
            original.source = uint64_t(entry.sector) * sector_bytes;
            base.append(entry.size, original);
        }
        if (edit.entries.empty() && edit.added.empty()) {
            place(key, entry, base);
            continue;
        }
        const ReadFn reader = [&](uint64_t offset, uint8_t *destination, size_t bytes) {
            read_content(base, offset, destination, bytes);
        };
        if (!is_afs(reader, base.size))
            throw std::runtime_error(key + " is not an AFS archive, so its entries cannot be replaced");
        const auto archive = parse_afs(reader, base.size);
        for (const auto &[index, file] : edit.entries)
            if (index >= archive.entries.size())
                throw std::runtime_error(utf8(files_[file]) + ": " + key + " has no entry " + std::to_string(index) +
                                         "; put new entries in an add folder");
        summary.replaced_entries += edit.entries.size();
        summary.added_entries += edit.added.size();

        // Entries that fit in the space up to the next entry are written in
        // place, so the archive keeps its layout.
        bool inPlace = !edit.replacement && edit.added.empty();
        std::vector<uint64_t> starts;
        for (const auto &item : archive.entries)
            if (item.size)
                starts.push_back(item.offset);
        if (archive.named)
            starts.push_back(archive.name_table_offset);
        std::sort(starts.begin(), starts.end());
        for (const auto &[index, file] : edit.entries) {
            const auto &original = archive.entries[index];
            const auto next = std::upper_bound(starts.begin(), starts.end(), original.offset);
            const uint64_t room = (next == starts.end() ? align(base.size) : *next) - original.offset;
            inPlace = inPlace && original.size && file_size(file) <= room;
        }
        if (inPlace) {
            const uint64_t archiveStart = uint64_t(entry.sector) * sector_bytes;
            for (const auto &[index, file] : edit.entries) {
                const auto &original = archive.entries[index];
                const uint64_t bytes = file_size(file);
                assign(archiveStart + original.offset, bytes, file_piece(file));
                if (bytes < original.size)
                    assign(archiveStart + original.offset + bytes, original.size - bytes, Piece{});
                patch(archiveStart + 12u + uint64_t(index) * 8u, le32(uint32_t(bytes)));
                if (archive.named)
                    patch(archiveStart + archive.name_table_offset + uint64_t(index) * afs_name_bytes + 44u,
                          le32(uint32_t(bytes)));
            }
            entries_[key] = entry;
            continue;
        }

        // Otherwise the archive is rebuilt with every entry, old and new.
        std::vector<AfsEntry> list = archive.entries;
        std::vector<Content> sources;
        for (size_t i = 0; i < list.size(); ++i) {
            const auto replaced = edit.entries.find(uint32_t(i));
            if (replaced != edit.entries.end()) {
                Content source;
                source.append(file_size(replaced->second), file_piece(replaced->second));
                list[i].size = uint32_t(source.size);
                sources.push_back(std::move(source));
            } else {
                sources.push_back(base.slice(list[i].offset, list[i].size));
            }
        }
        for (const auto &[name, file] : edit.added) {
            summary.added[key][lower(name)] = uint32_t(list.size());
            AfsEntry added;
            added.name = name;
            added.size = uint32_t(file_size(file));
            list.push_back(added);
            Content source;
            source.append(added.size, file_piece(file));
            sources.push_back(std::move(source));
        }
        const auto layout = layout_afs(list, archive.named);
        Content rebuilt;
        Piece header;
        header.kind = Piece::Kind::Memory;
        header.memory = std::make_shared<const std::vector<uint8_t>>(layout.header);
        rebuilt.append(layout.header.size(), header);
        for (size_t i = 0; i < list.size(); ++i) {
            rebuilt.pad(layout.offsets[i]);
            for (const auto &part : sources[i].parts)
                rebuilt.append(part.length, part.piece);
        }
        if (archive.named) {
            rebuilt.pad(layout.name_table_offset);
            Piece names;
            names.kind = Piece::Kind::Memory;
            names.memory = std::make_shared<const std::vector<uint8_t>>(layout.name_table);
            rebuilt.append(layout.name_table.size(), names);
        }
        rebuilt.pad(layout.size);
        place(key, entry, rebuilt);
    }
    // The primary volume descriptor records the volume size in sectors.
    if (size_ > image_size_)
        patch(16u * sector_bytes + 80u, both32(uint32_t(size_ / sector_bytes)));
    active_ = !segments_.empty() || !entries_.empty();
    return summary;
}

} // namespace openkai2::mods
