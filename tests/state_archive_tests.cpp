// SPDX-License-Identifier: GPL-3.0-only
#include "state_archive.h"
#include <iostream>

namespace {
void require(bool passed, const char *message) {
    if (!passed)
        throw std::runtime_error(message);
}

struct Thread {
    int id = 0;
    std::string name;
    std::vector<uint32_t> stack;
    template <typename Archive> void serializeState(Archive &archive) { archive(id, name, stack); }
};

struct Machine {
    uint32_t pc = 0;
    std::array<uint64_t, 4> regs{};
    std::atomic<int> tick{0};
    std::vector<bool> flags;
    std::deque<int> queue;
    std::map<int, Thread> threads;
    std::unordered_map<uint32_t, std::string> names;
    std::set<int> ready;
    std::optional<double> deadline;
    std::variant<std::monostate, int, std::string> pending;
    std::pair<int, float> pair{};

    void serializeState(StateArchive &archive) {
        archive.section(0x54534554u);
        archive(pc, regs, tick, flags, queue, threads, names, ready, deadline, pending, pair);
    }
};

void round_trip() {
    Machine a;
    a.pc = 0x1234;
    a.regs = {1, 2, 3, 4};
    a.tick = 77;
    a.flags = {true, false, true};
    a.queue = {5, 6};
    a.threads[3] = Thread{3, "main", {9, 8, 7}};
    a.names[0x10] = "idle";
    a.ready = {1, 3};
    a.deadline = 2.5;
    a.pending = std::string("wait");
    a.pair = {4, 0.5f};

    std::vector<uint8_t> data;
    StateArchive saving(data);
    saving(a);

    Machine b;
    b.deadline.reset();
    b.queue = {1, 2, 3, 4};
    StateArchive loading(static_cast<const std::vector<uint8_t> &>(data));
    loading(b);
    require(loading.finished(), "whole archive read");
    require(b.pc == 0x1234 && b.regs[3] == 4 && b.tick == 77, "plain values");
    require(b.flags == std::vector<bool>({true, false, true}), "bool vector");
    require(b.queue == std::deque<int>({5, 6}), "deque replaced");
    require(b.threads.size() == 1 && b.threads[3].name == "main" && b.threads[3].stack.size() == 3, "map of objects");
    require(b.names[0x10] == "idle" && b.ready.count(3), "hash map and set");
    require(b.deadline && *b.deadline == 2.5, "optional");
    require(std::get<std::string>(b.pending) == "wait", "variant");
    require(b.pair.first == 4 && b.pair.second == 0.5f, "pair");

    std::vector<uint8_t> again;
    StateArchive resave(again);
    resave(b);
    require(again == data, "saving a loaded state gives the same bytes");
}

void rejects_bad_data() {
    Machine a;
    std::vector<uint8_t> data;
    StateArchive saving(data);
    saving(a);

    auto truncated = data;
    truncated.resize(truncated.size() / 2);
    bool threw = false;
    try {
        Machine b;
        StateArchive loading(static_cast<const std::vector<uint8_t> &>(truncated));
        loading(b);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "truncated state is rejected");

    auto wrongSection = data;
    wrongSection[0] ^= 0xff;
    threw = false;
    try {
        Machine b;
        StateArchive loading(static_cast<const std::vector<uint8_t> &>(wrongSection));
        loading(b);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw, "section mismatch is rejected");
}
}

int main() {
    try {
        round_trip();
        rejects_bad_data();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "state archive tests passed\n";
    return 0;
}
