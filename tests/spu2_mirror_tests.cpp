// SPDX-License-Identifier: GPL-3.0-only
#include "audio/spu2_mirror.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
constexpr uint32_t spu2 = 0x1F900000u, tick = 768u;
double energy(const std::vector<int16_t> &samples) {
    double sum = 0.0;
    for (auto value : samples)
        sum += double(value) * value;
    return samples.empty() ? 0.0 : std::sqrt(sum / samples.size());
}
}

int main() {
    try {
        std::vector<uint8_t> iop(2u * 1024u * 1024u);
        uint64_t now = 0;
        openkai2::spu2::reset(iop.data(), now);
        std::vector<int16_t> out;
        const auto run = [&](unsigned samples) {
            now += uint64_t(samples) * tick;
            openkai2::spu2::advance(now);
            openkai2::spu2::take_output(out);
        };
        const auto write = [&](uint32_t reg, uint16_t value) { openkai2::spu2::write16(spu2 + reg, value, now); };
        run(480u);
        require(out.size() == 960u, "SPU2 did not mix one stereo frame per 768 IOP cycles");
        require(energy(out) == 0.0, "An idle SPU2 produced sound");

        uint8_t blocks[32]{};
        blocks[1] = 0x04;
        blocks[17] = 0x03;
        for (unsigned i = 2; i < 16; ++i)
            blocks[i] = blocks[i + 16] = i < 9 ? 0x77 : 0x88;
        std::memcpy(iop.data() + 0x10000u, blocks, sizeof(blocks));
        write(0x1A8, 0x0000);
        write(0x1AA, 0x5000);
        openkai2::spu2::dma_write(0, 0x10000u, sizeof(blocks), now);
        openkai2::spu2::dma_complete(0, now);

        write(0x198, 0x0FF0);
        write(0x598, 0x0FFC);
        for (uint32_t core : {0x760u, 0x788u}) {
            write(core, 0x3FFF);
            write(core + 2u, 0x3FFF);
        }
        write(0x000, 0x3FFF);
        write(0x002, 0x3FFF);
        write(0x004, 0x1000);
        write(0x006, 0x000F);
        write(0x008, 0x0000);
        write(0x1C0, 0x0000);
        write(0x1C2, 0x5000);
        write(0x1A0, 0x0001);
        run(4800u);
        require(energy(out) > 1000.0, "A keyed voice produced no sound");
        write(0x1A4, 0x0001);
        run(48000u);
        std::vector<int16_t> tail(out.end() - 4800, out.end());
        require(energy(tail) < 10.0, "A released voice kept playing");
        openkai2::spu2::reset(iop.data(), now);
        write(0x198, 0x0FF0);
        write(0x598, 0x0FFC);
        for (uint32_t core : {0x760u, 0x788u}) {
            write(core, 0x3FFF);
            write(core + 2u, 0x3FFF);
        }
        write(0x76C, 0x7FFF);
        write(0x76E, 0x7FFF);
        write(0x1B0, 0x0001);
        constexpr unsigned streamBlocks = 8u, frames = streamBlocks * 256u;
        std::vector<int16_t> music;
        unsigned phase = 0;
        for (unsigned transfer = 0; transfer < 12u; ++transfer) {
            auto *pcm = reinterpret_cast<int16_t *>(iop.data() + 0x20000u);
            for (unsigned b = 0; b < streamBlocks; ++b)
                for (unsigned i = 0; i < 256u; ++i, ++phase) {
                    const auto value = int16_t(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * phase / 48000.0));
                    pcm[b * 512u + i] = value;
                    pcm[b * 512u + 256u + i] = value;
                }
            openkai2::spu2::dma_write(0, 0x20000u, frames * 4u, now);
            run(frames);
            music.insert(music.end(), out.begin(), out.end());
            openkai2::spu2::dma_complete(0, now);
            std::memset(iop.data() + 0x20000u, 0x55, frames * 4u);
            now += 2000u;
            openkai2::spu2::advance(now);
            openkai2::spu2::take_output(out);
            music.insert(music.end(), out.begin(), out.end());
        }
        std::vector<double> left;
        for (size_t i = music.size() / 2u; i + 1u < music.size(); i += 2u)
            left.push_back(music[i]);
        const double k = 2.0 * std::cos(2.0 * 3.14159265358979323846 * 1000.0 / 48000.0);
        double s1 = 0.0, s2 = 0.0, total = 0.0;
        for (double x : left) {
            const double s = x + k * s1 - s2;
            s2 = s1;
            s1 = s;
            total += x * x;
        }
        const double tone = (s1 * s1 + s2 * s2 - k * s1 * s2) * 2.0 / double(left.size());
        require(total > 0.0 && tone / total > 0.95, "AutoDMA music did not play back as a clean tone");

        const auto clean = [](const std::vector<int16_t> &stereo, double hz) {
            const double k = 2.0 * std::cos(2.0 * 3.14159265358979323846 * hz / 48000.0);
            double s1 = 0.0, s2 = 0.0, sum = 0.0;
            size_t count = 0;
            for (size_t i = stereo.size() / 2u & ~size_t(1); i < stereo.size(); i += 2u, ++count) {
                const double s = stereo[i] + k * s1 - s2;
                s2 = s1;
                s1 = s;
                sum += double(stereo[i]) * stereo[i];
            }
            return sum > 0.0 ? (s1 * s1 + s2 * s2 - k * s1 * s2) * 2.0 / double(count) / sum : 0.0;
        };
        constexpr unsigned longBlocks = 32u;
        auto *ring = reinterpret_cast<int16_t *>(iop.data() + 0x40000u);
        std::memset(ring, 0x55, longBlocks * 1024u);
        unsigned written = 0;
        const auto fillBlock = [&](unsigned b) {
            for (unsigned i = 0; i < 256u; ++i) {
                const auto value = int16_t(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 750.0 * (b * 256u + i) / 48000.0));
                ring[b * 512u + i] = ring[b * 512u + 256u + i] = value;
            }
            ++written;
        };
        fillBlock(0);
        fillBlock(1);
        std::vector<int16_t> streamed;
        openkai2::spu2::dma_write(0, 0x40000u, longBlocks * 1024u, now);
        for (unsigned b = 0; b < longBlocks; ++b) {
            run(256u);
            streamed.insert(streamed.end(), out.begin(), out.end());
            if (written < longBlocks)
                fillBlock(written);
        }
        openkai2::spu2::dma_complete(0, now);
        require(clean(streamed, 750.0) > 0.95, "AutoDMA did not play blocks written during the transfer");
        std::cout << "SPU2 mirror passed: 48 kHz mixing, DMA upload, ADPCM voice key on and release, clean AutoDMA streaming, blocks written during a transfer.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
