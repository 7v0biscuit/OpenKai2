// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class EeScheduler;
class StateArchive;

namespace openkai2 {

struct MachineStateResult {
    bool ok = false;
    std::string message;
    std::vector<uint8_t> data;
};

void add_machine_state_part(std::function<void(StateArchive &)> part);
void serialize_machine(EeScheduler &scheduler, StateArchive &archive);
void set_machine_state_identity(std::string identity);
const std::string &machine_state_identity();

enum class MachineStatePurpose { manual, automatic_reset };
void enable_manual_machine_states(bool enabled);
void request_machine_save(std::function<void(MachineStateResult)> done,
                          MachineStatePurpose purpose = MachineStatePurpose::manual);
void request_machine_load(std::vector<uint8_t> data, std::function<void(MachineStateResult)> done,
                          MachineStatePurpose purpose = MachineStatePurpose::manual);
bool machine_state_pending();
void machine_state_point(EeScheduler &scheduler);
void set_machine_memory(const uint8_t *rdram, size_t bytes);
uint64_t machine_memory_hash();

std::vector<uint8_t> wrap_machine_state(const std::vector<uint8_t> &body);
bool unwrap_machine_state(const std::vector<uint8_t> &file, std::vector<uint8_t> &body, std::string &problem);

std::filesystem::path machine_state_file(unsigned slot);
}
