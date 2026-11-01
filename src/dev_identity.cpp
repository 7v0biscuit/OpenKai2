// SPDX-License-Identifier: GPL-3.0-only
#include "dev_identity.h"
#include "disc_image.h"
#include <fstream>
#include <random>
#include <vector>

namespace openkai2::identity {
namespace {
constexpr crypto::PublicKey key = {0x51, 0x2e, 0xf2, 0x44, 0x8a, 0x9d, 0x5a, 0xca, 0x93, 0xcf, 0xb9,
                                   0xd4, 0x05, 0x24, 0x08, 0x6e, 0x77, 0x27, 0x08, 0x15, 0x0e, 0xdb,
                                   0xad, 0xe5, 0x26, 0x6d, 0x57, 0xb1, 0x48, 0x11, 0xbe, 0x7c};

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

bool tag_at(const std::vector<Unit> &list, size_t i) {
    if (i + 4 >= list.size() || list[i].token != '[' || list[i + 4].token != ']')
        return false;
    const char want[] = {'D', 'E', 'V'};
    bool ascii = false;
    for (size_t k = 0; k < 3; ++k) {
        const char token = list[i + 1 + k].token;
        if (token == want[k])
            ascii = true;
        else if (token != '*')
            return false;
    }
    return ascii;
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
    if (!claims_developer(name))
        return name;
    return developer ? std::string(developer_name) : without_developer_tag(name);
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

std::optional<crypto::Seed> developer_seed() {
    static const auto seed = read_developer_key(developer_key_file());
    return seed;
}

std::string claim_message(const std::string &name, const std::string &context) {
    return "openkai2 developer\n" + name + "\n" + context;
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
    if (!claims_developer(name))
        return name;
    crypto::Signature signature{};
    if (name == developer_name && crypto::from_hex(signature_hex, signature.data(), signature.size()) &&
        verify_claim(name, context, signature))
        return name;
    return without_developer_tag(name);
}

std::string random_nonce() {
    std::random_device device;
    uint8_t bytes[16];
    for (auto &byte : bytes)
        byte = uint8_t(device());
    return crypto::to_hex(bytes, sizeof bytes);
}

}
