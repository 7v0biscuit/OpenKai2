// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::nat {

std::string local_address();
std::vector<uint8_t> stun_request(const uint8_t transaction[12]);
std::optional<std::string> stun_mapped_address(const std::vector<uint8_t> &reply, const uint8_t transaction[12]);
std::optional<std::string> public_address(std::string &error);

std::string find_tag(const std::string &xml, const std::string &tag, size_t from = 0);
std::optional<std::pair<std::string, std::string>> gateway_service(const std::string &description,
                                                                   const std::string &location);

class PortMapping {
  public:
    ~PortMapping();
    bool open(uint16_t port, std::string &external, std::string &error);
    void close();

  private:
    std::string control_, service_;
    uint16_t port_ = 0;
};

}
