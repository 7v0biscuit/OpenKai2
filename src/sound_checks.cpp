// SPDX-License-Identifier: GPL-3.0-only
#include "sound_checks.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_imports.h"
#include "audio_queue.h"
#include "audio_buffer.h"
#include <array>
#include <cstring>
#include <memory>
#include <thread>
#include <stdexcept>

namespace openkai2 {
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

void verify_sound_registers() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    for (const uint32_t core : {0u, 1u}) {
        const auto offset = core * 0x400u;
        const auto attr = 0xBF90019Au + offset;
        const auto stat = 0xBF900344u + offset;
        const auto dma = core ? 0xBF801508u : 0xBF8010C8u;
        const auto otherStat = core ? 0xBF900344u : 0xBF900744u;
        memory.reset();
        memory.write16(attr - 2u, 0xABCDu);
        memory.write16(attr, 0x8030u);
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 transfer engine did not become ready");
        require(memory.read16(otherStat) == 0u, "SPU2 transfer state leaked into the other core");
        memory.write32(dma - 4u, 0x00010010u);
        memory.write32(dma, 0x01000201u);
        require((memory.read32(dma) & 0x01000000u) == 0u, "SPU2 DMA did not finish");
        const auto interrupt = memory.takeDmaStart();
        require(interrupt && interrupt->irq == (core ? 0x28 : 0x24), "SPU2 DMA interrupt was lost");
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 DMA completion was lost");
        memory.write16(attr, 0x8000u);
        require((memory.read16(stat) & 0x7FFu) == 0u, "SPU2 reset is stuck on stale DMA status");
        require(memory.read16(attr - 2u) == 0xABCDu, "SPU2 ATTR write corrupted MMIX");
        require(memory.read16(stat & 0x1FFFFFFFu) == memory.read16(stat), "SPU2 KSEG alias differs");
        // Exercise byte and word accesses to the same upper-halfword ATTR.
        memory.write8(attr, 0x20u);
        require((memory.read16(stat) & 0x80u) != 0u, "SPU2 byte write did not enable transfer");
        memory.write32(attr - 2u, 0x80001234u);
        require(memory.read16(stat) == 0u && memory.read16(attr - 2u) == 0x1234u,
                "SPU2 word write did not acknowledge transfer status");
    }

    // Execute a guest reset-and-poll sequence after a completed transfer.
    // A stale status bit makes the real IOP interpreter loop indefinitely.
    memory.reset();
    memory.write32(0x1F8010C8u, 0x01000201u);
    const uint32_t program[] = {
        0x3C08BF90u, // lui t0, 0xbf90
        0xA500019Au, // sh zero, 0x19a(t0)
        0x34098000u, // ori t1, zero, 0x8000
        0xA509019Au, // sh t1, 0x19a(t0)
        0x95020344u, // lhu v0, 0x344(t0)
        0x00000000u, // load delay
        0x304207FFu, // andi v0, v0, 0x7ff
        0x1440FFFCu, // bnez v0, poll
        0x00000000u, // branch delay
        0x24030001u, // addiu v1, zero, 1
        0x03E00008u, // jr ra
        0x00000000u,
    };
    require(memory.writeRam(0x2000u, program, sizeof(program)), "Cannot write SPU2 guest regression");
    IopCpuCore cpu(memory);
    IopCpuState state{};
    state.pc = 0x2000u;
    state.gpr[31] = 0x4000u;
    for (unsigned steps = 0; steps < 100u && state.pc != 0x4000u; ++steps)
        require(cpu.executeInstruction(state) && !state.exception, "SPU2 guest regression raised an exception");
    require(state.pc == 0x4000u && state.gpr[3] == 1u, "IOP sound initialization did not leave its status poll");

    // Sound drivers read packed stream data with LWL/LWR pairs; the second
    // load merges into the first while it is still in its delay slot.
    const uint8_t packed[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    const uint32_t unaligned[] = {
        0x34083000u, // ori t0, zero, 0x3000
        0x89090004u, // lwl t1, 4(t0)
        0x99090001u, // lwr t1, 1(t0)
        0x00000000u, // load delay
        0x01205021u, // addu t2, t1, zero
        0x03E00008u, // jr ra
        0x00000000u,
    };
    require(memory.writeRam(0x3000u, packed, sizeof(packed)) && memory.writeRam(0x2000u, unaligned, sizeof(unaligned)),
            "Cannot write unaligned load regression");
    state = {};
    state.pc = 0x2000u;
    state.gpr[9] = 0xDEADBEEFu;
    state.gpr[31] = 0x4000u;
    for (unsigned steps = 0; steps < 20u && state.pc != 0x4000u; ++steps)
        require(cpu.executeInstruction(state) && !state.exception, "Unaligned load regression raised an exception");
    require(state.gpr[10] == 0x15141312u, "IOP LWL/LWR pair did not assemble the unaligned word");

    // Exercise the actual SPU2 register/DMA boundary with decoded planar PCM.
    std::array<uint8_t,1024> planar{};
    for(unsigned i=0;i<256u;++i){
        const int16_t left=int16_t(i*73u),right=int16_t(-int(i*61u)-1);
        std::memcpy(planar.data()+i*2u,&left,2u);std::memcpy(planar.data()+512u+i*2u,&right,2u);
    }
    for(unsigned core=0;core<2u;++core)for(unsigned partial=0;partial<2u;++partial){
        memory.reset();require(memory.writeRam(0x2000u,planar.data(),planar.size()),"Cannot write PCM fixture");
        const uint32_t chcr=core ? 0x1f801508u : 0x1f8010c8u;
        memory.write16(0x1f9001b0u+core*0x400u,uint16_t(1u<<core));
        memory.write32(chcr-8u,0x2000u);memory.write32(chcr-4u,partial ? 0x00080010u : 0x00100010u);
        memory.write32(chcr,0x01000201u);
        if(partial){
            require(memory.takeAudio().empty(),"Partial AutoDMA exposed an incomplete stereo group");
            memory.write32(chcr-8u,0x2200u);memory.write32(chcr,0x01000201u);
        }
        const auto completion=memory.takeDmaStart();
        require(completion && completion->delayCycles==(partial ? 128u : 256u)*768u,
            "AutoDMA completion does not match the 48 kHz stereo frame duration");
        auto audio=memory.takeAudio();require(audio.size()==1u && audio[0].core==core,"AutoDMA lost its core or block");
        for(unsigned i=0;i<256u;++i)require(audio[0].samples[i*2u]==int16_t(i*73u)
            && audio[0].samples[i*2u+1u]==int16_t(-int(i*61u)-1),"AutoDMA changed stereo order or signed PCM");
        memory.write16(0x1f9001b0u+core*0x400u,0u);
        const auto stopped=memory.takeAudio();require(stopped.size()==1u && stopped[0].stop && stopped[0].core==core,
            "Disabling AutoDMA did not stop its PCM stream");
        memory.write32(chcr,0x01000201u);
        require(memory.takeAudio().empty(),"Ordinary voice DMA was treated as PCM");
        memory.write16(0x1f9001b0u+core*0x400u,uint16_t(1u<<core));memory.write32(chcr,0x01000200u);
        require(memory.takeAudio().empty(),"SPU2 readback was treated as output");
        memory.write32(chcr-8u,IopMemory::RamSize-64u);memory.write32(chcr,0x01000201u);
        require(memory.takeAudio().empty(),"AutoDMA read outside IOP RAM");
        memory.reset();require(memory.takeAudio().empty(),"SPU2 reset retained pending PCM");
    }
    // While AutoDMA plays, the channel reads busy and its address advances one
    // 1 KiB block per 256 frames, as libsd's transfer status reports it.
    uint64_t cycles=0u;memory.setClock(&cycles);memory.reset();
    memory.write16(0x1f9001b0u,1u);memory.write32(0x1f8010c0u,0x4000u);memory.write32(0x1f8010c4u,0x00200010u);
    memory.write32(0x1f8010c8u,0x01000201u);(void)memory.takeDmaStart();
    require((memory.read32(0x1f8010c8u)&0x01000000u) && memory.read32(0x1f8010c0u)==0x4400u,"AutoDMA did not read busy at its first block");
    cycles=256u*768u;require(memory.read32(0x1f8010c0u)==0x4800u,"AutoDMA address did not advance with playback");
    cycles=512u*768u;require(!(memory.read32(0x1f8010c8u)&0x01000000u),"AutoDMA stayed busy after its last frame");
    memory.write32(0x1f8010c8u,0x01000201u);memory.write32(0x1f8010c8u,0u);
    require(!(memory.read32(0x1f8010c8u)&0x01000000u),"Stopping AutoDMA left the channel busy");
    memory.setClock(nullptr);memory.reset();

    auto queue=std::make_unique<StereoAudioQueue>();
    std::vector<int16_t> full((StereoAudioQueue::capacity+2u)*2u,123);
    require(queue->push(full)==StereoAudioQueue::capacity && queue->dropped()==2u,"Audio queue did not bound overflow");
    std::vector<int32_t> mixed(StereoAudioQueue::capacity*2u,7);
    require(queue->mix(mixed)==StereoAudioQueue::capacity && mixed.front()==130 && mixed.back()==130,
        "Audio queue changed accepted samples");
    queue->reset();
    constexpr unsigned total=200000u;
    std::thread producer([&]{
        std::array<int16_t,128> samples{};
        for(unsigned at=0;at<total;){
            const auto count=std::min(64u,total-at);
            for(unsigned i=0;i<count;++i){samples[i*2u]=int16_t((at+i)%30000u);samples[i*2u+1u]=-samples[i*2u];}
            at+=unsigned(queue->push(std::span<const int16_t>(samples.data(),count*2u)));std::this_thread::yield();
        }
    });
    bool valid=true;
    for(unsigned at=0;at<total;){
        std::array<int32_t,126> samples{};const auto count=queue->mix(samples);
        for(unsigned i=0;i<count;++i)valid&=samples[i*2u]==int((at+i)%30000u) && samples[i*2u+1u]==-samples[i*2u];
        at+=unsigned(count);std::this_thread::yield();
    }
    producer.join();require(valid && queue->queued()==0u,"Audio device queue lost order across wraparound");
    const std::array<int16_t,4> oldSamples{1,2,3,4},newSamples{5,6,7,8};
    queue->push(oldSamples);queue->discardPending();queue->push(newSamples);
    std::array<int32_t,8> afterStop{};
    require(queue->mix(afterStop)==2u && afterStop[0]==5 && afterStop[3]==8,
        "Audio stop discarded new samples or retained the stopped stream");

    auto playback=std::make_unique<StereoAudioPlayback>();queue->reset();
    std::vector<int16_t> buffered(StereoAudioPlayback::prefill*2u);
    for(unsigned i=0;i<StereoAudioPlayback::prefill;++i){buffered[i*2u]=12000;buffered[i*2u+1u]=-12000;}
    queue->push(std::span<const int16_t>(buffered.data(),buffered.size()-2u));
    std::array<int32_t,512> rendered{};
    require(playback->mix(*queue,rendered)==0u && queue->available()==StereoAudioPlayback::prefill-1u
        && std::all_of(rendered.begin(),rendered.end(),[](int32_t v){return v==0;}),
        "Audio prefill consumed a short jitter burst");
    queue->push(std::span<const int16_t>(buffered.data(),2u));
    require(playback->mix(*queue,rendered)==256u,"Audio did not start after its prefill");
    for(unsigned i=0;i<256u;++i){const int expected=12000*int(std::min(i+1u,128u))/128;
        require(rendered[i*2u]==expected && rendered[i*2u+1u]==-expected,
            "Audio startup ramp changed stereo sign or full-level samples");}
    // Once primed, every continuous sample retains its value and order.
    std::vector<int32_t> remaining((StereoAudioPlayback::prefill-256u)*2u);
    require(playback->mix(*queue,remaining)==remaining.size()/2u && remaining.front()==12000
        && remaining.back()==-12000,"Buffered continuous audio changed pitch or channel order");
    rendered={};require(playback->mix(*queue,rendered)==0u && playback->underruns()==1u,
        "Audio starvation did not enter its rebuffer state");
    for(unsigned i=0;i<256u;++i){const int expected=i<128u ? 12000*int(127u-i)/128 : 0;
        require(rendered[i*2u]==expected && rendered[i*2u+1u]==-expected,
            "Audio gap was not ramped to silence");}
    rendered={};playback->mix(*queue,rendered);
    require(playback->underruns()==1u && rendered[0]==0,"An empty stream repeated stale samples");
    // Stop and a replacement stream must retain the new stream, fading across
    // the boundary instead of playing the stopped stream's queued backlog.
    queue->push(buffered);rendered={};playback->mix(*queue,rendered);
    queue->discardPending();std::fill(buffered.begin(),buffered.end(),4000);queue->push(buffered);
    rendered={};require(playback->mix(*queue,rendered)==256u,"Audio stop lost the replacement stream");
    require(rendered[0]==12000*127/128+4000/128 && rendered[1]==-12000*127/128+4000/128
        && rendered[254]==4000 && rendered[255]==4000,
        "Audio stream replacement did not crossfade to its new samples");
    // Replacing a stream during a fade must continue from the last emitted
    // stereo sample, rather than jumping back to its old full-level anchor.
    queue->reset();playback->reset();
    for(unsigned i=0;i<StereoAudioPlayback::prefill;++i){buffered[i*2u]=12000;buffered[i*2u+1u]=-8000;}
    queue->push(buffered);rendered={};playback->mix(*queue,rendered);
    queue->discardPending();
    std::array<int32_t,64> partialFade{};playback->mix(*queue,partialFade);
    const int32_t previousLeft=partialFade[62],previousRight=partialFade[63];
    require(previousLeft==9000 && previousRight==-6000,"Audio fade did not reach its expected intermediate sample");
    // Advance a stop boundary while the first fade is still audible.
    const std::array<int16_t,2> discarded{123,456};queue->push(discarded);queue->discardPending();
    std::array<int32_t,2> nextSample{};playback->mix(*queue,nextSample);
    require(nextSample[0]==previousLeft*127/128 && nextSample[1]==previousRight*127/128,
        "Repeated audio stop restarted an old louder fade");
    rendered={};playback->mix(*queue,rendered);
    queue->push(discarded);queue->discardPending();nextSample={};playback->mix(*queue,nextSample);
    require(nextSample[0]==0 && nextSample[1]==0,"A later audio stop resurrected a stale signal after silence");
    // A replacement arriving halfway through that tail crossfades from the
    // audible intermediate level, with independent left and right channels.
    queue->reset();playback->reset();queue->push(buffered);rendered={};playback->mix(*queue,rendered);
    queue->discardPending();partialFade={};playback->mix(*queue,partialFade);
    std::fill(buffered.begin(),buffered.end(),4000);queue->push(buffered);queue->discardPending();queue->push(buffered);
    nextSample={};require(playback->mix(*queue,nextSample)==1u,"Audio fade replacement was not consumed");
    require(nextSample[0]==9000*127/128+4000/128 && nextSample[1]==-6000*127/128+4000/128,
        "Audio crossfade ignored its most recent emitted stereo sample");
    // Consumer-only trimming after overflow bounds stale playback latency.
    queue->reset();playback->reset();
    std::vector<int16_t> overflow((StereoAudioQueue::capacity+2u)*2u);
    for(unsigned i=0;i<StereoAudioQueue::capacity+2u;++i){overflow[i*2u]=int16_t(i%30000u);overflow[i*2u+1u]=-overflow[i*2u];}
    queue->push(overflow);rendered={};playback->mix(*queue,rendered);
    require(playback->trimmedFrames()==StereoAudioQueue::capacity-4096u
        && queue->available()==4096u-256u && queue->dropped()==2u,
        "Audio overflow retained unbounded stale latency");
    const auto retained=StereoAudioQueue::capacity-4096u+128u;
    require(rendered[256]==int32_t(retained%30000u) && rendered[257]==-rendered[256],
        "Audio overflow trimming changed the retained stereo sequence");

    // Import-cache hits must observe code edits, table edits, aliases and reset.
    memory.reset();IopImportRegistry imports(memory);
    constexpr uint32_t table=0x4000u,stub=table+20u;
    auto install=[&]{memory.write32(table,0x41e00000u);memory.write16(table+8u,0x105u);
        const char name[8]={'l','i','b','s','d',0,0,0};memory.writeRam(table+12u,name,8u);
        memory.write32(stub,0x03e00008u);memory.write32(stub+4u,0x24000005u);};
    install();require(imports.decode(stub)->ordinal==5u,"Cannot decode import fixture");
    for(unsigned i=0;i<32u;++i){memory.write32(0x9000u+i*4u,i);require(imports.decode(stub|0x80000000u)->ordinal==5u,
        "Cached import changed through a KSEG alias");}
    require(imports.decodeScans()==1u && imports.decodeHits()==32u,"Unrelated stack writes invalidated the import cache");
    memory.write32(stub+4u,0x24000006u);require(imports.decode(stub)->ordinal==6u,"Import cache retained an edited ordinal");
    memory.write16(table+8u,0x102u);require(imports.decode(stub)->version==0x102u,"Import cache retained an edited version");
    memory.write8(table+12u,'x');require(imports.decode(stub)->library=="xibsd","Import cache retained an edited library");
    memory.write32(table,0u);require(!imports.decode(stub),"Import cache retained an erased table");
    install();require(imports.decode(stub)->ordinal==5u,"Negative import cache hid a newly loaded table");
    memory.reset();install();require(imports.decode(stub)->version==0x105u,"Memory reset retained a cached module");
}
} // namespace openkai2
