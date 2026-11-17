// SPDX-License-Identifier: GPL-3.0-only
#include "dev_identity.h"
#include "disc_image.h"
#include <fstream>
#include <string>
#include <mutex>
#include <random>
#include <vector>

namespace openkai2::identity {
namespace {
constexpr crypto::PublicKey key = {0x51, 0x2e, 0xf2, 0x44, 0x8a, 0x9d, 0x5a, 0xca, 0x93, 0xcf, 0xb9,
                                   0xd4, 0x05, 0x24, 0x08, 0x6e, 0x77, 0x27, 0x08, 0x15, 0x0e, 0xdb,
                                   0xad, 0xe5, 0x26, 0x6d, 0x57, 0xb1, 0x48, 0x11, 0xbe, 0x7c};

constexpr crypto::PublicKey twitch_key = {0xb8, 0xf8, 0x67, 0x90, 0xa0, 0xc0, 0x3e, 0x86, 0xd5, 0xd3, 0x85, 0xb6, 0x8c, 0xc5, 0x3b, 0xba, 0xe8, 0x4b, 0x0b, 0x95, 0x86, 0xe3, 0x2b, 0xd4, 0x8e, 0x7c, 0x7f, 0x79, 0x9e, 0xac, 0xc9, 0xb1};
constexpr crypto::PublicKey youtube_key = {0x85, 0x73, 0x70, 0xd6, 0x99, 0x59, 0xc5, 0x23, 0x8f, 0x3e, 0x98, 0x7e, 0xfb, 0x19, 0x55, 0x40, 0x8e, 0x90, 0x0e, 0x50, 0x63, 0x79, 0x90, 0x07, 0x1a, 0xed, 0x80, 0xef, 0xad, 0x5b, 0x79, 0x8f};
constexpr crypto::PublicKey tob_key = {0xb4, 0xe3, 0xa7, 0x58, 0x94, 0x5e, 0xb0, 0x55, 0xd2, 0x9c, 0x41, 0x5a, 0xdf, 0x59, 0xb8, 0x89, 0x27, 0x10, 0x90, 0xae, 0x20, 0x24, 0xd6, 0x18, 0x47, 0x88, 0x59, 0xde, 0xa2, 0x0d, 0xe3, 0x04};

struct TagInfo {
    Tag tag;
    const char *letters, *label, *file;
    const crypto::PublicKey *key;
};
constexpr TagInfo tag_table[] = {{Tag::developer, "DEV", "[DEV]", "dev.key", &key},
                                 {Tag::twitch, "TTV", "[TTV]", "ttv.key", &twitch_key},
                                 {Tag::youtube, "YT", "[YT]", "yt.key", &youtube_key},
                                 {Tag::tob, "TOB", "[ToB]", "tob.key", &tob_key}};
const TagInfo &info(Tag tag) { return tag_table[size_t(tag)]; }

struct Unit {
    size_t begin, end;
    char token;
};

std::vector<Unit> units(const std::string &text) {
    std::vector<Unit> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + length > text.size())
            length = 1;
        uint32_t code = c;
        if (length == 2)
            code = (c & 0x1f) << 6 | (text[i + 1] & 0x3f);
        else if (length == 3)
            code = (c & 0x0f) << 12 | (text[i + 1] & 0x3f) << 6 | (text[i + 2] & 0x3f);
        else if (length == 4)
            code = (c & 0x07) << 18 | (text[i + 1] & 0x3f) << 12 | (text[i + 2] & 0x3f) << 6 | (text[i + 3] & 0x3f);
        char token = 0;
        if (code < 0x80) {
            if (code >= 'a' && code <= 'z')
                token = char(code - 32);
            else if (code >= 'A' && code <= 'Z')
                token = char(code);
            else if (code == '[' || code == '(' || code == '{' || code == '<')
                token = '[';
            else if (code == ']' || code == ')' || code == '}' || code == '>')
                token = ']';
        } else if (code == 0xff3b || code == 0x3010 || code == 0x3014 || code == 0xff08 || code == 0x2329 ||
                   code == 0x27e8) {
            token = '[';
        } else if (code == 0xff3d || code == 0x3011 || code == 0x3015 || code == 0xff09 || code == 0x232a ||
                   code == 0x27e9) {
            token = ']';
        } else if (code >= 0xff21 && code <= 0xff3a) {
            token = char('A' + (code - 0xff21));
        } else if (code >= 0xff41 && code <= 0xff5a) {
            token = char('A' + (code - 0xff41));
        } else if (code > 0x7f && code != 0x200b && code != 0x200c && code != 0x200d && code != 0xfeff &&
                   code != 0x3000 && code != 0x00a0) {
            token = '*';
        }
        if (token)
            out.push_back({i, i + length, token});
        i += length;
    }
    return out;
}

size_t tag_length(const std::vector<Unit> &list, size_t i, const char *want) {
    const size_t letters = std::char_traits<char>::length(want);
    if (i + letters + 1 >= list.size() || list[i].token != '[' || list[i + letters + 1].token != ']')
        return 0;
    bool ascii = false;
    for (size_t k = 0; k < letters; ++k) {
        const char token = list[i + 1 + k].token;
        if (token == want[k])
            ascii = true;
        else if (token != '*')
            return 0;
    }
    return ascii ? letters + 2 : 0;
}

bool tag_at(const std::vector<Unit> &list, size_t i) { return tag_length(list, i, "DEV") != 0; }

std::optional<std::pair<Tag, std::pair<size_t, size_t>>> first_tag(const std::string &text) {
    const auto list = units(text);
    for (size_t i = 0; i < list.size(); ++i)
        for (const auto &entry : tag_table)
            if (const size_t length = tag_length(list, i, entry.letters))
                return std::make_pair(entry.tag, std::make_pair(list[i].begin, list[i + length - 1].end));
    return std::nullopt;
}

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    size_t first = 0;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t'))
        ++first;
    std::string out;
    bool space = false;
    for (size_t i = first; i < text.size(); ++i) {
        if (text[i] == ' ') {
            if (!space)
                out.push_back(' ');
            space = true;
        } else {
            out.push_back(text[i]);
            space = false;
        }
    }
    return out;
}
}

std::string tag_label(Tag tag) { return info(tag).label; }

std::optional<Tag> claimed_tag(const std::string &name) {
    const auto found = first_tag(name);
    if (!found)
        return std::nullopt;
    return found->first;
}

std::string without_tags(const std::string &name) {
    std::string text = name;
    for (int pass = 0; pass < 16; ++pass) {
        const auto found = first_tag(text);
        if (!found)
            break;
        text.erase(found->second.first, found->second.second - found->second.first);
    }
    if (first_tag(text))
        return "Player";
    text = trimmed(text);
    return text.empty() ? "Player" : text;
}

bool claims_developer(const std::string &name) {
    const auto list = units(name);
    for (size_t i = 0; i < list.size(); ++i)
        if (tag_at(list, i))
            return true;
    return false;
}

std::string without_developer_tag(const std::string &name) {
    std::string text = name;
    for (int pass = 0; pass < 8 && claims_developer(text); ++pass) {
        const auto list = units(text);
        for (size_t i = list.size(); i-- > 0;)
            if (tag_at(list, i)) {
                text.erase(list[i].begin, list[i + 4].end - list[i].begin);
                break;
            }
    }
    if (claims_developer(text))
        return "Player";
    text = trimmed(text);
    return text.empty() ? "Player" : text;
}

std::string allowed_name(const std::string &name, bool developer) {
    const auto tag = claimed_tag(name);
    if (!tag)
        return name;
    if (*tag == Tag::developer)
        return developer ? std::string(developer_name) : without_tags(name);
    if (!tag_seed(*tag))
        return without_tags(name);
    return tag_label(*tag) + " " + without_tags(name);
}

std::string held_name(const std::string &name) { return allowed_name(name, tag_seed(Tag::developer).has_value()); }

std::string renamed(const std::string &current, const std::string &typed) {
    const auto keep = claimed_tag(current);
    if (claimed_tag(typed) || !keep || !tag_seed(*keep))
        return held_name(typed);
    if (*keep == Tag::developer)
        return std::string(developer_name);
    return tag_label(*keep) + " " + without_tags(typed);
}

const crypto::PublicKey &developer_key() { return key; }

std::optional<crypto::Seed> read_developer_key(const std::filesystem::path &file) {
    std::ifstream in(file);
    std::string text;
    if (!in || !(in >> text))
        return std::nullopt;
    crypto::Seed seed{};
    if (!crypto::from_hex(text, seed.data(), seed.size()) || crypto::public_key(seed) != key)
        return std::nullopt;
    return seed;
}

std::filesystem::path developer_key_file() { return default_cache_directory().parent_path() / "dev.key"; }

namespace {
std::filesystem::path tag_key_file(Tag tag) { return default_cache_directory().parent_path() / info(tag).file; }
std::optional<crypto::Seed> read_tag_key(const std::filesystem::path &file, Tag tag) {
    std::ifstream in(file);
    std::string text;
    if (!in || !(in >> text))
        return std::nullopt;
    crypto::Seed seed{};
    if (!crypto::from_hex(text, seed.data(), seed.size()) || crypto::public_key(seed) != *info(tag).key)
        return std::nullopt;
    return seed;
}
std::mutex tagMutex;
std::optional<crypto::Seed> tagSeeds[4];
bool tagRead[4] = {};
}

namespace {
std::mutex seedMutex;
std::optional<std::optional<crypto::Seed>> cachedSeed;
}

std::optional<crypto::Seed> developer_seed() {
    std::lock_guard lock(seedMutex);
    if (!cachedSeed)
        cachedSeed = read_developer_key(developer_key_file());
    return *cachedSeed;
}

bool install_developer_key(const std::filesystem::path &source) {
    const auto seed = read_developer_key(source);
    if (!seed)
        return false;
    const auto target = developer_key_file();
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    {
        std::ofstream out(target, std::ios::trunc);
        out << crypto::to_hex(seed->data(), seed->size()) << '\n';
        if (!out)
            return false;
    }
    std::lock_guard lock(seedMutex);
    cachedSeed = seed;
    return true;
}

std::optional<crypto::Seed> tag_seed(Tag tag) {
    if (tag == Tag::developer)
        return developer_seed();
    std::lock_guard lock(tagMutex);
    const size_t index = size_t(tag);
    if (!tagRead[index]) {
        tagSeeds[index] = read_tag_key(tag_key_file(tag), tag);
        tagRead[index] = true;
    }
    return tagSeeds[index];
}

std::optional<Tag> install_tag_key(const std::filesystem::path &source) {
    for (const auto &entry : tag_table) {
        if (entry.tag == Tag::developer) {
            if (install_developer_key(source))
                return Tag::developer;
            continue;
        }
        const auto seed = read_tag_key(source, entry.tag);
        if (!seed)
            continue;
        const auto target = tag_key_file(entry.tag);
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        {
            std::ofstream out(target, std::ios::trunc);
            out << crypto::to_hex(seed->data(), seed->size()) << '\n';
            if (!out)
                return std::nullopt;
        }
        std::lock_guard lock(tagMutex);
        tagSeeds[size_t(entry.tag)] = seed;
        tagRead[size_t(entry.tag)] = true;
        return entry.tag;
    }
    return std::nullopt;
}

namespace {
std::string tag_message(Tag tag, const std::string &name, const std::string &context) {
    if (tag == Tag::developer)
        return "openkai2 developer\n" + name + "\n" + context;
    return std::string("openkai2 tag ") + info(tag).letters + "\n" + name + "\n" + context;
}
}

bool verify_tag_claim(Tag tag, const std::string &name, const std::string &context, const crypto::Signature &signature) {
    const auto message = tag_message(tag, name, context);
    return crypto::verify(*info(tag).key, reinterpret_cast<const uint8_t *>(message.data()), message.size(), signature);
}

std::string tag_proof(const std::string &name, const std::string &context) {
    const auto tag = claimed_tag(name);
    if (!tag)
        return {};
    const auto seed = tag_seed(*tag);
    if (!seed)
        return {};
    const auto message = tag_message(*tag, name, context);
    const auto signature = crypto::sign(*seed, reinterpret_cast<const uint8_t *>(message.data()), message.size());
    return crypto::to_hex(signature.data(), signature.size());
}

std::string claim_message(const std::string &name, const std::string &context) {
    return tag_message(Tag::developer, name, context);
}

crypto::Signature sign_claim(const crypto::Seed &seed, const std::string &name, const std::string &context) {
    const auto message = claim_message(name, context);
    return crypto::sign(seed, reinterpret_cast<const uint8_t *>(message.data()), message.size());
}

bool verify_claim(const std::string &name, const std::string &context, const crypto::Signature &signature) {
    const auto message = claim_message(name, context);
    return crypto::verify(key, reinterpret_cast<const uint8_t *>(message.data()), message.size(), signature);
}

std::string verified_name(const std::string &name, const std::string &context, const std::string &signature_hex) {
    const auto tag = claimed_tag(name);
    if (!tag)
        return name;
    crypto::Signature signature{};
    const bool parsed = crypto::from_hex(signature_hex, signature.data(), signature.size());
    if (*tag == Tag::developer) {
        if (name == developer_name && parsed && verify_claim(name, context, signature))
            return name;
        return without_tags(name);
    }
    if (parsed && name == tag_label(*tag) + " " + without_tags(name) && verify_tag_claim(*tag, name, context, signature))
        return name;
    return without_tags(name);
}

std::string random_nonce() {
    std::random_device device;
    uint8_t bytes[16];
    for (auto &byte : bytes)
        byte = uint8_t(device());
    return crypto::to_hex(bytes, sizeof bytes);
}

}
