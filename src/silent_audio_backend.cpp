// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/ps2_audio.h"

// Host playback is suspended while the runtime's frame budget is addressed.
// Guest sound RPC responses, DMA completion, and interrupts remain in the IOP.
struct PS2AudioBackend::Impl {};
PS2AudioBackend::PS2AudioBackend() : m_impl(std::make_unique<Impl>()) {}
PS2AudioBackend::~PS2AudioBackend()=default;
void PS2AudioBackend::onVagTransfer(const uint8_t*,uint32_t,uint32_t) {}
void PS2AudioBackend::onVagTransferFromBuffer(const uint8_t*,uint32_t,uint32_t) {}
void PS2AudioBackend::onSoundCommand(uint32_t,uint32_t,const uint8_t*,uint32_t,uint8_t*,uint32_t) {}
void PS2AudioBackend::play(uint32_t,float,float,uint32_t) {}
void PS2AudioBackend::stop(uint32_t) {}
void PS2AudioBackend::stopAll() {}
void PS2AudioBackend::playDecodedSample(uint32_t,DecodedSample&,float,float,bool) {}
void PS2AudioBackend::pruneFinishedSounds() {}
