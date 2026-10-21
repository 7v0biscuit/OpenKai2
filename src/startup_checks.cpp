// SPDX-License-Identifier: GPL-3.0-only
#include "startup_checks.h"
#include "startup_diagnostics.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
void verify_startup_diagnostics() {
    PS2Runtime runtime;
    auto& memory=runtime.memory();
    require(memory.initialize(),"Cannot allocate startup diagnostic memory");
    auto* ram=memory.getRDRAM();
    require(runtime.syncCoreSubsystems(),"Cannot initialize diagnostic graphics");
    std::ostringstream graphics;
    graphics << std::hex;
    report_startup_graphics(runtime.gs(),5000u,graphics);
    const auto graphicsText=graphics.str();
    require(graphicsText.find("elapsed_ms=5000 frame=0 size=0x0")!=std::string::npos
            && std::count(graphicsText.begin(),graphicsText.end(),'\n')==3,
            "Empty graphics snapshot was misreported");
    require(graphics.flags()&std::ios::hex,"Graphics snapshot changed the caller's stream format");
    constexpr uint32_t movie=0x00370cb8u,backend=0x00250000u;
    memory.write32(movie,0x00370ae0u); memory.write32(movie+4u,1u);
    memory.write32(movie+8u,2u); memory.write32(movie+0x40u,0xa0000000u+backend);
    ram[backend+1u]=3u; memory.write32(backend+0x48u,1u);
    memory.write32(backend+0x9b8u,1u); memory.write32(backend+0x10u,0x1234u);
    const std::vector<uint8_t> movieBefore(ram,ram+PS2_RAM_SIZE);
    std::ostringstream movieReport;
    report_movie_wait(ram,PS2_RAM_SIZE,movieReport);
    require(movieReport.str().find("interface=sofdec valid=1 state=2 backend=0xa0250000 backend_status=3")!=std::string::npos
            && movieReport.str().find("word_10=0x1234")!=std::string::npos
            && std::equal(movieBefore.begin(),movieBefore.end(),ram),
            "Movie wait snapshot lost the aliased backend or changed guest memory");
    memory.write32(movie+0x40u,0x01fffffcu);
    std::ostringstream invalid;
    report_movie_wait(ram,PS2_RAM_SIZE,invalid);
    report_movie_wait(nullptr,0,invalid);
    require(invalid.str().find("backend_range=invalid")!=std::string::npos
            && invalid.str().find("interface=inactive")!=std::string::npos,
            "Movie wait snapshot read beyond the supplied guest range");
    auto& scheduler=runtime.eeScheduler();
    R5900Context context{};
    context.pc=0x001863e4u;
    context.r[31]=_mm_cvtsi32_si128(0x00186234u);
    context.r[29]=_mm_cvtsi32_si128(0x01fffe40u);
    scheduler.bindMainContextForSyscall(context,ram);
    const int semaphore=scheduler.createSemaphore(0,1,0,0);
    require(semaphore>0,"Cannot create diagnostic semaphore");
    bool blocked=false;
    try { scheduler.waitSemaphore(semaphore); }
    catch (const EeDispatcherTransfer&) { blocked=true; }
    require(blocked,"Diagnostic semaphore did not block");
    const auto state=scheduler.snapshot();
    const std::vector<uint8_t> before(ram,ram+PS2_RAM_SIZE);
    StartupDiagnostics diagnostics;
    std::ostringstream output;
    output << std::hex;
    require(!diagnostics.due(0) && !diagnostics.sample(4999,state,{},output),"Startup report logged too early");
    for (const uint64_t now:{5000u,15000u,30000u,60000u})
        require(diagnostics.sample(now,state,{2,0,3,4},output),"Scheduled startup report was lost");
    require(!diagnostics.sample(120000,state,{},output),"Startup report exceeded its sample limit");
    diagnostics.report("window-close",60001,state,{},output);
    const auto text=output.str();
    require(text.find("state=waiting wait=semaphore object="+std::to_string(semaphore))!=std::string::npos
            && text.find("pc=0x1863e4 ra=0x186234 sp=0x1fffe40")!=std::string::npos,
            "Startup report lost the blocked thread, wait object, or caller");
    require(text.find("count=0 maximum=1 waiters=1")!=std::string::npos
            && text.find("dma=2 gif=0 gs=3 vif=4")!=std::string::npos,
            "Startup report lost semaphore state or graphics activity");
    require(text.find("reason=window-close elapsed_ms=60001")!=std::string::npos,
            "Startup report lost the shutdown reason or decimal elapsed time");
    unsigned samples=0;
    for (size_t at=0;(at=text.find("reason=periodic",at))!=std::string::npos;at+=15) ++samples;
    require(samples==4,"Startup reports were duplicated");
    require(output.flags()&std::ios::hex,"Startup report changed the caller's stream format");
    require(std::equal(before.begin(),before.end(),ram),"Startup report changed guest memory");
    const auto after=scheduler.snapshot();
    require(after.sequence==state.sequence && scheduler.thread(1)->status==EeThreadStatus::Waiting
            && scheduler.semaphore(semaphore)->count==0,
            "Startup report changed the scheduler or woke a blocked thread");
    require(scheduler.signalSemaphore(semaphore,false)==semaphore
            && scheduler.thread(1)->status==EeThreadStatus::Ready,
            "Inspected semaphore no longer resumes its waiting thread");
    EeKernelSnapshot large;
    large.threads.resize(40);large.semaphores.resize(30);large.eventFlags.resize(20);
    std::ostringstream bounded;
    diagnostics.report("bounded",1,large,{},bounded);
    const auto limited=bounded.str();
    require(limited.find("[startup:truncated] threads=24 semaphores=14 events=4")!=std::string::npos
            && std::count(limited.begin(),limited.end(),'\n')==50,
            "Large startup snapshot did not respect record limits");
}
}
