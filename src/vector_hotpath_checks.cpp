// SPDX-License-Identifier: GPL-3.0-only
#include "vector_hotpath_checks.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cfenv>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>
namespace openkai2 {
namespace {
struct Hash {
    uint64_t value=14695981039346656037ull;
    void bytes(const void* data,unsigned size){auto p=static_cast<const uint8_t*>(data);for(unsigned i=0;i<size;++i)value=(value^p[i])*1099511628211ull;}
    template<class T>void add(const T& v){bytes(&v,unsigned(sizeof(v)));}
};
uint32_t random_word(uint32_t& state){state^=state<<13u;state^=state>>17u;state^=state<<5u;return state;}
float from_bits(uint32_t bits){float value;std::memcpy(&value,&bits,4u);return value;}
constexpr uint32_t edges[]={0u,0x80000000u,1u,0x80000001u,0x007fffffu,0x00800000u,0x00800001u,0x01000000u,
    0x30000000u,0x307fffffu,0x30800000u,0x30800001u,0x3f000000u,0x3f800000u,0x40000000u,
    0x4effffffu,0x4f000000u,0x6f800000u,0x70000000u,0x7effffffu,0x7f000000u,0x7f7ffffeu,0x7f7fffffu,
    0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0xbf800000u,0x80800000u,0xff7fffffu};
float sample(unsigned group,uint32_t& state,unsigned lane){
    const auto bits=random_word(state);
    if(group==0u || group==1u || group==3u)return from_bits((bits&0x807fffffu)|((97u+(bits>>24u)%61u)<<23u));
    return from_bits(edges[(bits+lane)%std::size(edges)]^((bits&1u)<<31u));
}
void add_state(Hash& h,const VU1State& s){
    h.add(s.vf);h.add(s.vi);h.add(s.acc);h.add(s.q);h.add(s.p);h.add(s.i);h.add(s.r);h.add(s.pc);h.add(s.mac);h.add(s.clip);h.add(s.status);
    h.add(s.cycles);h.add(s.ebit);h.add(s.haltAfterDelaySlot);h.add(s.stoppedByD);h.add(s.stoppedByT);h.add(s.branchPending);h.add(s.branchTarget);h.add(s.branchDelay);h.add(s.top);h.add(s.itop);
}
void append(std::vector<uint8_t>& packet,uint32_t word){const auto old=packet.size();packet.resize(old+4u);std::memcpy(packet.data()+old,&word,4u);}
void add_vif(Hash& h,PS2Memory& m){
    h.bytes(m.getVU1Data(),PS2_VU1_DATA_SIZE);const auto& r=m.vif1_regs;
    h.add(r.stat);h.add(r.fbrst);h.add(r.err);h.add(r.mark);h.add(r.cycle);h.add(r.mode);h.add(r.num);h.add(r.mask);h.add(r.code);
    h.add(r.itops);h.add(r.base);h.add(r.ofst);h.add(r.tops);h.add(r.itop);h.add(r.top);h.add(r.row);h.add(r.col);
}
}
HotpathResult arithmetic_workload(unsigned group,unsigned iterations){
    if(group>=8u || !iterations)throw std::runtime_error("Invalid arithmetic workload");
    struct Restore {std::fenv_t state;Restore(){std::fegetenv(&state);}~Restore(){std::fesetenv(&state);}} restore;
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Arithmetic memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(m->getGSVRAM(),PS2_GS_VRAM_SIZE,&m->gs());auto vu=std::make_unique<VU1Interpreter>();
    std::vector<unsigned> ops;for(unsigned op=0;op<16u;++op)ops.push_back(op);for(unsigned op=0x18u;op<=0x1cu;++op)ops.push_back(op);ops.push_back(0x1eu);
    for(unsigned op=0x20u;op<=0x2au;++op)ops.push_back(op);for(unsigned op=0x2cu;op<=0x2eu;++op)ops.push_back(op);
    constexpr int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    Hash hash;uint64_t operations=0;const auto start=std::chrono::steady_clock::now();
    for(unsigned iteration=0;iteration<iterations;++iteration)for(unsigned special=0;special<2u;++special)for(auto op:ops)for(unsigned maskCase=0;maskCase<4u;++maskCase){
        const unsigned mask=(group*4u+maskCase)&15u;
        const uint32_t opcode=special ? 0x3cu|(op&3u)|((op&0x7cu)<<4u) : (3u<<6u)|op;
        const uint32_t upper=(mask<<21u)|(2u<<16u)|(1u<<11u)|opcode|0x40000000u;
        m->write64(PS2_VU1_CODE_BASE,uint64_t(upper)<<32u);m->write64(PS2_VU1_CODE_BASE+8u,uint64_t(0x2ffu)<<32u);
        for(unsigned seed=0;seed<32u;++seed){
            vu->reset();auto& s=vu->state();uint32_t rng=0x3bd39e11u^(group*0x10203u+op*451u+seed*97u+mask*17u+special);
            if(group==7u)std::fesetround(modes[seed&3u]);
            for(unsigned lane=0;lane<4u;++lane){
                s.vf[1][lane]=sample(group,rng,lane);s.vf[2][lane]=sample(group,rng,lane+1u);s.acc[lane]=sample(group,rng,lane+2u);
                if(group==1u){volatile float product=s.vf[1][lane]*s.vf[2][lane];s.acc[lane]=(seed&1u) ? product : -product;}
                if(group==3u){s.vf[1][lane]=(seed+lane)%3u ? float(int(seed%7u)-3)*0.125f : 0.0f;s.vf[2][lane]=float(lane+1u)*0.25f;s.acc[lane]=float(int(seed%5u)-2)*0.5f;}
            }
            s.q=sample(group,rng,5u);s.i=sample(group,rng,6u);s.status=(seed*37u)&0xfffu;s.mac=(seed*13u)&0xffffu;
            vu->execute(m->getVU1Code(),PS2_VU1_CODE_SIZE,m->getVU1Data(),PS2_VU1_DATA_SIZE,*gs,m.get(),0u,3u,7u,16u);
            if(iterations==1u)add_state(hash,s);++operations;
        }
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if(iterations!=1u)add_state(hash,vu->state());return {hash.value,operations,double(elapsed)/operations};
}
HotpathResult unpack_workload(unsigned group,unsigned iterations){
    if(group>=8u || !iterations)throw std::runtime_error("Invalid unpack workload");
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Unpack memory initialization failed");
    constexpr unsigned counts[]={1u,3u,17u,256u};Hash hash;uint64_t operations=0;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned iteration=0;iteration<iterations;++iteration)for(unsigned vn=0;vn<4u;++vn)for(unsigned vl=0;vl<4u;++vl){
        if(vl==3u && vn!=3u)continue;
        for(unsigned sign=0;sign<2u;++sign)for(auto count:counts)for(unsigned wrap=0;wrap<2u;++wrap){
            m->write32(0x10003c10u,1u);for(unsigned i=0;i<PS2_VU1_DATA_SIZE;++i)m->getVU1Data()[i]=uint8_t(i*13u+(i>>8u)*7u+group*19u);
            const unsigned cl=group==5u ? (wrap ? 4u : 1u) : group==2u ? 3u : 1u;
            const unsigned wl=group==5u ? (wrap ? 2u : 4u) : group==2u ? 3u : 1u;
            const unsigned mode=group==1u ? 3u : group==4u ? 1u+wrap : 0u;
            std::vector<uint8_t> config;append(config,0x01000000u|cl|(wl<<8u));append(config,0x05000000u|mode);
            append(config,0x30000000u);for(unsigned i=0;i<4u;++i)append(config,0x12233440u+i*123u);
            append(config,0x31000000u);for(unsigned i=0;i<4u;++i)append(config,0x85314270u+i*913u);
            append(config,0x20000000u);append(config,wrap ? 0xe4e4e4e4u : 0x1b1b1b1bu);
            m->processVIF1Data(config.data(),unsigned(config.size()));m->vif1_regs.tops=1007u;
            const bool masked=group==3u;const unsigned components=vn+1u;
            const unsigned bytes=vl==0u ? components*4u : vl==1u ? components*2u : vl==2u ? components : 2u;
            const unsigned sources=cl>=wl ? count : (count/wl)*cl+std::min(count%wl,cl);
            const unsigned payload=(sources*bytes+3u)&~3u;
            unsigned address=wrap ? 1019u : 5u;if(group==7u)address=65u;
            const uint32_t cmd=((0x60u|(masked ? 0x10u : 0u)|(vn<<2u)|vl)<<24u)|((count&255u)<<16u)|address|(sign ? 0x4000u : 0u)|((group==2u) ? 0x8000u : 0u)|((group==3u) ? 0x80000000u : 0u);
            std::vector<uint8_t> packet;append(packet,cmd);const auto base=packet.size();packet.resize(base+payload);
            for(unsigned i=0;i<payload;++i)packet[base+i]=uint8_t(i*37u+vn*17u+vl*5u+count);
            if(group!=6u && group!=7u)append(packet,0x07001234u);
            unsigned size=unsigned(packet.size());if(group==6u)size-=wrap ? 1u : std::min(3u,payload);
            if(group==7u){auto* source=m->getVU1Data()+1024u;std::memcpy(source,packet.data(),size);m->processVIF1Data(source,size);}
            else m->processVIF1Data(packet.data(),size);
            if(iterations==1u)add_vif(hash,*m);++operations;
        }
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if(iterations!=1u)add_vif(hash,*m);return {hash.value,operations,double(elapsed)/operations};
}
HotpathResult unpack_stream_workload(unsigned group,unsigned iterations){
    if(group>=8u || !iterations)throw std::runtime_error("Invalid unpack stream workload");
    constexpr unsigned formats[]={0x6cu,0x6du,0x6eu,0x68u,0x69u,0x6au,0x60u,0x65u};
    const unsigned command=formats[group],components=1u+((command>>2u)&3u),width=command&3u;
    const unsigned size=256u*components*(width==0u ? 4u : width==1u ? 2u : 1u);
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Unpack stream memory initialization failed");
    m->vif1_regs.cycle=0x101u;
    std::vector<uint8_t> packet;append(packet,(command<<24u)|1019u);const auto base=packet.size();packet.resize(base+size);
    for(unsigned i=0;i<size;++i)packet[base+i]=uint8_t(i*37u+group*11u);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)m->processVIF1Data(packet.data(),unsigned(packet.size()));
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    Hash hash;add_vif(hash,*m);return {hash.value,iterations,double(elapsed)/iterations};
}
void verify_vector_arithmetic(){
    constexpr uint64_t extended[8]={0xc57d930b12a73a1cull,0xd844794b989b0368ull,0x73e7c753f02459e5ull,0x8fd3769a13485dffull,0xe1a0c1202bacac96ull,0x6afa4f701ab9e0beull,0xb802b8f23f59be0dull,0xd50905fb5b24c27aull};
    constexpr uint64_t binary64[8]={0xc57d930b12a73a1cull,0xd844794b989b0368ull,0xc6f5d02b46d69851ull,0x8fd3769a13485dffull,0x8a5ff47f08f51deull,0xde715de9961c1820ull,0x752f9f95550e2453ull,0x41abcf473a63fd48ull};
    static_assert(std::numeric_limits<long double>::digits==53 || std::numeric_limits<long double>::digits==64);
    const auto& expected=std::numeric_limits<long double>::digits==53 ? binary64 : extended;
    for(unsigned group=0;group<8u;++group)if(arithmetic_workload(group,1u).hash!=expected[group])throw std::runtime_error("Original arithmetic reference mismatch: "+std::to_string(group));
}
void verify_vif_unpack(){
    constexpr uint64_t expected[8]={0xeda8bf5823fba9c1ull,0x1ae777eba7ed7329ull,0x7e3eb407d3600d5dull,0xd41e243247c1e17bull,0x43508ed25c310d69ull,0xb44eb2b3e01d5049ull,0x18f4348b003bfe6dull,0x7ba978b336a79085ull};
    for(unsigned group=0;group<8u;++group)if(unpack_workload(group,1u).hash!=expected[group])throw std::runtime_error("Original VIF unpack reference mismatch: "+std::to_string(group));
}
}
