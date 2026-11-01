// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class EeScheduler;

namespace openkai2 {

struct MachineStateResult {
    bool ok = false;
    std::string message;
    std::vector<uint8_t> data;
};

void set_machine_state_identity(std::string identity);
const std::string &machine_state_identity();

void request_machine_save(std::function<void(MachineStateResult)> done);
void request_machine_load(std::vector<uint8_t> data, std::function<void(MachineStateResult)> done);
bool machine_state_pending();
void machine_state_point(EeScheduler &scheduler);
void set_machine_memory(const uint8_t *rdram, size_t bytes);

std::vector<uint8_t> wrap_machine_state(const std::vector<uint8_t> &body);
bool unwrap_machine_state(const std::vector<uint8_t> &file, std::vector<uint8_t> &body, std::string &problem);

std::filesystem::path machine_state_file(unsigned slot);
}
