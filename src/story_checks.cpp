// SPDX-License-Identifier: GPL-3.0-only
#include "story_checks.h"
#include "openkai2_translation_entries.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <stdexcept>
#include <vector>
namespace openkai2 {
void verify_story_callbacks(PS2Runtime &runtime, uint8_t *ram) {
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
} // namespace openkai2
