// SPDX-License-Identifier: GPL-3.0-only
#include "stream_checks.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime stream callback regression: ")+message);
}
}
void verify_stream_callbacks(PS2Runtime& runtime,uint8_t* memory) {
    require(runtime.hasFunction(0x0016cda0u),"logged movie cleanup target is not registered");
    constexpr uint32_t table=0x00367b68u, record=0x24000u, chunk=0x24200u;
    constexpr uint32_t buffer=0x26000u, output=0x24300u;
    constexpr uint32_t stack=0x01fffe40u, returned=0x0019de98u;
    for (const auto address:translation::stream_callback_entries)
        require(runtime.hasFunction(address),"SJRBF method is not registered");
    std::memcpy(memory+table,translation::stream_callback_slots.data(),translation::stream_callback_slots.size()*4);
    // Exercise the actual stream methods with their optional critical-section
    // hooks unset. Those hooks belong to the independent EE scheduler checks.
    runtime.memory().write32(0x003c07a0u,0u);
    runtime.memory().write32(0x003c07a8u,0u);
    auto call=[&](uint32_t entry,std::array<uint32_t,4> args) {
        R5900Context context{};
        context.pc=entry;
        for (unsigned i=0;i<args.size();++i) context.r[4+i]=_mm_cvtsi32_si128(args[i]);
        for (unsigned reg=16;reg<=23;++reg) context.r[reg]=_mm_cvtsi32_si128(0x1000u+reg);
        context.r[28]=_mm_cvtsi32_si128(0x003ac4f0u);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned step=0;step<256 && context.pc!=returned;++step) {
            require(runtime.hasFunction(context.pc),"compiled call reached an unregistered continuation");
            runtime.lookupFunction(context.pc)(memory,&context,&runtime);
        }
        require(context.pc==returned && getRegU32(&context,29)==stack
                && getRegU32(&context,31)==returned && getRegU32(&context,28)==0x003ac4f0u,
                "compiled stream call did not preserve the outer frame");
        for (unsigned reg=16;reg<=23;++reg)
            require(getRegU32(&context,reg)==0x1000u+reg,"compiled stream call changed a saved register");
        return getRegU32(&context,2);
    };
    auto method=[&](unsigned slot,uint32_t object,uint32_t a1=0u,uint32_t a2=0u,uint32_t a3=0u) {
        return call(translation::stream_callback_slots[slot],{object,a1,a2,a3});
    };
    constexpr uint32_t memory_table=0x003676a8u,memory_object=0x003676e0u;
    for (const auto address:translation::memory_stream_entries)
        require(runtime.hasFunction(address),"SJMEM method is not registered");
    std::memcpy(memory+memory_table,translation::memory_stream_slots.data(),48u);
    auto memory_method=[&](unsigned slot,uint32_t object,uint32_t a1=0u,uint32_t a2=0u,uint32_t a3=0u) {
        return call(translation::memory_stream_slots[slot],{object,a1,a2,a3});
    };
    // Recreate the logged memory-stream object, then run the original
    // movie teardown continuation through its indirect destructor call.
    std::memset(memory+memory_object-4u,0xa5,0x2cu);
    runtime.memory().write32(memory_object,memory_table);
    runtime.memory().write32(memory_object+4u,1u);
    constexpr uint32_t player=0x00370cb8u;
    std::memset(memory+player,0,0x210u);
    runtime.memory().write32(player+0x14cu,memory_object);
    runtime.memory().write64(stack,player);
    runtime.memory().write64(stack+8u,returned);
    R5900Context cleanup{};cleanup.pc=0x00183c38u;
    cleanup.r[16]=_mm_cvtsi32_si128(player);cleanup.r[28]=_mm_cvtsi32_si128(0x003ac4f0u);
    cleanup.r[29]=_mm_cvtsi32_si128(stack);cleanup.r[31]=_mm_cvtsi32_si128(returned);
    for (unsigned step=0;step<256u && cleanup.pc!=returned;++step) {
        require(runtime.hasFunction(cleanup.pc),"movie teardown reached a missing continuation");
        runtime.lookupFunction(cleanup.pc)(memory,&cleanup,&runtime);
    }
    require(cleanup.pc==returned && getRegU32(&cleanup,16)==player
            && getRegU32(&cleanup,29)==stack+16u && getRegU32(&cleanup,28)==0x003ac4f0u
            && runtime.memory().read32(player)==0x00370ae0u
            && runtime.memory().read32(player+0x14cu)==0u,
            "movie teardown did not resume after releasing its memory stream");
    require(std::all_of(memory+memory_object,memory+memory_object+0x24u,[](uint8_t b){return b==0;}),
            "movie teardown did not clear the complete memory-stream object");
    for (unsigned i=0;i<4u;++i)
        require(memory[memory_object-4u+i]==0xa5 && memory[memory_object+0x24u+i]==0xa5,
                "memory-stream destructor crossed its pool slot");
    runtime.memory().write32(player+0x14cu,0u);
    const uint32_t memory_stream=call(0x0016ccd8u,{buffer,64u,0,0});
    require(memory_stream==memory_object && runtime.memory().read32(memory_stream)==memory_table,
            "memory-stream constructor did not return its original pool slot");
    require(memory_method(4,memory_stream)==0x00384878u && memory_method(9,memory_stream,1u)==64u,
            "memory-stream identifier or initial byte count is wrong");
    memory_method(6,memory_stream,1u,20u,chunk);
    require(runtime.memory().read32(chunk)==buffer && runtime.memory().read32(chunk+4u)==20u
            && memory_method(9,memory_stream,1u)==44u,"memory-stream acquisition lost data");
    memory_method(7,memory_stream,1u,chunk);
    require(memory_method(9,memory_stream,1u)==64u,"returning memory-stream data did not restore it");
    memory_method(6,memory_stream,1u,20u,chunk);
    memory_method(8,memory_stream,1u,chunk);
    require(memory_method(10,memory_stream,1u,100u,output)==0u
            && runtime.memory().read32(output)==44u,"memory-stream peek consumed data or returned a wrong count");
    memory_method(6,memory_stream,1u,100u,chunk);
    require(runtime.memory().read32(chunk)==buffer+20u && runtime.memory().read32(chunk+4u)==44u
            && memory_method(9,memory_stream,1u)==0u,"memory-stream exhaustion lost its position");
    memory_method(5,memory_stream);
    require(memory_method(9,memory_stream,1u)==64u,"memory-stream reset did not restore the source");
    std::array<uint8_t,64> memory_zero_page{};std::memcpy(memory_zero_page.data(),memory,64u);
    memory_method(3,0u);
    require(std::memcmp(memory,memory_zero_page.data(),64u)==0,"null memory-stream cleanup changed the zero page");
    memory_method(11,memory_stream,0x00167188u,record);
    require(memory_method(9,memory_stream,7u)==0u && runtime.memory().read32(0x0035fb24u)==record
            && runtime.memory().read32(0x0035fb28u)==0xfffffffdu,"memory-stream error callback lost its arguments");
    memory_method(3,memory_stream);
    // Recreate the logged caller and its object at the original pool address.
    // The caller clears record+8, calls slot 0x0c, then restores its own frame.
    constexpr uint32_t retail_object=0x00367ea0u;
    std::memset(memory+record,0x5a,64);
    runtime.memory().write32(record,0x301u);
    runtime.memory().write32(record+8u,retail_object);
    std::array<uint8_t,64> original_record{};
    std::memcpy(original_record.data(),memory+record,64);
    std::memset(memory+retail_object-16u,0xa5,96);
    runtime.memory().write32(retail_object,table);
    runtime.memory().write32(retail_object+4u,1u);
    runtime.memory().write32(0x00350040u,0u);
    call(0x00156f90u,{record,0,0,0});
    std::fill(original_record.begin()+8,original_record.begin()+12,0);
    require(std::memcmp(memory+record,original_record.data(),64)==0,"cleanup changed unrelated record fields");
    require(std::all_of(memory+retail_object,memory+retail_object+64,[](uint8_t b){return b==0;}),
            "compiled cleanup caller did not release the complete stream object");
    for (unsigned i=0;i<16;++i)
        require(memory[retail_object-16u+i]==0xa5 && memory[retail_object+64u+i]==0xa5,
                "stream destruction changed an adjacent pool object");
    std::array<uint8_t,64> zero_page{};
    std::memcpy(zero_page.data(),memory,64);
    method(3,0u);
    require(std::memcmp(memory,zero_page.data(),64)==0,"null stream destruction changed the zero page");

    const uint32_t object=call(0x0016d220u,{buffer,64u,8u,0u});
    require(object==0x00367ba0u && runtime.memory().read32(object)==table
            && runtime.memory().read32(object+4u)==1u,"original stream constructor returned an invalid object");
    require(method(4,object)==0x003848c8u,"stream interface identifier getter failed");
    auto reset=[&] { method(5,object); };
    auto available=[&](uint32_t queue) { return method(9,object,queue); };
    require(available(0)==64u && available(1)==0u,"new stream has incorrect free/data counts");
    method(6,object,0u,20u,chunk);
    require(runtime.memory().read32(chunk)==buffer && runtime.memory().read32(chunk+4u)==20u
            && available(0)==44u,"free-buffer acquisition failed");
    method(8,object,1u,chunk);
    require(available(1)==20u,"committing a data chunk lost its length");
    std::array<uint8_t,64> before_peek{};
    std::memcpy(before_peek.data(),memory+object,64);
    require(method(10,object,1u,12u,output)==1u && runtime.memory().read32(output)==12u
            && std::memcmp(before_peek.data(),memory+object,64)==0,"peeking consumed stream data");
    method(6,object,1u,12u,chunk);
    require(runtime.memory().read32(chunk)==buffer && runtime.memory().read32(chunk+4u)==12u
            && available(1)==8u,"data-buffer acquisition failed");
    method(7,object,1u,chunk);
    require(available(1)==20u && runtime.memory().read32(object+0x18u)==0u,
            "returning an unconsumed data chunk did not restore its read position");
    method(6,object,1u,12u,chunk);
    method(8,object,0u,chunk);
    require(available(0)==56u && available(1)==8u,"recycling a consumed chunk lost buffer capacity");
    reset();
    method(6,object,0u,20u,chunk);
    method(7,object,0u,chunk);
    require(available(0)==64u && available(1)==0u && runtime.memory().read32(object+0x14u)==0u,
            "returning an unfilled chunk did not restore free capacity");

    // Exercise contiguous spill bytes across the physical end of the ring.
    reset();
    runtime.memory().write32(object+0x14u,60u);
    runtime.memory().write32(object+0x18u,60u);
    std::memset(memory+buffer-16u,0x66,104);
    method(6,object,0u,8u,chunk);
    require(runtime.memory().read32(chunk)==buffer+60u && runtime.memory().read32(chunk+4u)==8u,
            "wrapped acquisition did not include the spill area");
    for (unsigned i=0;i<8;++i) memory[buffer+60u+i]=uint8_t(10u+i);
    method(8,object,1u,chunk);
    require(std::equal(memory+buffer,memory+buffer+4u,memory+buffer+64u),"wrapped stream data was not mirrored");
    method(6,object,1u,8u,chunk);
    method(8,object,0u,chunk);
    require(available(0)==64u && available(1)==0u,"wrapped read/write cycle lost capacity");
    for (unsigned i=0;i<16;++i)
        require(memory[buffer-16u+i]==0x66 && memory[buffer+72u+i]==0x66,"ring copy exceeded its spill allocation");
    method(11,object,0x00167188u,record);
    require(runtime.memory().read32(object+0x38u)==0x00167188u
            && runtime.memory().read32(object+0x3cu)==record,"stream error callback setter failed");
    require(available(7u)==0u && runtime.memory().read32(0x0035fb24u)==record
            && runtime.memory().read32(0x0035fb28u)==0xfffffffdu,"stream error callback arguments were lost");
    method(3,object);
    require(runtime.memory().read32(object+4u)==0u,"constructed stream was not released");
}
}
