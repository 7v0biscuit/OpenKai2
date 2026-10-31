// SPDX-License-Identifier: GPL-3.0-only
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
double call(void (*stub)(uint8_t *, R5900Context *, PS2Runtime *), double value) {
    R5900Context ctx{};
    ctx.r[4] = _mm_set_epi64x(0, std::bit_cast<int64_t>(value));
    ctx.r[2] = _mm_set_epi64x(0, 0x5555555555555555ll);
    ctx.f[12] = 99.0f;
    R5900Context *registers = &ctx;
    stub(nullptr, registers, nullptr);
    return std::bit_cast<double>(static_cast<uint64_t>(GPR_U64(registers, 2)));
}
}
int main() {
    try {
        require(call(ps2_stubs::fabs, -2.5) == 2.5 && call(ps2_stubs::fabs, 3.25) == 3.25, "fabs ignored $a0");
        require(std::signbit(call(ps2_stubs::fabs, -0.0)) == false, "fabs kept the sign of zero");
        require(call(ps2_stubs::floor, 2.75) == 2.0 && call(ps2_stubs::floor, -2.25) == -3.0, "floor ignored $a0");
        require(std::abs(call(ps2_stubs::atan, 1.0) - 0.7853981633974483) < 1e-15, "atan ignored $a0");
        require(call(ps2_stubs::atan, 0.0) == 0.0 && call(ps2_stubs::atan, -1e300) < -1.57, "atan range differs");
        std::cout << "EE soft-double math passed: fabs, floor and atan take $a0 and return $v0.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
