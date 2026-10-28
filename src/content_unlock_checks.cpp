// SPDX-License-Identifier: GPL-3.0-only
#include "content_unlock_checks.h"
#include "launcher/content_unlock.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>
namespace openkai2 {
namespace {
constexpr uint32_t buffer = 0x650000u, gp = 0x3ac4f0u, sp = 0x1fff000u, returned = 0x29f00u, bytes = 0xab0u;
void require(bool good, const char *why) {
    if (!good)
        throw std::runtime_error(why);
}
void transfer(uint8_t *, R5900Context *c, PS2Runtime *runtime, bool upload) {
    require(getRegU32(c, 6) * 16u == bytes && getRegU32(c, 7) == 1u, "Original save DMA arguments changed");
    auto &memory = runtime->memory();
    const uint32_t address = getRegU32(c, upload ? 5 : 4);
    require((address & 0x1fffffffu) == buffer && getRegU32(c, upload ? 4 : 5) == 0u,
            "Original save DMA range changed");
    for (uint32_t i = 0; i < bytes; ++i) {
        const auto value = memory.read8((upload ? address : 0x70000000u) + i);
        memory.write8((upload ? 0x70000000u : address) + i, value);
    }
    c->pc = getRegU32(c, 31);
}
} // namespace
void verify_original_save_codec(PS2Runtime &runtime, uint8_t *ram) {
    struct Restore {
        PS2Runtime &runtime;
        std::vector<std::pair<uint32_t, PS2Runtime::RecompiledFunction>> saved;
        ~Restore() {
            for (auto it = saved.rbegin(); it != saved.rend(); ++it)
                runtime.replaceFunction(it->first, it->second);
        }
        void replace(uint32_t at, PS2Runtime::RecompiledFunction fn) {
            require(runtime.hasFunction(at), "Original save dependency is not registered");
            saved.emplace_back(at, runtime.lookupFunction(at));
            runtime.replaceFunction(at, fn);
        }
    } restore{runtime, {}};
    require(runtime.hasFunction(0x233010u) && runtime.hasFunction(0x2332a0u),
            "Original save codec is not registered");
    // Only cache maintenance and scratchpad DMA are substituted. The original
    // signature check, RNG, encryption/decryption and both checksum loops run.
    restore.replace(0x10a3d8u, [](uint8_t *, R5900Context *c, PS2Runtime *) {
        require((getRegU32(c, 4) & 0x1fffffffu) == buffer &&
                    (getRegU32(c, 5) & 0x1fffffffu) == buffer + bytes - 1u,
                "Save cache range changed");
        c->pc = getRegU32(c, 31);
    });
    restore.replace(0x143510u,
                    [](uint8_t *r, R5900Context *c, PS2Runtime *runtime) { transfer(r, c, runtime, true); });
    restore.replace(0x143580u,
                    [](uint8_t *r, R5900Context *c, PS2Runtime *runtime) { transfer(r, c, runtime, false); });
    constexpr char signature[] = "#SPF 1.0";
    std::memcpy(ram + 0x3a26a0u, signature, 8u);
    auto &memory = runtime.memory();
    const auto savedRandom = memory.read32(gp - 0x7fe8u);
    struct RandomRestore {
        PS2Memory &memory;
        uint32_t value;
        ~RandomRestore() { memory.write32(gp - 0x7fe8u, value); }
    } randomRestore{memory, savedRandom};
    const auto call = [&](uint32_t pc, uint32_t alias, uint32_t arg0 = buffer, uint32_t arg1 = bytes) {
        R5900Context c{};
        c.pc = pc;
        c.r[4] = _mm_cvtsi64_si128(int64_t(int32_t(arg0 | alias)));
        c.r[5] = _mm_cvtsi32_si128(arg1);
        c.r[28] = _mm_cvtsi32_si128(gp);
        c.r[29] = _mm_cvtsi32_si128(sp);
        c.r[31] = _mm_cvtsi32_si128(returned);
        unsigned steps = 0;
        while (c.pc != returned && ++steps < 200000u) {
            auto fn = runtime.lookupFunction(c.pc);
            require(fn != nullptr, "Original save codec reached an untranslated continuation");
            try {
                fn(ram, &c, &runtime);
            } catch (const EeDispatcherTransfer &) {
            }
        }
        require(c.pc == returned && getRegU32(&c, 29) == sp && getRegU32(&c, 28) == gp,
                "Original save codec did not return through its caller frame");
        require(std::all_of(ram + buffer - 16u, ram + buffer, [](uint8_t v) { return v == 0xa5u; }) &&
                    std::all_of(ram + buffer + bytes, ram + buffer + bytes + 16u,
                                [](uint8_t v) { return v == 0xa5u; }),
                "Original save codec changed memory guards");
        return getRegU32(&c, 2);
    };
    const auto place = [&](const std::vector<uint8_t> &data) {
        std::memset(ram + buffer - 16u, 0xa5u, bytes + 32u);
        std::memcpy(ram + buffer, data.data(), bytes);
    };
    for (uint32_t seed : {1u, 0x12345678u, 0xf0743925u})
        for (uint32_t alias : {0u, 0x80000000u, 0xa0000000u}) {
            std::vector<uint8_t> plain(bytes);
            std::memcpy(plain.data(), signature, 8u);
            for (unsigned i = 0; i < 4u; ++i)
                plain[12u + i] = uint8_t(seed >> (i * 8u));
            for (unsigned i = 0x30u; i < bytes; ++i)
                plain[i] = uint8_t(i * 37u + (i >> 3u));
            launcher::unlock_progress_content(plain);
            const auto encoded = launcher::encode_progress_save(plain);
            place(encoded);
            memory.write32(gp - 0x7fe8u, 0x13579bdfu);
            require(call(0x233010u, alias) == 0u &&
                        std::equal(plain.begin() + 0x30u, plain.end(), ram + buffer + 0x30u),
                    "The original game decoder rejected or changed the unlocked save");
            require(memory.read32(gp - 0x7fe8u) == 0x13579bdfu,
                    "Original save decoder did not restore the game RNG");
            place(plain);
            memory.write32(gp - 0x7fe8u, seed);
            call(0x2332a0u, alias);
            const std::vector<uint8_t> original(ram + buffer, ram + buffer + bytes);
            const auto decoded = launcher::decode_progress_save(original);
            require(std::equal(plain.begin() + 0x30u, plain.end(), decoded.begin() + 0x30u) &&
                        launcher::encode_progress_save(decoded) == original,
                    "Save tool differs from the original game encoder");
            auto broken = encoded;
            broken[0] = '!';
            place(broken);
            require(call(0x233010u, alias) == 1u, "Original decoder accepted an invalid signature");
            broken = encoded;
            broken[100u] ^= 1u;
            place(broken);
            require(call(0x233010u, alias) == 2u, "Original decoder accepted an invalid outer checksum");
            for (unsigned j = 0; j < 16u; ++j) {
                uint8_t sum = 0;
                for (unsigned i = 0x20u + j; i < bytes; i += 16u)
                    sum = uint8_t(sum + broken[i]);
                broken[0x10u + j] = sum;
            }
            place(broken);
            require(call(0x233010u, alias) == 3u, "Original decoder accepted an invalid inner checksum");
        }
    // Exercise the original capsule side effects and serializer rather than
    // validating the launcher field offsets against another save-tool copy.
    constexpr uint32_t object = 0x660000u;
    constexpr uint8_t characterMap[] = {0, 2,  28, 26, 3,  27, 5,  6,  4,  7,  8,  24, 21, 33, 35,
                                        9, 10, 12, 11, 15, 16, 17, 18, 25, 20, 32, 29, 30, 31};
    constexpr uint8_t stageMap[] = {3, 2, 0, 5, 7, 6, 1, 8, 4};
    std::memcpy(ram + 0x379fd0u, characterMap, sizeof(characterMap));
    std::memcpy(ram + 0x379ff0u, stageMap, sizeof(stageMap));
    std::memset(ram + object, 0, 0xb90u);
    for (unsigned i = 0; i < 29u; ++i)
        call(0x283680u, 0u, object, 0x1cdu + i);
    for (unsigned i = 0; i < 9u; ++i)
        call(0x283680u, 0u, object, 0x1feu + i);
    std::vector<uint8_t> expected(bytes);
    std::memcpy(expected.data(), signature, 8u);
    launcher::unlock_progress_content(expected);
    place(std::vector<uint8_t>(bytes));
    call(0x233810u, 0u, buffer, object);
    require(std::equal(expected.begin() + 0x90u, expected.begin() + 0x98u, ram + buffer + 0x90u) &&
                std::equal(expected.begin() + 0xb4u, expected.begin() + 0xb6u, ram + buffer + 0xb4u),
            "Launcher unlock flags differ from original capsule handling and save serialization");
}
} // namespace openkai2
