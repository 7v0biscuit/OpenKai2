// SPDX-License-Identifier: GPL-3.0-only
#include "disc_timing.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using openkai2::DiscTiming;
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
double milliseconds(uint64_t cycles) { return double(cycles) * 1000.0 / DiscTiming::clock; }
}
int main() {
    try {
        require(std::abs(DiscTiming::speed(0) - 24.0 / 58.0) < 1e-9, "Hub speed differs from the inner radius");
        require(std::abs(DiscTiming::speed(DiscTiming::layer_sectors) - 1.0) < 1e-9, "Edge speed is not 4x");
        require(DiscTiming::speed(400'000) > DiscTiming::speed(100'000), "Outer sectors are not faster");

        // A 1 MiB read near the start of the disc takes a little under half a second.
        const double megabyte = milliseconds(DiscTiming::transfer(30'000, 512));
        require(megabyte > 380.0 && megabyte < 480.0, "Inner-track transfer rate is out of range");
        const double edge = milliseconds(DiscTiming::transfer(DiscTiming::layer_sectors - 512, 512));
        require(edge > 180.0 && edge < 200.0, "Edge transfer rate is not 4x DVD");

        require(DiscTiming::seek(100, 100) == 0, "A read at the head position needs no seek");
        require(DiscTiming::seek(100, 104) == 4 * DiscTiming::sector_cycles(104), "Short skips are not read through");
        require(DiscTiming::seek(100, 5'000) == DiscTiming::short_seek, "Nearby seek is not a short seek");
        require(DiscTiming::seek(5'000, 100) == DiscTiming::short_seek, "Backward seek differs from forward seek");
        require(DiscTiming::seek(100, 200'000) == DiscTiming::full_seek, "Distant seek is not a full seek");

        DiscTiming drive;
        const uint64_t first = drive.read(1'000, 200'000, 16);
        require(first == 1'000 + DiscTiming::full_seek + DiscTiming::transfer(200'000, 16), "First read time differs");
        require(drive.busy(first - 1) && !drive.busy(first), "Drive busy state differs from completion");
        require(drive.head() == 200'016, "Head did not advance past the read");
        const uint64_t second = drive.read(first, 200'016, 16);
        require(second == first + DiscTiming::transfer(200'016, 16), "Sequential read paid for a seek");
        const uint64_t queued = drive.read(first, 200'032, 16);
        require(queued == second + DiscTiming::transfer(200'032, 16), "A command issued while busy did not queue");
        drive.reset();
        require(!drive.busy(0) && drive.head() == 0, "Reset kept drive state");
        const uint64_t seek = drive.seek_to(10, 50'000);
        require(seek == 10 + DiscTiming::full_seek && drive.head() == 50'000, "Seek command time differs");
        std::cout << "PS2 disc timing passed: CAV transfer rate, seeks, sequential reads and queued commands.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
