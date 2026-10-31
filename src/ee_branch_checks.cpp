// SPDX-License-Identifier: GPL-3.0-only
#include "ee_branch_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace openkai2 {
void verify_ee_signed_branch_math(PS2Runtime &runtime, uint8_t *ram) {
    constexpr std::array<uint32_t, 4> entries{0x11fc50u, 0x11fca8u, 0x11fd10u, 0x11ff98u};
    constexpr std::array<double, 8> values{0.5, -0.5, 1.0, -1.0, 3.141592653589793,
        0.4636476090008061, 1.0e20, 1.0e-20};
    constexpr uint32_t stack = 0x1fff000u, gp = 0x3ac4f0u, returned = 0x29f00u;
    for (unsigned operation = 0; operation < entries.size(); ++operation) {
        for (double left : values) for (double right : values) {
            R5900Context context{};
            context.pc = entries[operation];
            context.r[4] = _mm_cvtsi64_si128(std::bit_cast<int64_t>(left));
            context.r[5] = _mm_cvtsi64_si128(std::bit_cast<int64_t>(right));
            context.r[28] = _mm_cvtsi32_si128(gp);
            context.r[29] = _mm_cvtsi32_si128(stack);
            context.r[31] = _mm_cvtsi32_si128(returned);
            unsigned steps = 0;
            while (context.pc != returned && ++steps < 200000u) {
                auto function = runtime.lookupFunction(context.pc);
                if (!function) throw std::runtime_error("Original EE arithmetic reached an unregistered continuation");
                try { function(ram, &context, &runtime); } catch (const EeDispatcherTransfer &) {}
            }
            if (context.pc != returned || getRegU32(&context,29) != stack || getRegU32(&context,28) != gp)
                throw std::runtime_error("Original EE arithmetic lost its caller frame");
            const double result = std::bit_cast<double>(uint64_t(_mm_cvtsi128_si64(context.r[2])));
            const double expected = operation == 0 ? left + right : operation == 1 ? left - right
                                    : operation == 2 ? left * right : left / right;
            if (!std::isfinite(result) || std::abs(result - expected) >
                std::max(1.0e-30, std::abs(expected) * 1.0e-12))
                throw std::runtime_error("Original EE arithmetic differs after a signed branch");
        }
    }
}
}
