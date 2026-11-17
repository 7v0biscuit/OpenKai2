// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include "ed25519.h"

namespace openkai2::identity {

inline constexpr char developer_name[] = "[DEV] Biscuit";

enum class Tag { developer, twitch, youtube, tob };
inline constexpr Tag all_tags[] = {Tag::developer, Tag::twitch, Tag::youtube, Tag::tob};
std::string tag_label(Tag tag);
std::optional<Tag> claimed_tag(const std::string &name);
std::string without_tags(const std::string &name);
std::optional<crypto::Seed> tag_seed(Tag tag);
std::optional<Tag> install_tag_key(const std::filesystem::path &source);
std::string tag_proof(const std::string &name, const std::string &context);
std::string held_name(const std::string &name);
std::string renamed(const std::string &current, const std::string &typed);
bool verify_tag_claim(Tag tag, const std::string &name, const std::string &context, const crypto::Signature &signature);

bool claims_developer(const std::string &name);
std::string without_developer_tag(const std::string &name);
std::string allowed_name(const std::string &name, bool developer);

const crypto::PublicKey &developer_key();
std::optional<crypto::Seed> read_developer_key(const std::filesystem::path &file);
std::filesystem::path developer_key_file();
std::optional<crypto::Seed> developer_seed();
bool install_developer_key(const std::filesystem::path &source);

std::string claim_message(const std::string &name, const std::string &context);
crypto::Signature sign_claim(const crypto::Seed &seed, const std::string &name, const std::string &context);
bool verify_claim(const std::string &name, const std::string &context, const crypto::Signature &signature);
std::string verified_name(const std::string &name, const std::string &context, const std::string &signature_hex);

std::string random_nonce();

}
