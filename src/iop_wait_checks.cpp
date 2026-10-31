// SPDX-License-Identifier: GPL-3.0-only
#include "iop_wait_checks.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
}
void verify_iop_wait_diagnostics() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    IopKernel kernel(memory);
    kernel.reset();
    const uint32_t descriptor[]={0,0,0x10000u,0x400u,60};
    require(memory.writeRam(0x3000u,descriptor,sizeof(descriptor)),"Cannot write wait-check descriptor");
    IopCpuState control{};
    control.gpr[4]=0x3000u;
    require(kernel.dispatchThreadImport(4,control,0) && control.gpr[2]==1,"Cannot create wait-check thread");
    control.gpr[4]=1;
    require(kernel.dispatchThreadImport(6,control,0),"Cannot start wait-check thread");
    auto* thread=kernel.beginNextReady(0);
    require(thread && thread->id==1,"Wait-check thread did not become ready");
    const uint32_t semaphore[]={0,0,0,1};
    require(memory.writeRam(0x3020u,semaphore,sizeof(semaphore)),"Cannot write wait-check semaphore");
    control.gpr[4]=0x3020u;
    require(kernel.dispatchSemaphoreImport(4,control) && control.gpr[2]==1,"Cannot create wait-check semaphore");
    thread->cpu.gpr[4]=1;
    thread->cpu.gpr[31]=0x10008u;
    thread->cpu.pc=0x10100u;
    memory.write32(0x10100u,0x24620001u);
    require(kernel.dispatchSemaphoreImport(8,thread->cpu),"Cannot enter semaphore wait");
    kernel.endTimeslice(*thread,0x1FFFFF00u);
    const std::vector<uint8_t> before(memory.ram().begin(),memory.ram().end());
    std::ostringstream snapshot;
    kernel.appendDebugState(snapshot);
    const auto text=snapshot.str();
    require(text.find("thread=1 state=semaphore priority=60 wait=1")!=std::string::npos,
            "Wait snapshot lost the blocked thread or semaphore ID");
    require(text.find("semaphore=1 count=0 maximum=1")!=std::string::npos,
            "Wait snapshot lost the semaphore count");
    require(text.find("code@0x100f0=")!=std::string::npos && text.find("0x24620001")!=std::string::npos,
            "Wait snapshot lost the guest instruction context");
    require(std::equal(before.begin(),before.end(),memory.ram().begin()),"Wait snapshot modified guest memory");
    require(kernel.beginNextReady(1)==nullptr,"Wait snapshot woke a blocked thread");
    control.gpr[4]=1;
    require(kernel.dispatchSemaphoreImport(6,control),"Cannot signal wait-check semaphore");
    thread=kernel.beginNextReady(2);
    require(thread && thread->id==1,"Semaphore signal did not resume the inspected thread");
    const auto event=kernel.createInternalEventFlag(0,0,0);
    thread->cpu.gpr[4]=uint32_t(event);
    thread->cpu.gpr[5]=4;
    thread->cpu.gpr[6]=1;
    thread->cpu.gpr[7]=0;
    require(kernel.dispatchEventImport(10,thread->cpu),"Cannot enter event wait");
    kernel.endTimeslice(*thread,0x1FFFFF00u);
    std::ostringstream eventSnapshot;
    kernel.appendDebugState(eventSnapshot);
    require(eventSnapshot.str().find("thread=1 state=event")!=std::string::npos
            && eventSnapshot.str().find("event=1 bits=0x0")!=std::string::npos,
            "Wait snapshot lost the event state");
    require(kernel.beginNextReady(3)==nullptr && kernel.setInternalEventFlag(event,4),
            "Event snapshot changed the pending wait");
    require(kernel.beginNextReady(4)==thread,"Event signal did not resume the inspected thread");
}
}
