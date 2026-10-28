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
        alignas(16) int32_t lanes[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(lanes), ps2_vftoi(_mm_setr_ps(3.0e9f, -3.0e9f, -7.9f, bits(0x7F7FFFFFu))));
        require(lanes[0] == INT32_MAX && lanes[1] == INT32_MIN && lanes[2] == -7 && lanes[3] == INT32_MAX,
                "VFTOI did not truncate and saturate by sign");
        _mm_store_si128(reinterpret_cast<__m128i *>(lanes), ps2_vftoi(_mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))));
        require(lanes[0] == INT32_MAX, "VFTOI treated the VU maximum as a NaN");
        require(ps2_vdiv(1.0f, 4.0f) == 0.25f, "VDIV changed an ordinary quotient");
        require(std::bit_cast<uint32_t>(ps2_vdiv(1.0f, 0.0f)) == 0x7F7FFFFFu &&
                std::bit_cast<uint32_t>(ps2_vdiv(-1.0f, 0.0f)) == 0xFF7FFFFFu &&
                std::bit_cast<uint32_t>(ps2_vdiv(1.0f, bits(0x80000000u))) == 0xFF7FFFFFu,
                "VDIV by zero did not return the signed VU maximum");
        require(std::bit_cast<uint32_t>(ps2_vdiv(1.0f, bits(0x00000001u))) == 0x7F7FFFFFu,
                "VDIV did not read a denormal divisor as zero");
        require(std::bit_cast<uint32_t>(ps2_vdiv(0.0f, 0.0f)) == 0x7FFFFFFFu, "VDIV of zero by zero differs");
        require(ps2_vsqrt(-16.0f) == 4.0f, "VSQRT did not take the magnitude");
        require(ps2_vrsqrt(1.0f, 16.0f) == 0.25f && ps2_vrsqrt(2.0f, -16.0f) == 0.5f,
                "VRSQRT ignored its numerator or the divisor magnitude");
        require(std::bit_cast<uint32_t>(ps2_vrsqrt(1.0f, 0.0f)) == 0x7F7FFFFFu &&
                std::bit_cast<uint32_t>(ps2_vrsqrt(-1.0f, 0.0f)) == 0xFF7FFFFFu,
                "VRSQRT of zero did not return the signed VU maximum");
        std::cout << "EE float conversion passed: CVT.W.S, VFTOI, VDIV, VSQRT, VRSQRT and looped story time.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
