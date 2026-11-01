// SPDX-License-Identifier: GPL-3.0-only
#include "mqtt.h"
#include "online.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char *argv[]) {
    unsigned port = openkai2::mqtt::default_port;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--port=", 7) == 0)
            port = unsigned(std::strtoul(argv[i] + 7, nullptr, 10));
        else {
            std::cout << "Usage: OpenKai2Lobby [--port=" << openkai2::mqtt::default_port << "]\n"
                      << "Runs a small online service for OpenKai2 rooms, players and invites (an MQTT broker).\n";
            return argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) ? 0 : 2;
        }
    }
    if (port < 1 || port > 65535) {
        std::cerr << "Choose a port between 1 and 65535\n";
        return 2;
    }
    try {
        const std::atomic<bool> stopping{false};
        openkai2::mqtt::serve(uint16_t(port), stopping, {}, &openkai2::online::allow_service_publish);
    } catch (const std::exception &error) {
        std::cerr << "[lobby] " << error.what() << '\n';
        return 1;
    }
    return 0;
}
