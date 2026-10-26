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
// Short programs with repeated MPG-style writes. Include unchanged uploads,
// executed and unused edits, both units, and edits between resume slices.
HotpathResult code_update_workload(unsigned group,unsigned iterations) {
    if(group>=16u || !iterations)throw std::runtime_error("Invalid vector code update workload");
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Code update memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(m->getGSVRAM(),PS2_GS_VRAM_SIZE,&m->gs());
    const bool vu0=(group&8u)!=0u,sliced=(group&4u)!=0u;const unsigned mode=group&3u;
    auto vu=std::make_unique<VU1Interpreter>(vu0 ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);
    const unsigned base=vu0 ? PS2_VU0_CODE_BASE : PS2_VU1_CODE_BASE;
    const unsigned codeSize=vu0 ? PS2_VU0_CODE_SIZE : PS2_VU1_CODE_SIZE,dataSize=vu0 ? PS2_VU0_DATA_SIZE : PS2_VU1_DATA_SIZE;
    auto* code=vu0 ? m->getVU0Code() : m->getVU1Code();auto* data=vu0 ? m->getVU0Data() : m->getVU1Data();
    const uint32_t upper=0x80000000u|(15u<<21u)|(1u<<11u)|(3u<<6u)|0x22u; // ADDi + LOI
    for(unsigned i=0;i<32u;++i)m->write64(base+i*8u,uint64_t((i==17u ? (upper&~(31u<<6u))|(4u<<6u) : upper)|(i==30u ? 0x40000000u : 0u))<<32u|0x3f000000u);
    for(unsigned lane=0;lane<4u;++lane)vu->state().vf[1][lane]=float(lane+1u)*.125f;
    auto edit=[&](unsigned iteration){
        if(!mode)return;
        const unsigned pc=mode==3u ? codeSize-8u : 16u*8u;
        const uint32_t immediate=mode==1u ? 0x3f000000u : 0x3e800000u|((iteration&15u)<<18u);
        m->write64(base+pc,(uint64_t(upper)<<32u)|immediate);
    };
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i){
        if(!sliced)edit(i);
        vu->execute(code,codeSize,data,dataSize,*gs,m.get(),0u,3u,7u,sliced ? 3u : 4096u);
        if(sliced){edit(i);vu->resume(code,codeSize,data,dataSize,*gs,m.get(),3u,7u,4096u);}
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    Hash hash;add_state(hash,vu->state());hash.bytes(data,dataSize);
    return {hash.value,uint64_t(iterations)*32u,double(elapsed)/(iterations*32.0)};
}
void verify_vector_code_updates() {
    // Complete state, cycle and data snapshots from the pinned interpreter.
    constexpr uint64_t expected[]={
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0xbcbedbef4d80259full,0xb5a0b91cfa836fc1ull,0x120ad62b2ea9b15dull,0xfb085bdf15f2fa73ull,
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0xbcbedbef4d80259full,0xb5a0b91cfa836fc1ull,0x120ad62b2ea9b15dull,0xfb085bdf15f2fa73ull,
        0x10fea05cb9dc63feull,0x36b80a4e3fd34dd8ull,0x2e4e35374dfc021cull,0xe0509ece8f105b8aull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
        0x7e474be30c78e59full,0x9f8703aa2982afc1ull,0x89f007a6e333f15dull,0x4d5f870b598cba73ull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
        0x7e474be30c78e59full,0x9f8703aa2982afc1ull,0x89f007a6e333f15dull,0x4d5f870b598cba73ull,
        0xaefd98dbc13de3feull,0x432caeb094f14dd8ull,0x6a969e2ceb27021cull,0x6f6934524688db8aull,
    };
    constexpr unsigned counts[]={1u,3u,7u,19u};
    for(unsigned group=0;group<16u;++group)for(unsigned i=0;i<4u;++i)
        if(code_update_workload(group,counts[i]).hash!=expected[group*4u+i])
            throw std::runtime_error("Original vector code update reference mismatch: "+std::to_string(group));
}
HotpathResult product_workload(unsigned group,unsigned iterations) {
    if(group>=8u || !iterations)throw std::runtime_error("Invalid vector product workload");
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Product memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(m->getGSVRAM(),PS2_GS_VRAM_SIZE,&m->gs());auto vu=std::make_unique<VU1Interpreter>();
    constexpr uint32_t ops[]={0x29u,0x2du,0x08u,0x0eu,0x21u,0x23u,0x25u,0x2eu};
    for(unsigned i=0;i<32u;++i){
        const auto upper=(15u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|ops[group]|(i==30u ? 0x40000000u : 0u);
        m->write64(PS2_VU1_CODE_BASE+i*8u,uint64_t(upper)<<32u);
    }
    auto& state=vu->state();
    for(unsigned lane=0;lane<4u;++lane){state.vf[1][lane]=(lane&1u ? -1.0f : 1.0f)*float(lane+1u)*0.125f;
        state.vf[2][lane]=lane==2u ? -0.0f : float(lane+1u)*0.25f;state.acc[lane]=float(lane+1u)*0.5f;}
    state.q=-0.5f;state.i=1.25f;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)vu->execute(m->getVU1Code(),PS2_VU1_CODE_SIZE,m->getVU1Data(),PS2_VU1_DATA_SIZE,*gs,m.get(),0u,3u,7u,8192u);
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    Hash hash;add_state(hash,vu->state());return {hash.value,uint64_t(iterations)*32u,double(elapsed)/(iterations*32.0)};
}
// Exercise selected FMAC operands while other registers retain large or
// denormal bit patterns. Inactive lanes must not affect the selected result.
HotpathResult operand_workload(unsigned group,unsigned iterations) {
    if(group>=32u || !iterations)throw std::runtime_error("Invalid operand workload");
    auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Operand memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(m->getGSVRAM(),PS2_GS_VRAM_SIZE,&m->gs());auto vu=std::make_unique<VU1Interpreter>();
    constexpr unsigned ops[]={0x28u,0x2au,0x29u,0x08u,0x21u,0x23u,0x2eu,0x18u};
    constexpr unsigned masks[]={1u,3u,5u,10u,14u,8u,12u,6u};
    const unsigned op=ops[group%8u],mask=(group&16u) ? masks[group%8u] : 15u;const bool special=(group&8u)!=0u;
    const unsigned opcode=special ? 0x3cu|(op&3u)|((op&0x7cu)<<4u) : (3u<<6u)|op;
    for(unsigned i=0;i<32u;++i){const unsigned hi=(mask<<21u)|(2u<<16u)|(1u<<11u)|opcode|(i==30u ? 0x40000000u : 0u);m->write64(PS2_VU1_CODE_BASE+i*8u,uint64_t(hi)<<32u);}
    const bool usesAcc=op==0x29u || op==0x08u || op==0x21u || op==0x23u || (op==0x2eu && !special);
    unsigned left=mask,right=mask;
    if(op==0x2eu){left=((mask&8u)>>1u)|((mask&4u)>>1u)|((mask&2u)<<2u);right=((mask&8u)>>2u)|((mask&4u)<<1u)|((mask&2u)<<1u);}
    if(op==0x08u || op==0x18u)right=8u;
    if(op==0x21u || op==0x23u)right=0u;
    auto& state=vu->state();
    for(unsigned lane=0;lane<4u;++lane){
        const unsigned bit=8u>>lane;
        state.vf[1][lane]=(left&bit) ? float(lane+1u)*0.125f : from_bits((lane&1u) ? 1u : 0x7f7fffffu);
        state.vf[2][lane]=(right&bit) ? float(lane+1u)*0.25f : from_bits((lane&1u) ? 0x80000001u : 0xff7fffffu);
        state.acc[lane]=usesAcc && (mask&bit) ? float(lane+1u)*0.5f : from_bits(0x7f7fffffu);
    }
    state.q=op==0x21u ? -0.5f : from_bits(0x7f7fffffu);state.i=op==0x23u ? 1.25f : from_bits(1u);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)vu->execute(m->getVU1Code(),PS2_VU1_CODE_SIZE,m->getVU1Data(),PS2_VU1_DATA_SIZE,*gs,m.get(),0u,3u,7u,8192u);
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    Hash hash;add_state(hash,vu->state());return {hash.value,uint64_t(iterations)*32u,double(ns)/(iterations*32.0)};
}
void verify_vector_operands() {
    // Full state and cycle snapshots from the pinned original interpreter.
    constexpr uint64_t expected[]={
        0xf2454ec8b810bba9ull,0xdc8ae1bce9f5df0bull,0x90da31b813f35fefull,0xf0a9df8f2cead159ull,
        0x2859d5a02666d04eull,0x0ee296f15403ba28ull,0xc331e6ec7e013b0cull,0x81ed18ac9e5af53aull,
        0x538c6d60681535d8ull,0x6d03ac0f3a784bfeull,0xd8f5d77db0f45f02ull,0xf9f92a53f02110ecull,
        0x9ca4e9fb4c084547ull,0xae47a06d1289e365ull,0x7afe3e186a72b221ull,0x1d3a6b8e5547dff7ull,
        0x9f705a2fab1f5380ull,0xfd82464bc60ecff6ull,0xa9f7688d7d7e0acaull,0x055a204ff23806c4ull,
        0x01363511c441ba15ull,0xef937e9ffdc01bf7ull,0x02f0a505a201f043ull,0x80a0b37ebb021f65ull,
        0x719934814bd47575ull,0x5bdec7757db998d7ull,0x670c80a712c8f023ull,0xe6c86a6d2f95be85ull,
        0xf7b2d7fab45de764ull,0x15970cbe2afe72faull,0xea27d822f57587c6ull,0x259e53f6665a26c8ull,
        0x74897b0ae83df1b9ull,0x62e6c49921bc539bull,0x8e00b9b9baa107dfull,0xe9b8b0f6cbff3ac9ull,
        0x6debd01a22665a9eull,0x5474916b50034478ull,0x7f8e868be8e7f8bcull,0x3e49b84c0941b2eaull,
        0x277911a5f7e5619full,0x850edcb29c9a027cull,0x10871fd2e7283b34ull,0x718ad601ad1ca056ull,
        0x148a6984ccda43c0ull,0xe70cb43c71f305a1ull,0x5a17b5dfd0680138ull,0x1fff31907ae97b2aull,
        0x2619d253b5cf97d7ull,0xbdcdd3093f008f97ull,0xd25d75230a7ab3f8ull,0xb164e8d21e6b87e0ull,
        0x5a3ac33761ed410cull,0xdfe02d186a9c7b75ull,0xe7f4985f9c6ce9b7ull,0x33e926fd62f35674ull,
        0x167899ded129f28eull,0xf8e9a495f72d9de8ull,0xad38f491212b1eccull,0x700bdceb491e177aull,
        0x01582130e64386f4ull,0x1b249f5a554ad40aull,0x66800fe48ea91c36ull,0xa60e42520726f9b8ull,
        0xf77e63dc739f77f7ull,0x09211a4e3a211615ull,0x5d0137871f561231ull,0x0149404a0df7df47ull,
        0xf4ffd78e012f70e0ull,0x5311c3aa1c1eed56ull,0xff86e5ebd38e282aull,0x5cf578fb4c14c364ull,
        0xb0393f27094bb2b9ull,0x9e9688b542ca149bull,0xc9b07dd5dbaec8dfull,0x25687512ed0cfbc9ull,
        0x21a188558d07f2b6ull,0xc7a752d379b1b4c0ull,0xb44a2c6dd56fe074ull,0x3282675ab4d672d2ull,
        0xcc0923b722c38df7ull,0xddabda28e9452c15ull,0x318bf761ce7a2831ull,0xd5d40024bd1bf547ull,
        0x25b511e526feff2aull,0x07d0dd21b05e7394ull,0x1b2e038754a047e0ull,0xfff9031d84353cc6ull,
        0x76a9b0cf87e47777ull,0x884c67414e661595ull,0xd3fd1746246894b1ull,0x78452009130a61c7ull,
        0x9dede7f82504486dull,0x47b0841915f643ffull,0x1c968ef87d118fbbull,0x5ddb5d385cb7d58dull,
        0xd5eaa193601cee3bull,0xeba50e9f2e37cad9ull,0x5f7167c7174223edull,0xdfb57e00fa75558bull,
        0xd4df7db8b6abf0e8ull,0xf26e730190a8458eull,0xe795f94a983d2532ull,0x0275ba39cc03f95cull,
        0xeb7d7fe2fb5edebcull,0x114ec76faab03218ull,0x364b8f4e7c146ec0ull,0xab7310d25cd094a5ull,
        0x49dff538ea28e849ull,0xead165de41b0e4ddull,0x51dde4dfd74dbe7dull,0x7dd963f6bc551f7bull,
        0xf6cbe1652164f55bull,0x0b37384f1444aaceull,0xa8344a38b644e26eull,0x44489697ef24d7d9ull,
        0x26d543ab79ab1122ull,0x03c4843a24b4c699ull,0x164331bdd6a93570ull,0x6cfd28c3a5dd5dbdull,
        0xd0e59c4a7217609dull,0x7ebfef056aa29aafull,0xd2a00c3e4fd796cbull,0x0dc148972417feddull,
        0x63a95db836e6e755ull,0x4deef0ac68cc0ab7ull,0x614c17120d0ddf03ull,0xe313dc252da74ca5ull,
    };
    constexpr unsigned counts[]={1u,3u,7u,19u};
    for(unsigned group=0;group<32u;++group)for(unsigned i=0;i<4u;++i)
        if(operand_workload(group,counts[i]).hash!=expected[group*4u+i])throw std::runtime_error("Original selected-operand snapshot differs: "+std::to_string(group*4u+i));
}
void verify_vector_products() {
    // Original pipeline snapshots, including zeros, signed products and cross lanes.
    constexpr uint64_t expected[32]={
        0xd1b0f842b2da4957ull,0xe76b654e80f525f5ull,0x3b4b8287662a2211ull,0xd96ff96349661167ull,
        0x516244d3ac2e8c4dull,0xff3c978ea4b9c65full,0xcbf33539fca2951bull,0x7147192d0e2fd2dull,
        0xb4c73d45c393421bull,0xc669f3b78a14e039ull,0x3a364cdf731f394dull,0xbc863e665a1f0a2bull,
        0x226e990531d3f754ull,0x3c3b172ea0db446aull,0x879687b8da398c96ull,0xc930957356840958ull,
        0x867467feea7891d5ull,0x70b9faf31c5db537ull,0x7be7b424b16d0c83ull,0xfdaf7937d2067a25ull,
        0xc4bc975c7169b72bull,0xd65f4dce37eb5549ull,0xc302276893a980fdull,0x475df43c7e75f11bull,
        0xc6dc3eddc9526897ull,0xd87ef54f8fd406b5ull,0x2c5f1288750902d1ull,0xce9b3ffe5fde30a7ull,
        0x84d0e351b092e954ull,0x9e9d617b1f9a366aull,0xe9f8d20558f87e96ull,0x2b92dfbfd542fb58ull,
    };
    constexpr unsigned counts[]={1u,3u,7u,19u};
    for(unsigned group=0;group<8u;++group)for(unsigned i=0;i<4u;++i)
        if(product_workload(group,counts[i]).hash!=expected[group*4u+i])throw std::runtime_error("Original product pipeline mismatch: "+std::to_string(group*4u+i));
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
