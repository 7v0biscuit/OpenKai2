// SPDX-License-Identifier: GPL-3.0-only
#include "vector/backend.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "vector/pcsx2_bridge.h"
#include "work_timings.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
namespace openkai2 {
namespace {
thread_local bool referenceBackend = false;
enum class Backend { legacy, interpreter, microvu };
Backend backend() {
    if (referenceBackend)
        return Backend::legacy;
    static const Backend selected = [] {
        const char *option = std::getenv("OPENKAI2_VECTOR_BACKEND");
        if (option && std::strcmp(option, "legacy") == 0)
            return Backend::legacy;
        if (option && std::strcmp(option, "interpreter") == 0)
            return Backend::interpreter;
        if (bridgeInitializeNative())
            return Backend::microvu;
        std::cerr << "[vector:engine] backend=legacy native_unavailable=1\n";
        return Backend::legacy;
    }();
    return selected;
}
void submit(void *context, const uint8_t *packet, uint32_t size) {
    RuntimeWorkTimer graphics(RuntimeWork::path1);
    static_cast<PS2Memory *>(context)->submitGifPacket(GifPathId::Path1, packet, size);
}
}
bool run_vector_backend(VU1State &state, uint8_t *code, uint32_t codeSize, uint8_t *data,
                        uint32_t dataSize, GS &, PS2Memory *memory, uint32_t budget, bool fresh) {
    if (!memory || code != memory->getVU1Code() || codeSize != 0x4000u || dataSize != 0x4000u ||
        backend() == Backend::legacy)
        return false;
    RuntimeWorkTimer work(RuntimeWork::vu1);
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::cout << "[vector:engine] backend=pcsx2-"
                  << (backend() == Backend::microvu ? "microvu" : "interpreter")
                  << " source=dcfd663c1903025864a865d3fcf508d0caab70c7\n";
    }
    bridgeRun(state, code, data, budget, fresh, memory->getVU1CodeGeneration(), submit, memory,
              &state, backend() == Backend::microvu);
    return true;
}
void reset_vector_backend(const void *owner) { bridgeForget(owner); }
VectorReferenceScope::VectorReferenceScope() : previous_(referenceBackend) {
    referenceBackend = true;
}
VectorReferenceScope::~VectorReferenceScope() { referenceBackend = previous_; }
}
