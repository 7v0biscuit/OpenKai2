// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "netplay.h"

class PS2Runtime;

namespace openkai2::netplay {

struct Arguments {
    bool enabled = false, host = true;
    std::string address;
    uint16_t port = default_port;
    unsigned delay = 2;
    std::string name = "Player";
    std::string password, lobby, room;
};

bool parse_argument(const std::string &argument, Arguments &arguments);
void prepare_memory_card(const Arguments &arguments);
std::unique_ptr<Session> start_game_session(PS2Runtime &runtime, const Arguments &arguments, const std::string &identity);
void stop_game_session(std::unique_ptr<Session> &session);

}
