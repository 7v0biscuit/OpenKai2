// SPDX-License-Identifier: GPL-3.0-only
#include "story_checks.h"
#include "openkai2_translation_entries.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <stdexcept>
#include <vector>
namespace openkai2 {
namespace {
void verify_transition_methods(PS2Runtime &runtime, uint8_t *ram) {
    const auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    constexpr uint32_t object = 0x650000, returned = 0x29f00, sp = 0x1fffde0, gp = 0x3ac4f0;
    auto &memory = runtime.memory();
    struct SavedWords {
        PS2Runtime &runtime;
        std::vector<std::pair<uint32_t, uint32_t>> words;
        ~SavedWords() {
            for (auto [at, value] : words)
                runtime.memory().write32(at, value);
        }
    } saved{runtime, {}};
    for (uint32_t at : {gp - 0x7e80u, 0x69ccf0u, 0x69ccf4u, 0x69ccf8u, 0x69ccfcu,
                        0x69cd10u, 0x69cea4u, 0x69ceacu, 0x69ceb0u, 0x693f38u,
                        0x3b4bacu, gp - 0x7084u, gp - 0x7078u})
        saved.words.emplace_back(at, memory.read32(at));
    const auto put_float = [&](uint32_t at, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        memory.write32(at, bits);
    };
    const auto get_float = [&](uint32_t at) {
        auto bits = memory.read32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    };
    const auto call = [&](uint32_t entry, uint32_t alias) {
        R5900Context context{};
        context.pc = entry;
        context.r[4] = _mm_cvtsi64_si128(int64_t(int32_t(object | alias)));
        context.r[28] = _mm_cvtsi32_si128(gp);
        context.r[29] = _mm_cvtsi32_si128(sp);
        context.r[31] = _mm_cvtsi32_si128(returned);
        context.r[16] = _mm_set_epi32(1, 2, 3, 4);
        const auto preserved = context.r[16];
        for (unsigned step = 0; step < 32 && context.pc != returned; ++step) {
            auto function = runtime.lookupFunction(context.pc);
            require(function != nullptr, "Dragon World transition method is untranslated");
            try {
                function(ram, &context, &runtime);
            } catch (const EeDispatcherTransfer &) {
            }
        }
        require(context.pc == returned && getRegU32(&context, 29) == sp &&
                    getRegU32(&context, 31) == returned && getRegU32(&context, 28) == gp &&
                    std::memcmp(&context.r[16], &preserved, 16) == 0,
                "Dragon World transition lost its caller frame");
    };
    for (uint32_t alias : {0u, 0x80000000u, 0xa0000000u}) {
        // The board-overlay callback must return through its original update
        // method after both branches, rather than stop the guest dispatcher.
        for (uint32_t frame : {0u, 1u, 0xffffffffu})
            for (uint32_t mask : {0u, 1u, 2u}) {
                constexpr uint32_t overlay = 0x660000;
                std::memset(ram + object, 0, 128);
                std::memset(ram + overlay, 0, 128);
                memory.write32(object + 0x1c, 0x292400);
                memory.write32(object + 0x5c, overlay | alias);
                memory.write32(object + 0x60, mask);
                put_float(object + 0x44, 1.0f);
                put_float(object + 0x50, 1.0f);
                put_float(overlay + 0x30, 17.0f);
                put_float(overlay + 0x34, 23.0f);
                memory.write32(0x69cea4, frame);
                put_float(0x69ceac, 0.5f);
                put_float(0x69ceb0, 0.25f);
                call(0x292400, alias);
                const bool visible = ((frame + 1u) & mask) != 0;
                require(get_float(overlay + 0x5c) == (visible ? 0.5f : 0.0f) &&
                            get_float(overlay + 0x60) == (visible ? 0.25f : 0.0f) &&
                            get_float(overlay + 0x30) == (visible ? 226.0f : 17.0f) &&
                            get_float(overlay + 0x34) == (visible ? 187.0f : 23.0f) &&
                            memory.read32(object + 0x1c) == 0x292400,
                        "Dragon World board overlay changed its mask or coordinates");
            }
        for (uint32_t mode : {0xffffffffu, 0u, 4u, 6u}) {
            memory.write32(object + 0x1c, 0x22fee0);
            memory.write32(gp - 0x7e80, mode);
            call(0x22fee0, alias);
            require(memory.read32(object + 0x1c) == (mode == 0xffffffffu ? 0x22fee0u : 0x22fe70u),
                    "Dragon World scene wait changed the transition callback");
        }
        for (float initial : {0.3f, 1.0f}) {
            std::memset(ram + object, 0xa5, 128);
            memory.write32(object + 0x1c, 0x291b00);
            put_float(0x69ccf0, initial);
            for (unsigned frame = 0; frame < 24; ++frame) {
                if (memory.read32(object + 0x1c) != 0x291b00)
                    break;
                call(0x291b00, alias);
                const float now = get_float(0x69ccf0);
                require(now >= 0.25f && now <= initial &&
                            memory.read32(0x69ccf0) == memory.read32(0x69ccf4) &&
                            memory.read32(0x69ccf0) == memory.read32(0x69ccf8),
                        "Dragon World dimming changed its clamp or color lanes");
            }
            require(memory.read32(object + 0x1c) == 0x291990 &&
                        memory.read8(object + 0x34) == 0 && get_float(0x69ccf0) == 0.25f,
                    "Dragon World dimming did not finish");
            memory.write32(object + 0x1c, 0x291b80);
            put_float(0x69ccfc, initial);
            for (unsigned frame = 0; frame < 24; ++frame) {
                if (memory.read32(object + 0x1c) != 0x291b80)
                    break;
                call(0x291b80, alias);
                require(get_float(0x69ccfc) >= 0 && get_float(0x69ccfc) <= initial,
                        "Dragon World fade escaped its bounds");
            }
            require(memory.read32(object + 0x1c) == 0 && memory.read32(0x69cd10) == 0 &&
                        get_float(0x69ccfc) == 0,
                    "Dragon World fade did not release its callback");
        }
        for (uint32_t start : {0u, 8u})
            for (uint32_t busy : {0u, 1u}) {
                std::memset(ram + object, 0xa5, 128);
                memory.write32(object + 0x1c, 0x2a6a30);
                memory.write32(0x693f38, 0);
                memory.write16(0x3b4bae, uint16_t(start));
                memory.write32(gp - 0x7084, busy);
                memory.write32(gp - 0x7078, 0x1234);
                call(0x2a6a30, alias);
                const bool finish = start || !busy;
                require(memory.read32(object + 0x1c) == (finish ? 0x2a68e0u : 0x2a6a30u) &&
                            memory.read32(object + 0x50) == (finish ? 0u : 0xa5a5a5a5u) &&
                            memory.read32(gp - 0x7078) == (start ? 0u : 0x1234u),
                        "Dragon World ending changed its wait or Start handling");
                if (finish)
                    require(get_float(object + 0x30) == 45.0f,
                            "Dragon World ending did not start its exit timer");
            }
    }
}
}
void verify_story_callbacks(PS2Runtime &runtime, uint8_t *ram) {
    verify_transition_methods(runtime, ram);
    const auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    for (auto pc : translation::story_callback_entries)
        require(runtime.hasFunction(pc), "Story callback bank is incomplete");
    constexpr uint32_t object = 0x650000, marker = 0x29b00, returned = 0x29f00, sp = 0x1fffde0,
                       gp = 0x3ac4f0;
    struct Restore {
        PS2Runtime &r;
        std::vector<std::pair<uint32_t, PS2Runtime::RecompiledFunction>> old;
        ~Restore() {
            for (auto i = old.rbegin(); i != old.rend(); ++i)
                r.replaceFunction(i->first, i->second);
        }
        void replace(uint32_t at, PS2Runtime::RecompiledFunction f) {
            old.emplace_back(at, r.lookupFunction(at));
            r.replaceFunction(at, f);
        }
    } restore{runtime, {}};
    restore.replace(0x2733c0, [](uint8_t *, R5900Context *c, PS2Runtime *r) {
        auto &m = r->memory();
        if (getRegU32(c, 4) != m.read32(marker + 4))
            throw std::runtime_error("Story dependency argument changed");
        m.write32(marker, m.read32(marker) + 1);
        c->r[2] = _mm_cvtsi32_si128(m.read32(marker + 8));
        c->pc = getRegU32(c, 31);
    });
    restore.replace(0x1485f0, [](uint8_t *, R5900Context *c, PS2Runtime *r) {
        auto &m = r->memory();
        if (getRegU32(c, 4) != m.read32(marker + 12) || m.read32(gp - 0x7248) != 1)
            throw std::runtime_error("Story completion state or object changed");
        m.write32(marker + 16, m.read32(marker + 16) + 1);
        c->r[2] = _mm_cvtsi32_si128(0x6543);
        c->pc = getRegU32(c, 31);
    });
    for (uint32_t result : {0u, 1u, 0xffffffffu})
        for (uint32_t alias : {0u, 0x80000000u, 0xa0000000u}) {
            std::memset(ram + object, 0xa5, 128);
            std::memset(ram + marker, 0, 64);
            std::memset(ram + sp - 0x100, 0xa5, 0x110);
            auto &m = runtime.memory();
            m.write32(object + 0x34, 0x660000 | alias);
            m.write32(marker + 4, 0x660000 | alias);
            m.write32(marker + 8, result);
            m.write32(marker + 12, object | alias);
            m.write32(gp - 0x7248, 0xa5a5a5a5);
            R5900Context c{};
            c.pc = 0x295f40;
            c.r[4] = _mm_cvtsi64_si128(int64_t(int32_t(object | alias)));
            c.r[28] = _mm_cvtsi32_si128(gp);
            c.r[29] = _mm_cvtsi32_si128(sp);
            c.r[31] = _mm_cvtsi32_si128(returned);
            c.r[16] = _mm_set_epi32(1, 2, 3, 4);
            const auto saved = c.r[16];
            for (unsigned i = 0; i < 32 && c.pc != returned; ++i) {
                auto f = runtime.lookupFunction(c.pc);
                require(f != nullptr, "Story callback reached an untranslated continuation");
                try {
                    f(ram, &c, &runtime);
                } catch (const EeDispatcherTransfer &) {
                }
            }
            require(c.pc == returned && getRegU32(&c, 29) == sp && getRegU32(&c, 31) == returned &&
                        getRegU32(&c, 28) == gp && std::memcmp(&c.r[16], &saved, 16) == 0,
                    "Story callback lost the caller frame");
            require(m.read32(marker) == 1 && m.read32(marker + 16) == (result ? 1u : 0u) &&
                        m.read32(gp - 0x7248) == (result ? 1u : 0xa5a5a5a5u),
                    "Story callback changed completion handling");
            require(getRegU32(&c, 2) == (result ? 0x6543u : 0u),
                    "Story callback return value changed");
            for (unsigned i = 0; i < 16; ++i)
                require(ram[sp - 0x100 + i] == 0xa5 && ram[sp + i] == 0xa5,
                        "Story callback changed stack guards");
            for (unsigned i = 0; i < 128; ++i)
                if (i < 0x34 || i >= 0x38)
                    require(ram[object + i] == 0xa5, "Story callback modified its object");
        }
}
}
