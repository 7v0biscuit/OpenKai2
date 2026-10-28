// SPDX-License-Identifier: GPL-3.0-only
#include "ps2_runtime_macros.h"
#include <climits>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
float bits(uint32_t value) { return std::bit_cast<float>(value); }
// Story-mode paths wrap their time into a loop with t - trunc(t) / n * n
// (0x0023A67C) before sampling keyframes.
float wrap(float time, int32_t length) {
    const int32_t whole = FPU_CVT_W_S(time);
    return FPU_SUB_S(time, FPU_CVT_S_W(whole / length * length));
}
}
int main() {
    try {
        require(FPU_CVT_W_S(2.6f) == 2 && FPU_CVT_W_S(2.4f) == 2, "CVT.W.S rounded instead of truncating");
        require(FPU_CVT_W_S(-2.6f) == -2 && FPU_CVT_W_S(-0.5f) == 0, "CVT.W.S did not truncate toward zero");
        require(FPU_CVT_W_S(0.0f) == 0 && FPU_CVT_W_S(bits(0x80000000u)) == 0, "CVT.W.S changed zero");
        require(FPU_CVT_W_S(bits(0x4EFFFFFFu)) == 2147483520, "CVT.W.S lost the largest in-range value");
        require(FPU_CVT_W_S(bits(0x4F000000u)) == INT32_MAX && FPU_CVT_W_S(3.0e9f) == INT32_MAX,
                "CVT.W.S did not saturate large positive values");
        require(FPU_CVT_W_S(bits(0xCF000000u)) == INT32_MIN && FPU_CVT_W_S(-3.0e9f) == INT32_MIN,
                "CVT.W.S did not saturate large negative values");
        require(FPU_CVT_W_S(bits(0x7F7FFFFFu)) == INT32_MAX && FPU_CVT_W_S(bits(0x7F800000u)) == INT32_MAX,
                "CVT.W.S did not saturate the EE maximum");
        require(wrap(9.6f, 10) == 9.6f && wrap(19.5f, 10) == 9.5f && wrap(4.75f, 10) == 4.75f,
                "Looped story time left the current segment");
        std::cout << "EE float conversion passed: truncation, signed zero, saturation and looped story time.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
