// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include "ed25519.h"

namespace openkai2::identity {

inline constexpr char developer_name[] = "[DEV] Biscuit";

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
