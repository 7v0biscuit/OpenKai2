// SPDX-License-Identifier: GPL-3.0-only
#include "dev_identity.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace openkai2;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

void rfc8032_vector() {
    crypto::Seed seed{};
    crypto::from_hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed.data(), 32);
    const auto key = crypto::public_key(seed);
    require(crypto::to_hex(key.data(), 32) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
            "RFC 8032 public key");
    const auto signature = crypto::sign(seed, nullptr, 0);
    require(crypto::to_hex(signature.data(), 64) ==
                "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
            "RFC 8032 signature");
    require(crypto::verify(key, nullptr, 0, signature), "RFC 8032 signature verifies");
    auto broken = signature;
    broken[40] ^= 4;
    require(!crypto::verify(key, nullptr, 0, broken), "a changed signature fails");
}

void tags() {
    namespace id = identity;
    for (const char *name : {"Joint [DEV]", "Biscuit [dev]", "[ D E V ] Goku", "Vegeta (DEV)", "Gohan {Dev}",
                             "Krillin \xef\xbc\xbb" "DEV\xef\xbc\xbd", "Piccolo [D\xd0\x95V]", "Trunks [DEV ]",
                             "\xe3\x80\x90" "dev\xe3\x80\x91" "Goten", "Yamcha <DEV>"})
        require(id::claims_developer(name), std::string("tag found in ") + name);
    for (const char *name : {"Dev Joe", "Developer", "[DEVIL]", "Goku", "D E V", "[DE]V", "[\xd0\x94\xd0\x95\xd0\x92]"})
        require(!id::claims_developer(name), std::string("no tag in ") + name);
    require(id::without_developer_tag("Joint [DEV]") == "Joint", "tag removed");
    require(id::without_developer_tag("[DEV]") == "Player", "empty name falls back");
    require(id::without_developer_tag("A [DEV] B (dev)") == "A B", "every tag removed");
    require(id::allowed_name("Joint [DEV]", true) == "[DEV] Biscuit", "developer profile is renamed");
    require(id::allowed_name("Joint [DEV]", false) == "Joint", "others lose the tag");
    require(id::allowed_name("Goku", false) == "Goku", "plain names stay");
    require(id::claimed_tag("[TTV] Joint") == id::Tag::twitch && id::claimed_tag("[yt]Joint") == id::Tag::youtube &&
                id::claimed_tag("(ToB) Joint") == id::Tag::tob && id::claimed_tag("[DEV] Joint") == id::Tag::developer,
            "every name tag is recognised in any case and bracket");
    require(!id::claimed_tag("[TV] Joint") && !id::claimed_tag("TTV Joint") && !id::claimed_tag("[TOBY] Joint"),
            "other bracketed words are not tags");
    require(!id::claims_developer("[TTV] Joint"), "stream tags are not the developer tag");
    require(id::without_tags("[TTV] Joint [YT]") == "Joint" && id::without_tags("[ToB]") == "Player", "every tag is removed");
    require(id::tag_label(id::Tag::tob) == "[ToB]" && id::tag_label(id::Tag::youtube) == "[YT]", "tag labels");
    if (!id::tag_seed(id::Tag::twitch)) {
        require(id::allowed_name("[TTV] Joint", false) == "Joint", "a stream tag needs its key");
        require(id::renamed("[TTV] Biscuit", "Joint") == "Joint", "a rename without the key keeps no tag");
    }
}

void claims() {
    namespace id = identity;
    crypto::Seed fake{};
    for (size_t i = 0; i < fake.size(); ++i)
        fake[i] = uint8_t(i * 7 + 1);
    const auto forged = id::sign_claim(fake, id::developer_name, "nonce-1");
    require(!id::verify_claim(id::developer_name, "nonce-1", forged), "a claim from another key is refused");
    require(id::verified_name(id::developer_name, "nonce-1", crypto::to_hex(forged.data(), 64)) == "Biscuit",
            "a forged developer name loses its tag");
    require(id::verified_name("Goku", "nonce-1", "") == "Goku", "plain names need no proof");
    crypto::Signature signature{};
    std::fill(signature.begin(), signature.end(), uint8_t(3));
    require(!id::verify_tag_claim(id::Tag::twitch, "[TTV] Joint", "nonce-1", signature), "a forged stream tag claim is refused");
    require(id::verified_name("[TTV] Joint", "nonce-1", crypto::to_hex(signature.data(), 64)) == "Joint",
            "a forged stream tag is removed");
    require(id::verified_name("[YT] Joint", "nonce-1", "") == "Joint", "a stream tag without proof is removed");
    require(id::tag_proof("[TTV] Joint", "nonce-1").empty() || id::tag_seed(id::Tag::twitch).has_value(),
            "only a stored key signs a stream tag");
    if (const char *file = std::getenv("OPENKAI2_DEV_KEY_FILE"); file && *file) {
        const auto seed = id::read_developer_key(file);
        require(seed.has_value(), "the developer key matches the built-in public key");
        const auto real = id::sign_claim(*seed, id::developer_name, "nonce-2");
        require(id::verified_name(id::developer_name, "nonce-2", crypto::to_hex(real.data(), 64)) == "[DEV] Biscuit",
                "the developer's claim is accepted");
        require(id::verified_name(id::developer_name, "nonce-3", crypto::to_hex(real.data(), 64)) == "Biscuit",
                "a replayed claim is refused");
        std::cout << "developer key checked\n";
    }
}
}

int main() {
    try {
        rfc8032_vector();
        tags();
        claims();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "identity tests passed\n";
    return 0;
}
