// SPDX-License-Identifier: GPL-3.0-only
#include "animation_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace openkai2 {
void verify_skeletal_transforms(PS2Runtime& runtime,uint8_t* ram){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    constexpr uint32_t entry=0x145880u,input=0x730000u,translation=input+32u,returned=0x1be1c4u;
    require(runtime.hasFunction(entry),"Original quaternion transform is not registered");
    // Use a separate kernel so starting and restarting this worker cannot
    // modify the scheduler used by other translated-code regression fixtures.
    auto owner=std::make_unique<PS2Runtime>();auto& scheduler=owner->eeScheduler();
    require(owner->memory().initialize(),"Cannot allocate skeletal worker fixture");
    R5900Context caller{};caller.r[29]=_mm_cvtsi32_si128(0x1ffffc0u);
    scheduler.bindMainContextForSyscall(caller,owner->memory().getRDRAM());
    EeThreadCreateParams params{};params.entry=entry;params.stack=0x740000u;params.stackSize=0x1000u;params.priority=25;
    const int id=scheduler.createThread(params);require(id>1,"Cannot create skeletal worker fixture");
    constexpr float quaternions[][4]={{0,0,0,1},{0,0,0,0},{1,0,0,1},{0,2,0,2},
        {0,0,3,3},{1,2,3,4},{-1,0.5f,2,-0.25f},{-2,-4,-6,-8}};
    constexpr float offset[]={2,-4,7,1};
    for(unsigned restart=0;restart<2u;++restart){
        require(scheduler.startThread(id,0u,caller,true)==0,"Cannot start skeletal worker fixture");
        const auto initial=scheduler.thread(id)->context;
        for(const auto& q:quaternions)for(unsigned parent=0;parent<2u;++parent)for(unsigned moved=0;moved<2u;++moved){
            std::memset(ram+input-16u,0xa5,80u);std::memcpy(ram+input,q,sizeof(q));std::memcpy(ram+translation,offset,sizeof(offset));
            auto c=initial;c.pc=entry;c.r[4]=_mm_cvtsi32_si128(input);c.r[5]=_mm_cvtsi32_si128(moved ? translation : 0u);
            c.r[31]=_mm_cvtsi32_si128(returned);
            // Columns of a parent with translation, plus a rotated/scaled
            // parent. The oracle multiplies independent double matrices.
            const double p[4][4]={{parent ? 0.0 : 1.0,parent ? 2.0 : 0.0,0,0},
                {parent ? -3.0 : 0.0,parent ? 0.0 : 1.0,0,0},
                {0,0,parent ? 4.0 : 1.0,0},{5,-3,9,1}};
            for(unsigned column=0;column<4u;++column)c.vu0_vf[28u+column]=_mm_set_ps(float(p[column][3]),float(p[column][2]),float(p[column][1]),float(p[column][0]));
            const double x=q[0],y=q[1],z=q[2],w=q[3],norm=x*x+y*y+z*z+w*w;
            const double scale=norm>0 ? 2.0/norm : 0.0;
            const double local[4][4]={{1-scale*(y*y+z*z),scale*(x*y+z*w),scale*(x*z-y*w),0},
                {scale*(x*y-z*w),1-scale*(x*x+z*z),scale*(y*z+x*w),0},
                {scale*(x*z+y*w),scale*(y*z-x*w),1-scale*(x*x+y*y),0},
                {moved ? offset[0] : 0.0,moved ? offset[1] : 0.0,moved ? offset[2] : 0.0,1}};
            runtime.lookupFunction(entry)(ram,&c,&runtime);
            require(c.pc==returned && getRegU32(&c,29)==getRegU32(&initial,29),"Skeletal transform did not return to its worker caller");
            for(unsigned column=0;column<4u;++column){alignas(16) float actual[4];_mm_store_ps(actual,c.vu0_vf[28u+column]);
                for(unsigned row=0;row<4u;++row){double expected=0;for(unsigned k=0;k<4u;++k)expected+=p[k][row]*local[column][k];
                    require(std::isfinite(actual[row]) && std::abs(double(actual[row])-expected)<=0.00002*(1+std::abs(expected)),
                        "Skeletal quaternion transform lost rotation, parent translation, or homogeneous coordinates");}}
            require(std::memcmp(ram+input,q,sizeof(q))==0 && std::memcmp(ram+translation,offset,sizeof(offset))==0,
                "Skeletal transform changed its source quaternion or offset");
            for(unsigned i=0;i<16u;++i)require(ram[input-16u+i]==0xa5u && ram[input+48u+i]==0xa5u,"Skeletal transform changed source guards");
        }
        scheduler.thread(id)->context.vu0_vf[0]=_mm_setzero_ps();uint32_t stack=0;
        require(scheduler.terminateThread(id,stack,true)==0,"Cannot restart skeletal worker fixture");
    }
}
}
