// SPDX-License-Identifier: GPL-3.0-only
#include "vector_checks.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace openkai2 {
VectorWorkResult vector_pipeline_workload(unsigned seed,unsigned budget,unsigned iterations) {
    if(seed>=16u || !budget || !iterations)throw std::runtime_error("Invalid vector pipeline workload");
    auto memory=std::make_unique<PS2Memory>();
    if(!memory->initialize())throw std::runtime_error("Vector pipeline memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(memory->getGSVRAM(),PS2_GS_VRAM_SIZE,&memory->gs());
    const bool vu0=(seed&8u)!=0u;
    auto vu=std::make_unique<VU1Interpreter>(vu0 ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);
    auto* code=vu0 ? memory->getVU0Code() : memory->getVU1Code();
    auto* data=vu0 ? memory->getVU0Data() : memory->getVU1Data();
    const unsigned codeSize=vu0 ? PS2_VU0_CODE_SIZE : PS2_VU1_CODE_SIZE;
    const unsigned dataSize=vu0 ? PS2_VU0_DATA_SIZE : PS2_VU1_DATA_SIZE;
    constexpr uint32_t nop=0x2ffu;
    auto special=[](unsigned op,unsigned source,unsigned target=0u) {
        return 0x80000000u|(target<<16u)|(source<<11u)|((op&0x7cu)<<4u)|(op&3u)|0x3cu;
    };
    for(unsigned pc=0;pc<codeSize;pc+=8u){const uint64_t pair=uint64_t(nop)<<32u;std::memcpy(code+pc,&pair,8u);}
    for(unsigned i=0;i<96u;++i){
        const unsigned from=1u+(i+seed)%8u,to=1u+(i*3u+seed)%8u;
        const unsigned lanes=1u+(i+seed)%15u;
        uint32_t hi=(lanes<<21u)|(2u<<16u)|(from<<11u)|(to<<6u)|((i&1u) ? 0x29u : 0x2au);
        uint32_t lo=0u;
        switch((i+seed)%16u){
        case 0:lo=0x10000000u|(3u<<16u)|(4u<<11u)|((i+3u)&2047u);break;
        case 1:lo=(lanes<<21u)|(to<<16u)|(14u<<11u)|(i&7u);break;
        case 2:lo=0x02000000u|(lanes<<21u)|(14u<<16u)|(from<<11u)|(i&7u);break;
        case 3:lo=special(0x38u,from,2u);break;
        case 4:lo=special(0x3bu,0u);break;
        case 5:lo=0x2a000000u|((i&1u)<<21u)|0x7c0u;break;
        case 6:lo=0x22000000u|(0x123456u^i);break;
        case 7:hi=(lanes<<21u)|(2u<<16u)|(from<<11u)|0x1ffu;break; // CLIP
        case 8:lo=vu0 ? 0u : special(0x76u,from);break;
        case 9:lo=vu0 ? 0u : special(0x7bu,0u);break;
        case 10:lo=special(0x3du,4u,to)|(lanes<<21u);break;
        case 11:lo=special(0x3cu,from,5u);break;
        case 12:lo=vu0 ? 0u : special(0x6cu,15u);break;
        case 13:lo=special(0x39u,0u,2u);break;
        case 14:lo=0x2c000000u|(6u<<16u)|0x7ffu;break;
        default:hi=(lanes<<21u)|(2u<<16u)|(from<<11u)|0x1bcu;break; // MULA
        }
        if(i==94u)hi|=0x40000000u;
        const uint64_t pair=lo|(uint64_t(hi)<<32u);std::memcpy(code+i*8u,&pair,8u);
    }
    uint64_t first;std::memcpy(&first,code,8u);
    memory->write64(vu0 ? PS2_VU0_CODE_BASE : PS2_VU1_CODE_BASE,first);
    uint64_t hash=14695981039346656037ull,packets=0u;
    auto add=[&](const auto& value) {
        const auto* bytes=reinterpret_cast<const uint8_t*>(&value);
        for(unsigned i=0;i<sizeof(value);++i)hash=(hash^bytes[i])*1099511628211ull;
    };
    auto snapshot=[&] {
        const auto& state=vu->state();add(state.vf);add(state.vi);add(state.acc);add(state.q);add(state.p);add(state.i);add(state.r);
        add(state.pc);add(state.mac);add(state.clip);add(state.status);add(state.cycles);add(state.ebit);add(state.haltAfterDelaySlot);
        add(state.stoppedByD);add(state.stoppedByT);add(state.branchPending);add(state.branchTarget);add(state.branchDelay);add(state.top);add(state.itop);
        for(unsigned i=0;i<dataSize;++i)hash=(hash^data[i])*1099511628211ull;
    };
    memory->setGifPacketCallback([&](const uint8_t* bytes,uint32_t count) {
        ++packets;add(count);add(vu->state().cycles);
        for(unsigned i=0;i<count;++i)hash=(hash^bytes[i])*1099511628211ull;
    });
    const auto started=std::chrono::steady_clock::now();
    for(unsigned run=0;run<iterations;++run){
        vu->reset();std::memset(data,0,dataSize);
        for(unsigned reg=1;reg<32u;++reg)for(unsigned lane=0;lane<4u;++lane)
            vu->state().vf[reg][lane]=float(int((seed+reg+lane)%13u)-6)*0.125f;
        vu->state().vi[3]=7;vu->state().vi[4]=-3;vu->state().vi[14]=64;vu->state().vi[15]=48;
        const uint64_t tag=0x8000u;std::memcpy(data+48u*16u,&tag,8u);
        vu->execute(code,codeSize,data,dataSize,*gs,memory.get(),0u,3u,9u,budget);
        if(iterations==1u)snapshot();
        for(unsigned slice=0;slice<8u;++slice){
            vu->resume(code,codeSize,data,dataSize,*gs,memory.get(),3u,9u,budget);
            if(iterations==1u)snapshot();
        }
        // A fresh MSCAL must discard every queued write from the sliced run.
        vu->execute(code,codeSize,data,dataSize,*gs,memory.get(),0u,3u,9u,4096u);
        if(iterations==1u)snapshot();
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
    if(iterations!=1u)snapshot();add(packets);
    return {hash,vu->state().cycles,packets,double(elapsed)/iterations};
}
void verify_vector_pipeline_scheduling() {
    constexpr unsigned budgets[]={1u,2u,3u,4u,7u,13u,31u,127u};
    // Golden snapshots are produced by the original interpreter, including
    // intermediate resume boundaries and the cycle of every PATH1 callback.
    constexpr uint64_t expected[]={
        0x3208448321b7c6ebull,0xb77cd14402a925b4ull,0x16b1cd035bd30e39ull,0x97389e03a439b97ull,
        0xe8643904cbda8aebull,0x31b2c1b8a7323027ull,0x3463571b6a58b075ull,0xc190bc392333b21eull,
        0xb82c58316b7df7c3ull,0x8710ec6f35708926ull,0xda032104ae9ca9c4ull,0xe935b8e2bf1dc19cull,
        0x5490d1fc5d39890ull,0x4f56b52ecb684280ull,0xd6ecef6f0653e3a5ull,0xeac25364b3743421ull,
        0x2e5c3ce0cc55c199ull,0x1704d6359169e4e4ull,0x391d0cecc28ed230ull,0xc39f89ea6deac94eull,
        0x6845cc375b0a7445ull,0xcf448fe2d6ee7a13ull,0x3b3d142818cec237ull,0x85aaa022c34eae9eull,
        0xc26e1daa9f888f57ull,0xc585b548f5865b2bull,0x20612fab3441e89eull,0x2dcab7c4e24b187full,
        0xc49bebbdbd8917e8ull,0xcbbe5949e7f83424ull,0xf155bb6a145f801bull,0x3a5d52d497b34f9bull,
        0x204ecd389a4d3c45ull,0x120a0ba9c0c0db22ull,0x39f80f6486eca318ull,0x1f9fc17413d6441full,
        0x1283f65e46b6dull,0xe006ee8fe112b720ull,0x48ad11179048d5c3ull,0x2467be12bd2cf89ull,
        0x517072d87d00e557ull,0x7fad2e0fd60e506eull,0xfc8ef1e5c73a72a1ull,0x3be60db002cdeee7ull,
        0x40d2b2dcb14294b5ull,0x2759e311be9a09ffull,0xb7847a4761750d46ull,0x8532342b027ff6feull,
        0x21f1fdb727a1b5a5ull,0xeae69ee7970aa912ull,0xe8143c1785f25a24ull,0x43e6a50b2606f50full,
        0xc7d484c89fdc6ed7ull,0xf22a9912d1d1c95dull,0x205a07cd0fe9f6d0ull,0x3f33c399aead0027ull,
        0x7feb8cdd47b7f5c0ull,0x66b993e24d31d1a7ull,0xfce2d898274b5650ull,0x7cd8774e1190df6cull,
        0xe627e3b1d9ed075full,0xd651cff27a90aa66ull,0x67b7c9525098b8bcull,0xc234022eb9d202c4ull,
        0x1357b7ef49b7dc6cull,0x610e2293be329f98ull,0x928518e5f57c5f64ull,0x5309b09e2a642c94ull,
        0x4af343dd29f7273full,0xb6d2221c3fd5293cull,0xf45df20e51214edaull,0xeb7e4264b3443a06ull,
        0x564c77e07a68dbe9ull,0x826898e3bba2e4e0ull,0x98ee5b4fbadc64c1ull,0xbc74cae770eebd24ull,
        0xfd7140a88d9e205dull,0x14d846a29eabda9full,0x954e36eb7e10bd58ull,0x2ae3fe8da09f3094ull,
        0xdff02044f5c029b4ull,0x1190a0f74db19c6cull,0x628f015700d660eeull,0xef4c14ee3d6ddbe8ull,
        0x28bd71548baed0e0ull,0xa460f593afc97aebull,0x4657d8a032690f8full,0xe4191067a827c1f7ull,
        0x6a5ecbd8c64de470ull,0xd591bcfc5a5a4ab0ull,0x1f96ee15b4d1c55aull,0xa9a8134f159cf570ull,
        0xa795ef58dedec2e7ull,0x4e8da58ddb2068ceull,0xf1033b76346208a8ull,0x1b2ccf2d62b6d29ull,
        0x678804e5d1f0c997ull,0x74408347b001762cull,0xfcad5f4ffdb84443ull,0xd40866609af03eeeull,
        0xedafcb2a29830a71ull,0x37beac8a998e4b84ull,0xda3d5b151bdd5587ull,0x2c889b2ca621a3b7ull,
        0x8a061aa85a5fd726ull,0xd0a713b1f044f483ull,0x51de24dee44ceb3cull,0x442476c27624167cull,
        0xb218c772ee8adadcull,0x77bc66fd4abe017dull,0xbcbfcea56b834431ull,0xca26f1071d7f7373ull,
        0x6c9d6d261320b09bull,0x4c656c09b030f27dull,0xb164230d334ca10aull,0xe88e62780b35ffeull,
        0xc15342955ffc63ccull,0xf762731b0ff7474full,0x1c0666ed19315745ull,0x56bf6ea7d4207f48ull,
        0xcb17188cb0326ecbull,0x11a7b47152be461aull,0xf3ad5b1f97fc7e09ull,0x7909c44909ae8f99ull,
        0xf29cf6d7fbef512full,0xbd1c107c61e63127ull,0x902d4917f8c76068ull,0x8cb0d36ec09a70f2ull,
    };
    static_assert(sizeof(expected)/sizeof(expected[0])==128u);
    for(unsigned seed=0;seed<16u;++seed)for(unsigned i=0;i<8u;++i){
        const auto result=vector_pipeline_workload(seed,budgets[i],1u);
        if(result.hash!=expected[seed*8u+i])throw std::runtime_error("Vector pipeline reference mismatch: "+std::to_string(seed*8u+i));
    }
}
}
