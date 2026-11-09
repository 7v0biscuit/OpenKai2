// SPDX-License-Identifier: GPL-3.0-only
#include "disc_timing.h"
#include "ee_disc_timing.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using openkai2::DiscTiming;
using openkai2::EeDiscClock;
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
        require(EeDiscClock::tick_at(EeDiscClock::cycles_at(1'234)) == 1'234, "Field ticks do not round-trip through drive cycles");
        require(std::abs(double(EeDiscClock::cycles_at(60'000)) / DiscTiming::clock - 1'001.0) < 1e-6, "Field ticks are not NTSC fields");
        EeDiscClock disc;
        require(!disc.busy(100), "An idle drive reported busy");
        disc.read(100, 200'000, 512);
        const uint64_t loaded = disc.ready();
        const double expected = (double(DiscTiming::full_seek + DiscTiming::transfer(200'000, 512)) / DiscTiming::clock) * 60'000.0 / 1'001.0;
        require(std::abs(double(loaded - 100) - expected) <= 1.0, "A game read did not take the drive's seek and transfer time");
        require(disc.busy(loaded - 1) && !disc.busy(loaded), "A game read finished at the wrong field");
        disc.read(loaded, 200'512, 512);
        require(disc.ready() - loaded < loaded - 100, "A sequential game read paid for a seek");
        disc.read(100'000, 900'000, 16);
        require(!disc.busy(100'000 + EeDiscClock::stale_ticks) || disc.ready() <= 100'000 + EeDiscClock::stale_ticks, "Read time ran away");
        disc.read(1'000'000, 10, 16);
        disc.read(10, 20, 16);
        require(disc.ready() < 10 + EeDiscClock::stale_ticks, "A state loaded from earlier kept the drive busy");
        disc.enable(false);
        disc.read(500, 300'000, 4'096);
        require(!disc.busy(500), "Instant mode delayed a read");
        std::cout << "PS2 disc timing passed: CAV transfer rate, seeks, sequential reads, queued commands and game reads timed in fields.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
