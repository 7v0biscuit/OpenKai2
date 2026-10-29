// SPDX-License-Identifier: GPL-3.0-only
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<uint8_t> ram(PS2_RAM_SIZE);
void put(uint32_t address, const std::string &text) { std::memcpy(ram.data() + address, text.c_str(), text.size() + 1u); }
void put64(uint32_t address, uint64_t value) { std::memcpy(ram.data() + address, &value, 8u); }
std::string text(uint32_t address) { return reinterpret_cast<const char *>(ram.data() + address); }
void setRegister(R5900Context &ctx, int index, uint64_t value) { ctx.r[index] = _mm_set_epi64x(0, int64_t(value)); }
}

int main() {
    try {
        put(0x1000u, "%s met %s at %d.%02d (%.1f)");
        put(0x2000u, "Goku");
        put(0x2100u, "Piccolo");
        const double ratio = 2.5;
        // A variadic EE function stores each argument register in an 8-byte
        // slot and passes their address as the va_list.
        put64(0x3000u, 0x2000u);
        put64(0x3008u, 0x2100u);
        put64(0x3010u, 7u);
        put64(0x3018u, 5u);
        put64(0x3020u, std::bit_cast<uint64_t>(ratio));
        R5900Context ctx{};
        setRegister(ctx, 4, 0x4000u);
        setRegister(ctx, 5, 0x1000u);
        setRegister(ctx, 6, 0x3000u);
        ps2_stubs::vsprintf(ram.data(), &ctx, nullptr);
        require(text(0x4000u) == "Goku met Piccolo at 7.05 (2.5)", "vsprintf read a va_list as 4-byte slots: " + text(0x4000u));

        // Fixed arguments: a 64-bit double fills one register, and arguments
        // past t3 come from 8-byte stack slots at the caller's $sp.
        put(0x1100u, "%d %d %d %d %d %d %.2f %s");
        R5900Context call{};
        setRegister(call, 4, 0x5000u);
        setRegister(call, 5, 0x1100u);
        for (int i = 0; i < 5; ++i)
            setRegister(call, 6 + i, uint64_t(i + 1));
        setRegister(call, 11, 6u);
        setRegister(call, 29, 0x6000u);
        put64(0x6000u, std::bit_cast<uint64_t>(0.75));
        put64(0x6008u, 0x2000u);
        ps2_stubs::sprintf(ram.data(), &call, nullptr);
        require(text(0x5000u) == "1 2 3 4 5 6 0.75 Goku", "sprintf misread EE argument slots: " + text(0x5000u));

        // Long dialogue lines are formatted whole.
        const std::string line(600u, 'A');
        put(0x7000u, line);
        put(0x1200u, "%s");
        put64(0x3100u, 0x7000u);
        setRegister(ctx, 4, 0x8000u);
        setRegister(ctx, 5, 0x1200u);
        setRegister(ctx, 6, 0x3100u);
        ps2_stubs::vsprintf(ram.data(), &ctx, nullptr);
        require(text(0x8000u) == line, "vsprintf cut a long line");
        std::cout << "EE varargs passed: va_list and register arguments take one 8-byte slot each, stack arguments follow at $sp, and long lines are formatted whole.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
