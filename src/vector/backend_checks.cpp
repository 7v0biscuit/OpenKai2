// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/ps2_vu1.h"
#include "vector/pcsx2_bridge.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>
namespace {
using Memory = std::array<uint8_t, 0x4000>;
constexpr uint32_t nop = 0x8000033cu, upperNop = 0x2ffu, end = 0x40000000u;
uint64_t generation = 0;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void put(Memory &code, unsigned pc, uint32_t lo, uint32_t hi = upperNop) {
    std::memcpy(code.data() + pc, &lo, 4u);
    std::memcpy(code.data() + pc + 4u, &hi, 4u);
}
uint32_t special(unsigned opcode, unsigned fs = 0, unsigned ft = 0, unsigned mask = 15u) {
    return 0x8000003cu | (opcode & 3u) | ((opcode & 0x7cu) << 4u) | (fs << 11u) | (ft << 16u) |
           (mask << 21u);
}
uint32_t upperSpecial(unsigned opcode, unsigned fs, unsigned ft) {
    return (15u << 21u) | (ft << 16u) | (fs << 11u) | 0x3cu | (opcode & 3u) |
           ((opcode & 0x7cu) << 4u);
}
VU1State initial() {
    VU1State s{};
    s.vf[0][3] = 1;
    s.q = 1;
    s.r = 0x3f800000u;
    return s;
}
void run(VU1State &s, Memory &code, Memory &data, bool jit, uint32_t budget = 8192u,
         bool fresh = true, VectorPacketCallback callback = nullptr, void *context = nullptr) {
    bridgeRun(s, code.data(), data.data(), budget, fresh, generation, callback, context, &s, jit);
}
void flags(bool jit) {
    Memory code{}, data{};
    put(code, 0, nop, upperNop | end);
    put(code, 8, nop);
    ++generation;
    auto s = initial();
    for (unsigned status = 0; status < 4096u; ++status) {
        s = initial();
        s.status = status;
        s.mac = 0xa55au;
        s.clip = 0xabcdefu;
        const auto previous = _mm_getcsr();
        _mm_setcsr(0x5f80u);
        run(s, code, data, jit);
        require(_mm_getcsr() == 0x5f80u, "Caller floating-point control changed");
        _mm_setcsr(previous);
        require(s.status == status && s.mac == 0xa55au && s.clip == 0xabcdefu,
                "Architectural flag import/export changed bits");
        require(s.ebit && s.pc == 16u, "End-bit program did not stop after its delay slot");
    }
    bridgeForget();
}
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
double
host_calculations_across_dispatch(VU1State &state, Memory &code, Memory &data, bool jit,
                                  volatile double *input) {
    const double a0 = input[0] * 1.125, a1 = input[1] * 1.25, a2 = input[2] * 1.375,
                 a3 = input[3] * 1.5;
    const double a4 = input[4] * 1.625, a5 = input[5] * 1.75, a6 = input[6] * 1.875,
                 a7 = input[7] * 2.0;
    const double a8 = input[8] * 2.125, a9 = input[9] * 2.25, a10 = input[10] * 2.375,
                 a11 = input[11] * 2.5;
    run(state, code, data, jit);
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11;
}
void load_increment(bool jit) {
    Memory code{}, data{};
    for (unsigned row = 0; row < 32u; ++row)
        for (unsigned lane = 0; lane < 4u; ++lane) {
            const float v = float(100u * row + lane);
            std::memcpy(data.data() + row * 16u + lane * 4u, &v, 4u);
        }
    constexpr unsigned destinations[] = {17, 9,  10, 11, 12, 18, 19, 20,
                                         21, 22, 23, 24, 25, 26, 27, 28};
    for (unsigned i = 0; i < 16u; ++i)
        put(code, i * 8u, special(0x34u, 1u, destinations[i]));
    put(code, 128u, nop, upperNop | end);
    put(code, 136u, nop);
    ++generation;
    auto s = initial();
    s.vi[1] = 8;
    volatile double hostInputs[12];
    double expectedHost = 0;
    for (unsigned i = 0; i < 12u; ++i) {
        hostInputs[i] = double(i + 3u);
        expectedHost += double(i + 3u) * (1.125 + double(i) * .125);
    }
    const double hostResult = host_calculations_across_dispatch(s, code, data, jit, hostInputs);
    require(std::isfinite(hostResult) && std::abs(hostResult - expectedHost) < 1e-10,
            "Native execution corrupted floating-point values held by the host caller");
    for (unsigned i = 0; i < 16u; ++i)
        for (unsigned lane = 0; lane < 4u; ++lane)
            require(s.vf[destinations[i]][lane] == float(100u * (i + 8u) + lane),
                    "Load-increment changed the source address or vector register");
    require(s.vi[1] == 24, "Load-increment did not advance the integer register once per vector");
    bridgeForget();
}
void matrices(bool jit) {
    constexpr float matrix[4][4] = {
        {.5f, -.25f, .75f, 0}, {.125f, .5f, -.25f, 0}, {-.5f, .25f, .125f, 0}, {2, -3, 4, 1}};
    Memory code{}, data{};
    for (unsigned vertex = 0; vertex < 64u; ++vertex) {
        const float input[] = {float(int(vertex % 11u) - 5) * .125f,
                               float(int(vertex % 7u) - 3) * .25f,
                               float(int(vertex % 5u) - 2) * .5f, 1};
        std::memcpy(data.data() + vertex * 16u, input, 16u);
        const uint32_t hi[] = {upperNop,
                               upperSpecial(0x18u, 1u, 5u),
                               upperSpecial(0x09u, 2u, 5u),
                               upperSpecial(0x0au, 3u, 5u),
                               (15u << 21u) | (5u << 16u) | (4u << 11u) | (6u << 6u) | 0x0bu,
                               upperNop};
        for (unsigned i = 0; i < 6u; ++i) {
            const auto lo = i == 0u   ? (15u << 21u) | (5u << 16u) | vertex
                            : i == 5u ? 0x02000000u | (15u << 21u) | (6u << 11u) | (128u + vertex)
                                      : nop;
            put(code, (vertex * 6u + i) * 8u, lo, hi[i] | (vertex == 63u && i == 4u ? end : 0u));
        }
    }
    ++generation;
    auto s = initial();
    std::memcpy(s.vf[1], matrix, sizeof(matrix));
    run(s, code, data, jit);
    require(s.ebit, "Affine transform program did not finish");
    for (unsigned vertex = 0; vertex < 64u; ++vertex) {
        float input[4], output[4];
        std::memcpy(input, data.data() + vertex * 16u, 16u);
        std::memcpy(output, data.data() + (128u + vertex) * 16u, 16u);
        for (unsigned row = 0; row < 4u; ++row) {
            double expected = 0;
            for (unsigned col = 0; col < 4u; ++col)
                expected += double(matrix[col][row]) * input[col];
            require(std::isfinite(output[row]) && std::abs(double(output[row]) - expected) <=
                                                      .00002 * (1 + std::abs(expected)),
                    "Stored vertex differs from the independent matrix product");
        }
    }
    bridgeForget();
}
void branches_and_resume(bool jit) {
    Memory code{}, data{};
    put(code, 0, 0x10010064u);
    put(code, 8, 0x10020000u);
    put(code, 16, 0x80000032u | (2u << 16u) | (2u << 11u) | (1u << 6u));
    put(code, 24, 0x800007f2u | (1u << 16u) | (1u << 11u));
    put(code, 32, nop);
    put(code, 40, (0x2du << 25u) | (1u << 11u) | 0x7fcu);
    put(code, 48, nop);
    put(code, 56, nop, upperNop | end);
    put(code, 64, nop);
    ++generation;
    auto s = initial();
    s.cycles = 0xfffffff0ull;
    const auto start = s.cycles;
    run(s, code, data, jit, 64u);
    unsigned resumes = 0;
    while (!s.ebit && ++resumes < 64u)
        run(s, code, data, jit, 64u, false);
    require(s.ebit && resumes < 64u && s.vi[1] == 0 && s.vi[2] == 100,
            "Branch delay, integer wrap or continuation failed");
    require(s.cycles > start && s.cycles >= 0x100000000ull,
            "Vector cycle accounting truncated at 32 bits");
    bridgeForget();
}
void scalars_and_halt(bool jit) {
    Memory code{}, data{};
    put(code, 0, special(0x38u, 1u, 2u, 0u));
    put(code, 8, special(0x3bu, 0u, 0u, 0u));
    put(code, 16, special(0x70u, 3u, 0u, 0u));
    put(code, 24, special(0x7bu, 0u, 0u, 0u));
    put(code, 32, nop, upperNop | end);
    put(code, 40, nop);
    ++generation;
    auto s = initial();
    s.vf[1][0] = 6;
    s.vf[2][0] = 3;
    s.vf[3][0] = 2;
    s.vf[3][1] = 3;
    s.vf[3][2] = 6;
    run(s, code, data, jit);
    require(s.q == 2 && s.p == 49 && s.ebit, "DIV/EFU scalar result or wait ordering changed");
    for (bool t : {false, true}) {
        code.fill(0u);
        put(code, 0, nop, upperNop | (t ? 0x08000000u : 0x10000000u));
        put(code, 8, nop, upperNop | end);
        put(code, 16, nop);
        ++generation;
        s = initial();
        s.dBitEnabled = !t;
        s.tBitEnabled = t;
        run(s, code, data, jit);
        if (s.stoppedByD != !t || s.stoppedByT != t || s.ebit)
            throw std::runtime_error(
                "Enabled D/T halt export mismatch: jit=" + std::to_string(jit) +
                " t=" + std::to_string(t) + " D=" + std::to_string(s.stoppedByD) +
                " T=" + std::to_string(s.stoppedByT) + " E=" + std::to_string(s.ebit) +
                " pc=" + std::to_string(s.pc));
    }
    bridgeForget();
}
struct Packets {
    std::vector<std::vector<uint8_t>> values;
};
void collect(void *context, const uint8_t *p, uint32_t size) {
    static_cast<Packets *>(context)->values.emplace_back(p, p + size);
}
void fail_callback(void *, const uint8_t *, uint32_t) {
    throw std::runtime_error("callback exception sentinel");
}
void packets(bool jit) {
    Memory code{}, data{};
    put(code, 0, special(0x68u, 0u, 2u));
    put(code, 8, special(0x69u, 0u, 3u));
    put(code, 16, special(0x6cu, 1u));
    put(code, 24, nop);
    put(code, 32, nop, upperNop | end);
    put(code, 40, nop);
    ++generation;
    std::array<uint8_t, 48> expected{};
    const uint64_t first = 1ull | (1ull << 60u), last = 0x8000ull | (1ull << 60u), registers = 0xeu,
                   payload = 0x1122334455667788ull;
    std::memcpy(expected.data(), &first, 8u);
    std::memcpy(expected.data() + 8u, &registers, 8u);
    std::memcpy(expected.data() + 16u, &payload, 8u);
    std::memcpy(expected.data() + 32u, &last, 8u);
    std::memcpy(expected.data() + 40u, &registers, 8u);
    for (unsigned i = 0; i < expected.size(); ++i)
        data[(0x3ff0u + i) & 0x3fffu] = expected[i];
    auto s = initial();
    s.vi[1] = 0x3ff;
    s.top = 0x6abu;
    s.itop = 0x735u;
    Packets output;
    run(s, code, data, jit, 8192u, true, collect, &output);
    require(s.vi[2] == 0x2ab && s.vi[3] == 0x335,
            "VIF TOP/ITOP were not masked to the VU1 address range");
    require(output.values.size() == 1u &&
                output.values.front() == std::vector<uint8_t>(expected.begin(), expected.end()),
            "Wrapped multi-tag PATH1 packet changed");
    s = initial();
    s.vi[1] = 0x3ff;
    bool caught = false;
    try {
        run(s, code, data, jit, 8192u, true, fail_callback, nullptr);
    } catch (const std::runtime_error &e) {
        caught = std::string(e.what()) == "callback exception sentinel";
    }
    require(caught, "Graphics callback error was not returned after native execution");
    s = initial();
    s.vi[1] = 0x3ff;
    output.values.clear();
    run(s, code, data, jit, 8192u, true, collect, &output);
    require(output.values.size() == 1u, "Callback failure corrupted the next vector dispatch");
    bridgeForget();
}
void code_updates_and_zero(bool jit) {
    Memory code{}, data{};
    auto s = initial();
    auto multiply = [&](unsigned destination) {
        put(code, 0, 0x02000000u | (15u << 21u),
            end | (15u << 21u) | (2u << 16u) | (1u << 11u) | (destination << 6u) | 0x2au);
        put(code, 8, nop);
        ++generation;
    };
    multiply(0u);
    for (unsigned lane = 0; lane < 4u; ++lane) {
        s.vf[1][lane] = float(lane + 1u);
        s.vf[2][lane] = float(lane + 2u);
    }
    run(s, code, data, jit);
    const float zero[] = {0, 0, 0, 1};
    require(std::memcmp(data.data(), zero, 16u) == 0 && std::memcmp(s.vf[0], zero, 16u) == 0,
            "Upper write changed the constant register seen by its paired store");
    multiply(3u);
    s.pc = 0u;
    run(s, code, data, jit);
    for (unsigned lane = 0; lane < 4u; ++lane)
        require(s.vf[3][lane] == float((lane + 1u) * (lane + 2u)),
                "Uploaded microcode reused stale compiled instructions");
    bridgeForget();
}
void unsupported_opcode(bool jit) {
    Memory code{}, data{};
    put(code, 0, 0xfc000000u, upperNop | end);
    put(code, 8, nop);
    ++generation;
    auto state = initial();
    bool caught = false;
    try {
        run(state, code, data, jit);
    } catch (const std::runtime_error &error) {
        caught = std::string(error.what()).find("Vector core error:") == 0;
    }
    require(caught, "Unsupported vector opcode was silently accepted");
    bridgeForget();
}
} // namespace
namespace openkai2 {
void verify_vector_backend() {
    for (bool jit : {false, true}) {
        flags(jit);
        load_increment(jit);
        matrices(jit);
        branches_and_resume(jit);
        scalars_and_halt(jit);
        packets(jit);
        code_updates_and_zero(jit);
        if (jit)
            unsupported_opcode(jit);
        std::cout << (jit ? "microVU" : "PCSX2 interpreter")
                  << ": flags, loads, matrices, branches, resume, cycles, scalars, halt bits, "
                     "packets, callbacks and code updates passed\n";
    }
}
} // namespace openkai2
