// SPDX-License-Identifier: GPL-3.0-only
#include "delta_states.h"
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>

using openkai2::DeltaStates;

namespace {
void require(bool good, const char *why) {
    if (!good)
        throw std::runtime_error(why);
}

struct Machine {
    static constexpr size_t ramBytes = 1u << 20, videoBytes = 300000u;
    std::unique_ptr<uint8_t[]> ram{new uint8_t[ramBytes]()}, video{new uint8_t[videoBytes]()};
    std::vector<uint8_t> stream = std::vector<uint8_t>(90000u, 7);
    std::vector<uint32_t> queue;
    uint64_t counter = 0;

    void serialize(StateArchive &archive) {
        archive.section(0x314d5454u);
        archive.bytes(ram.get(), ramBytes);
        archive(counter);
        archive(stream);
        archive.bytes(video.get(), videoBytes);
        archive(queue);
        archive.section(0x324d5454u);
    }
    std::vector<uint8_t> snapshot() {
        std::vector<uint8_t> out;
        StateArchive archive(out);
        serialize(archive);
        return out;
    }
    void step(std::mt19937 &random) {
        ++counter;
        for (unsigned i = 0, writes = random() % 40u; i < writes; ++i)
            ram[random() % ramBytes] = uint8_t(random());
        if (random() % 5u == 0)
            for (size_t i = random() % 5000u, end = std::min<size_t>(i + random() % 30000u, ramBytes); i < end; ++i)
                ram[i] = uint8_t(random());
        if (random() % 7u == 0)
            std::memset(video.get(), int(random() & 255u), videoBytes);
        else
            video[random() % videoBytes] ^= 1u;
        if (random() % 6u == 0) {
            std::vector<uint8_t> next(70000u + random() % 200000u);
            for (size_t i = 0; i < next.size(); i += 97u)
                next[i] = uint8_t(random());
            stream.swap(next);
        } else if (!stream.empty())
            stream[random() % stream.size()] = uint8_t(random());
        if (random() % 3u == 0)
            queue.push_back(uint32_t(random()));
        if (queue.size() > 50u)
            queue.clear();
    }
};

void randomRollbacks(unsigned seed, size_t capacity, unsigned helpers = 0) {
    std::mt19937 random(seed);
    Machine machine;
    DeltaStates states(capacity, helpers);
    std::map<uint64_t, std::vector<uint8_t>> reference;
    auto serialize = [&](StateArchive &archive) { machine.serialize(archive); };
    uint64_t id = 0;
    size_t rollbacks = 0, deepest = 0, pages = 0, captures = 0;
    for (unsigned turn = 0; turn < 4000u; ++turn) {
        machine.step(random);
        ++id;
        states.capture(id, serialize);
        pages += states.stats().pages;
        ++captures;
        reference[id] = machine.snapshot();
        while (reference.size() > capacity)
            reference.erase(reference.begin());
        require(states.size() == reference.size() && states.oldest() == reference.begin()->first && states.newest() == id,
                "The retained rollback frames are wrong");
        if (random() % 4u != 0)
            continue;
        if (random() % 2u)
            machine.step(random);
        const uint64_t target = states.oldest() + random() % (states.newest() - states.oldest() + 1u);
        require(!states.restore(states.oldest() - 1u, serialize) && !states.restore(id + 5u, serialize),
                "A frame that was never kept was restored");
        require(states.restore(target, serialize), "A kept frame could not be restored");
        require(machine.snapshot() == reference[target], "Rolling back did not reproduce the saved frame");
        deepest = std::max<size_t>(deepest, size_t(id - target));
        reference.erase(reference.upper_bound(target), reference.end());
        id = target;
        ++rollbacks;
    }
    std::cout << "seed=" << seed << " capacity=" << capacity << " rollbacks=" << rollbacks << " deepest=" << deepest
              << " pages_per_frame=" << pages / captures << '\n';
    require(rollbacks > 500 && deepest + 1u >= capacity, "The rollback test did not cover the retained depth");
}

void changedPagesOnly() {
    Machine machine;
    DeltaStates states(4);
    auto serialize = [&](StateArchive &archive) { machine.serialize(archive); };
    states.capture(1, serialize);
    require(states.stats().pages == 0 && states.stats().small < 200000u, "The first frame copied tracked memory into its record");
    machine.ram[5 * DeltaStates::page + 3] = 9;
    machine.ram[5 * DeltaStates::page + 900] = 9;
    machine.ram[77 * DeltaStates::page] = 1;
    states.capture(2, serialize);
    require(states.stats().pages == 2, "A frame stored pages that did not change");
    const auto before = machine.snapshot();
    states.capture(3, serialize);
    require(states.stats().pages == 0, "An unchanged frame stored pages");
    machine.video[0] = 200;
    require(states.restore(3, serialize) && machine.snapshot() == before, "Changes made after the last frame were kept");
    require(states.restore(1, serialize) && machine.ram[77 * DeltaStates::page] == 0 && states.newest() == 1,
            "Rolling back two frames left newer memory in place");
    bool refused = false;
    try { states.capture(1, serialize); } catch (const std::exception &) { refused = true; }
    require(refused, "A frame was captured out of order");
}
}

int main() {
    try {
        changedPagesOnly();
        for (unsigned seed : {1u, 2u, 3u})
            randomRollbacks(seed, 8);
        randomRollbacks(9u, 1);
        randomRollbacks(11u, 16);
        randomRollbacks(21u, 8, 3);
        randomRollbacks(22u, 6, 1);
        std::cout << "Rollback state checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
