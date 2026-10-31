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
VectorWorkResult vector_transfer_workload(unsigned seed,unsigned budget,unsigned iterations) {
    if(seed>=32u || !budget || !iterations)throw std::runtime_error("Invalid vector transfer workload");
    auto memory=std::make_unique<PS2Memory>();
    if(!memory->initialize())throw std::runtime_error("Vector transfer memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(memory->getGSVRAM(),PS2_GS_VRAM_SIZE,&memory->gs());
    auto vu=std::make_unique<VU1Interpreter>();
    auto* code=memory->getVU1Code();auto* data=memory->getVU1Data();
    constexpr unsigned sizes[]={PS2_VU1_DATA_SIZE,4096u,1021u,257u};
    constexpr unsigned loops[]={0u,1u,2u,7u,31u,255u,1023u,4095u};
    const unsigned size=sizes[seed/8u],count=loops[seed%8u];
    const unsigned format=count>31u ? 2u : seed%3u;
    const unsigned source=(size-32u)/16u*16u;
    const bool chain=size==PS2_VU1_DATA_SIZE && count<=31u;
    const bool second=size==PS2_VU1_DATA_SIZE;
    const unsigned firstBytes=16u+(format==1u ? ((count+1u)&~1u)*8u : count*16u);
    auto special=[](unsigned op,unsigned reg){return 0x80000000u|(reg<<11u)|((op&0x7cu)<<4u)|(op&3u)|0x3cu;};
    auto pair=[&](unsigned i,uint32_t lo,uint32_t hi){const uint64_t value=lo|(uint64_t(hi)<<32u);memory->write64(PS2_VU1_CODE_BASE+i*8u,value);};
    for(unsigned i=0;i<8u;++i)pair(i,0u,0x2ffu);
    pair(0u,special(0x6cu,1u),0x2ffu);
    pair(1u,0x02000000u|((1u+seed%15u)<<21u)|(2u<<16u)|(2u<<11u),0x2ffu);
    pair(2u,0u,(15u<<21u)|(3u<<16u)|(2u<<11u)|(4u<<6u)|0x2au);
    if(second)pair(3u,special(0x6cu,4u),0x2ffu);
    pair(4u,0u,0x400002ffu);
    uint64_t hash=14695981039346656037ull,packets=0u;
    auto addBytes=[&](const void* ptr,unsigned count){const auto* p=static_cast<const uint8_t*>(ptr);for(unsigned i=0;i<count;++i)hash=(hash^p[i])*1099511628211ull;};
    auto add=[&](const auto& value){addBytes(&value,sizeof(value));};
    auto snapshot=[&]{const auto& s=vu->state();add(s.vf);add(s.vi);add(s.acc);add(s.q);add(s.p);add(s.i);add(s.r);add(s.pc);add(s.mac);add(s.clip);add(s.status);add(s.cycles);add(s.ebit);add(s.branchPending);add(s.branchTarget);add(s.branchDelay);addBytes(data,size);};
    memory->setGifPacketCallback([&](const uint8_t* bytes,uint32_t count){
        ++packets;add(count);add(vu->state().cycles);addBytes(bytes,count);
        if(second){uint32_t changed=0xa5b6c7d8u;std::memcpy(data+4112u,&changed,4u);}
    });
    const auto started=std::chrono::steady_clock::now();
    for(unsigned run=0;run<iterations;++run){
        vu->reset();for(unsigned i=0;i<size;++i)data[i]=uint8_t(i*37u+seed*11u);
        auto tag=[&](unsigned offset,uint64_t value){for(unsigned i=0;i<16u;++i)data[(offset+i)%size]=i<8u ? uint8_t(value>>(i*8u)) : 0u;};
        tag(source,uint64_t(count)|(chain ? 0u : 0x8000u)|(uint64_t(format)<<58u)|(1ull<<60u));
        if(chain)tag((source+firstBytes)%size,0x8003u|(2ull<<58u));
        if(second)tag(4096u,0x8007u|(2ull<<58u));
        vu->state().vi[1]=int32_t(source/16u);vu->state().vi[2]=int32_t(count ? (source+16u)%size/16u : 4u);vu->state().vi[4]=256;
        for(unsigned lane=0;lane<4u;++lane){vu->state().vf[2][lane]=float(lane+1u);vu->state().vf[3][lane]=0.5f;}
        vu->execute(code,PS2_VU1_CODE_SIZE,data,size,*gs,memory.get(),0u,3u,7u,budget);
        if(iterations==1u)snapshot();
        for(unsigned slice=0;slice<8u;++slice){vu->resume(code,PS2_VU1_CODE_SIZE,data,size,*gs,memory.get(),3u,7u,budget);if(iterations==1u)snapshot();}
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
    if(iterations!=1u)snapshot();add(packets);
    return {hash,vu->state().cycles,packets,double(elapsed)/iterations};
}
void verify_vector_transfers() {
    constexpr unsigned budgets[]={1u,3u,17u,4096u};
    constexpr uint64_t expected[]={
        0x61fdd7d9d367351full,0x279c064aa0e3fce1ull,0x63ac4b8e86b7774bull,0xaf7b069f50b0e7b7ull,
        0x9c27d25adb57ed17ull,0xb0e593189defb35eull,0x677283835f65fdafull,0xca2ead42f37ef7ebull,
        0x955dba464f1ae9c8ull,0x9f41fea4246fbee2ull,0xdc40dc51b2dd3f05ull,0xfc3eba3e315bf9ull,
        0x15df6806e6847785ull,0x5f2e94f52e7c469eull,0x18c5678f8fe41eb8ull,0x2eeb8e6fbad32a14ull,
        0x3b60225b4ce41c59ull,0xda971c57bc5a2e97ull,0xdd506abc9543d06bull,0xb8eb1bec1b417a6eull,
        0xcd25073d594a1496ull,0x420f8267aff1cd10ull,0x97138caf57c76d3ull,0xe91a411c5587e868ull,
        0x2bc480a3d3aafab3ull,0x372a1e911e4a05b1ull,0xdd2245de176b94daull,0xda9c0f09110a6185ull,
        0x1d7cef47c803aa97ull,0xf910fa5e2006c68cull,0xbfe28db8655ff52bull,0x10bf38e37f319284ull,
        0xfd3b15b672a38dcull,0x56996415c2cb649aull,0xc52ea08580fee0f2ull,0xb5fa0807eaf06d66ull,
        0x1584584ca0d0d4b8ull,0x4f73664a1ed6f6beull,0x5177b3f730f29caull,0x827674f430d31636ull,
        0x719e3bcd3a2f33fcull,0x935031d8de9f46baull,0x7568dbc8b013f502ull,0x509d316c6035739eull,
        0xaa62c054118ed854ull,0xf0e87a1a329ef682ull,0xe5fa92316398b22bull,0x5a8ee172aebd9f3full,
        0x621a060a33518ceull,0xa67f8215da8c56cull,0xaeac6a96d521ade1ull,0xea4a8426b396fe79ull,
        0xfb3b5dc1958862beull,0xe5b642df56be4bdcull,0xdb46793bab431153ull,0x518128fbfdb45667ull,
        0x230888c5f9c36cc3ull,0x60e1244ec29bcbfdull,0xf38958160e9edb18ull,0x7bd860096bffe21cull,
        0x66e72134098027d2ull,0x6d287fcd41dbb8bdull,0x9a36c7f5f363777cull,0xb61a4322c669a05cull,
        0x5d0f34c4b38009ffull,0xc9852c29db9773e6ull,0xa2297e629e2d3c52ull,0xfd542ed1c92e83e2ull,
        0xea32e860ce81568aull,0x48a8d6d16866582bull,0x408eaf33760df8d3ull,0xe20c5ad7c4428853ull,
        0x4d7b9b6998dcd152ull,0xee76db3218f41a77ull,0xf04efa02cec1e1f7ull,0x4d7590d4e4110527ull,
        0x86d7570793418e03ull,0xcfe6636573cf2816ull,0xed3fe225c78e6cf9ull,0x2afb3d8890d068c1ull,
        0xaa3ffdca139c2f6eull,0x8f4c09c5dc3f2353ull,0xd656afa6e8d3499aull,0x4b2454f7b39765aaull,
        0xb0090c5e653de8dcull,0x17698ee8c5f38a5dull,0x584d8b3c8c736040ull,0x99f3279c2ba1fa98ull,
        0xfcd608d75b0cd015ull,0x2a62e18de6cd1e3ull,0x2dc4a2dcef26936aull,0x604e3bfafff63faull,
        0xa6430b5e1f4b634full,0x42dbb088ddefd001ull,0x191a8346abc598a2ull,0x4052d5b4564b3da6ull,
        0x8ce66c7e7fdb1d22ull,0x2a689a83c72181e8ull,0xbd3ca66df018f36cull,0x5032cda3b77f044ull,
        0x5203bd6611f47be4ull,0x84835bfa1e17a736ull,0xb8419abe944f76feull,0x7c921934316ce776ull,
        0x84e3d04f932433f5ull,0x90981104705d5591ull,0x463dcaa3edf37093ull,0x933894d064fed003ull,
        0xcef0c628c81d3ea0ull,0xbb4aa9585cce39beull,0xa3f671cd50aec509ull,0xc3ff7b0020ac1b71ull,
        0x514390d2b03e9f8aull,0xd351df444eecc8a0ull,0xbf673f299b1d424full,0xc9a725f2471079b7ull,
        0x52c7328af25953edull,0xd367a8c282998a87ull,0xd506936b00a04a1cull,0x795380ed1593b914ull,
        0x55bf52135ca0ce8bull,0xbcc2352b1684ac32ull,0x4634b53d580978f3ull,0x254c9e7147d51abull,
        0xb76edff0b66a724bull,0x64fe524a87d46712ull,0x2b4fbb3c084212bdull,0x8158399429844771ull,
    };
    static_assert(sizeof(expected)/sizeof(expected[0])==128u);
    for(unsigned seed=0;seed<32u;++seed)for(unsigned i=0;i<4u;++i)
        if(vector_transfer_workload(seed,budgets[i],1u).hash!=expected[seed*4u+i])
            throw std::runtime_error("Vector transfer reference mismatch: "+std::to_string(seed*4u+i));
}
}
