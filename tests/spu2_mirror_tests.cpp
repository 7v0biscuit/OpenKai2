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
constexpr uint32_t spu2 = 0x1F900000u, tick = 768u; // IOP cycles per 48 kHz sample
double energy(const std::vector<int16_t> &samples) {
    double sum = 0.0;
    for (auto value : samples)
        sum += double(value) * value;
    return samples.empty() ? 0.0 : std::sqrt(sum / samples.size());
}
} // namespace

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

        // Two looping PS-ADPCM blocks of a full-scale square wave.
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

        // Sound library init: core 0's mix feeds core 1, which feeds the output.
        write(0x198, 0x0FF0);
        write(0x598, 0x0FFC);
        for (uint32_t core : {0x760u, 0x788u}) {
            write(core, 0x3FFF);
            write(core + 2u, 0x3FFF);
        }
        write(0x000, 0x3FFF); // voice 0 volume
        write(0x002, 0x3FFF);
        write(0x004, 0x1000); // 48 kHz playback
        write(0x006, 0x000F); // fastest attack, full sustain
        write(0x008, 0x0000);
        write(0x1C0, 0x0000);
        write(0x1C2, 0x5000);
        write(0x1A0, 0x0001); // key on voice 0
        run(4800u);
        require(energy(out) > 1000.0, "A keyed voice produced no sound");
        write(0x1A4, 0x0001); // key off
        run(48000u);
        std::vector<int16_t> tail(out.end() - 4800, out.end());
        require(energy(tail) < 10.0, "A released voice kept playing");
        // Streamed music: AutoDMA blocks of 256 left then 256 right samples,
        // refilled in IOP memory as soon as each transfer completes.
        openkai2::spu2::reset(iop.data(), now);
        write(0x198, 0x0FF0);
        write(0x598, 0x0FFC);
        for (uint32_t core : {0x760u, 0x788u}) {
            write(core, 0x3FFF);
            write(core + 2u, 0x3FFF);
        }
        write(0x76C, 0x7FFF); // core 0 sound data input volume
        write(0x76E, 0x7FFF);
        write(0x1B0, 0x0001); // AutoDMA on core 0
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
            std::memset(iop.data() + 0x20000u, 0x55, frames * 4u); // the driver refills its buffer
            // The driver takes a moment to answer the interrupt and restart.
            now += 2000u;
            openkai2::spu2::advance(now);
            openkai2::spu2::take_output(out);
            music.insert(music.end(), out.begin(), out.end());
        }
        // Goertzel energy at 1 kHz against the total, over the settled tail.
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
        std::cout << "SPU2 mirror passed: 48 kHz mixing, DMA upload, ADPCM voice key on and release, clean AutoDMA streaming.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
