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
    constexpr uint32_t movie=0x00260000u,backend=0x00250000u,record=0x003b4f80u;
    memory.write32(0x00370cb8u,0x00370ae0u);
    std::ostringstream unlinked;
    report_movie_wait(ram,PS2_RAM_SIZE,unlinked);
    require(unlinked.str().find("player=0x0 interface=inactive")!=std::string::npos,
            "Movie snapshot inspected a fixed test/pool address instead of the game handle");
    MovieWaitDiagnostics movieSamples;
    std::ostringstream sampled;
    require(movieSamples.sample(0u,ram,PS2_RAM_SIZE,sampled)
            && !movieSamples.sample(15000u,ram,PS2_RAM_SIZE,sampled)
            && !movieSamples.sample(30000u,ram,PS2_RAM_SIZE,sampled),
            "Logo frames consumed the movie's active sampling deadlines");
    memory.write32(0x00370cb4u,uint32_t(-13));
    require(movieSamples.sample(32000u,ram,PS2_RAM_SIZE,sampled),
            "Movie creation failure was missed while no live handle existed");
    memory.write32(record,0x80000000u+movie);
    memory.write32(movie,0xa0370ae0u); memory.write32(movie+4u,1u);
    memory.write32(movie+8u,2u); memory.write32(movie+0x40u,0x20000000u+backend);
    ram[backend+1u]=0x7eu;
    memory.write32(backend+0x48u,4u);
    memory.write32(backend+0x9b8u,1u); memory.write32(backend+0x10u,0x1234u);
    memory.write32(0x00370cb4u,uint32_t(-309));
    memory.write32(0x003a5448u,0x00270000u);
    memory.write32(0x00270008u,0u); memory.write32(0x0027001cu,0x002a4d40u);
    const char filename[]="INTRO.SFD";
    std::copy(std::begin(filename),std::end(filename),ram+record+0x84u);
    memory.write32(backend+0x4cu,4u);memory.write32(backend+0x9b4u,1u);
    memory.write32(backend+0x1c38u,1u);memory.write32(backend+0x1c3cu,0u);
    memory.write32(backend+0x1c40u,0xa0280000u);memory.write32(backend+0x1c44u,0x80290000u);
    memory.write32(0x00290008u,0x00197230u);
    memory.write32(backend+0x1c88u,0x01fffffcu);
    memory.write32(backend+0x1b30u,0xa0280000u);
    memory.write32(0x00280138u,3u);memory.write32(0x00280a58u,2048u);
    memory.write32(backend+0x1af8u,20u);memory.write32(backend+0x1afcu,1u);
    memory.write32(backend+0x1144u,1u);memory.write32(backend+0x1154u,0xa02a0000u);
    memory.write32(backend+0x11c8u,0x01fffffcu);
    memory.write32(0x002a0000u,0x00367b68u);memory.write32(0x002a000cu,1234u);
    memory.write32(backend+0x2b78u,37u);
    memory.write32(0x00370c4cu,1u);memory.write32(0x00370c74u,1u);
    memory.write32(movie+0x60u,1u);
    const std::vector<uint8_t> movieBefore(ram,ram+PS2_RAM_SIZE);
    std::ostringstream movieReport;
    report_movie_wait(ram,PS2_RAM_SIZE,movieReport);
    require(movieReport.str().find("player=0x80260000 interface=sofdec valid=1 state=2")!=std::string::npos
            && movieReport.str().find("backend=0x20250000 backend_state=4")!=std::string::npos
            && movieReport.str().find("word_10=0x1234")!=std::string::npos
            && movieReport.str().find("INTRO.SFD")!=std::string::npos
            && movieReport.str().find("last_error=-309")!=std::string::npos
            && movieReport.str().find("callback=0x2a4d40")!=std::string::npos
            && movieReport.str().find("requested_state=4 pending=0 parameter_5=1 parameter_6=1 parameter_7=0")!=std::string::npos
            && movieReport.str().find("slot=6 status=1,0 work=0xa0280000 table=0x80290000 prepare=0x197230")!=std::string::npos
            && movieReport.str().find("slot=7 status=0,0 work=0x0 table=0x1fffffc prepare=invalid")!=std::string::npos
            && movieReport.str().find("movie-video] work=0xa0280000")!=std::string::npos
            && movieReport.str().find("word_a58=0x800")!=std::string::npos
            && movieReport.str().find("slot=0 enabled=1 state=0,0 sj=0xa02a0000 sj_0=0x367b68")!=std::string::npos
            && movieReport.str().find("sj_c=0x4d2")!=std::string::npos
            && movieReport.str().find("slot=1 enabled=0 state=0,0 sj=0x1fffffc range=invalid")!=std::string::npos
            && movieReport.str().find("movie-stream] slot=20")==std::string::npos
            && movieReport.str().find("movie-service] iterations=37")!=std::string::npos
            && movieReport.str().find("initialized=1 global_busy=1 player_busy=1 callback_busy=0")!=std::string::npos
            && std::equal(movieBefore.begin(),movieBefore.end(),ram),
            "Movie snapshot lost the live handle, true status, scene, filename, or changed guest memory");
    require(movieSamples.sample(35000u,ram,PS2_RAM_SIZE,sampled)
            && !movieSamples.sample(35001u,ram,PS2_RAM_SIZE,sampled)
            && !movieSamples.sample(39999u,ram,PS2_RAM_SIZE,sampled)
            && movieSamples.sample(40000u,ram,PS2_RAM_SIZE,sampled),
            "Late movie activation was missed or periodic sampling used logo time");
    memory.write32(backend+0x48u,6u);
    require(movieSamples.sample(41000u,ram,PS2_RAM_SIZE,sampled)
            && !movieSamples.sample(41001u,ram,PS2_RAM_SIZE,sampled),
            "Movie backend status changes were missed or duplicated");
    require(movieSamples.sample(50000u,ram,PS2_RAM_SIZE,sampled)
            && movieSamples.sample(95000u,ram,PS2_RAM_SIZE,sampled),
            "Movie periodic snapshots after activation were lost");
    for (unsigned i=0;i<50u;++i) {
        memory.write32(backend+0x48u,i);
        movieSamples.sample(96000u+i,ram,PS2_RAM_SIZE,sampled);
    }
    const auto movieText=sampled.str();
    unsigned movieReports=0;
    for (size_t at=0;(at=movieText.find("[startup:movie-sample]",at))!=std::string::npos;at+=22) ++movieReports;
    require(movieReports==11u && !movieSamples.sample(200000u,ram,PS2_RAM_SIZE,sampled),
            "Movie state churn exceeded the bounded report budget");
    memory.write32(movie+0x40u,0x01fffffcu);
    std::ostringstream invalid;
    report_movie_wait(ram,PS2_RAM_SIZE,invalid);
    memory.write32(record,0x01fffffcu);
    report_movie_wait(ram,PS2_RAM_SIZE,invalid);
    report_movie_wait(nullptr,0,invalid);
    require(invalid.str().find("backend_range=invalid")!=std::string::npos
            && invalid.str().find("interface=invalid")!=std::string::npos
            && invalid.str().find("interface=inactive")!=std::string::npos,
            "Movie wait snapshot read beyond the supplied guest range");
    memory.write32(record,movie);memory.write32(movie+0x40u,0x01fff644u);
    std::ostringstream truncated;
    report_movie_wait(ram,PS2_RAM_SIZE,truncated);
    require(truncated.str().find("backend_range=invalid")==std::string::npos
            && truncated.str().find("movie-pipeline")==std::string::npos,
            "Movie preparation snapshot read past a truncated backend");
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
    SaveWaitDiagnostics saveSamples;std::ostringstream saveReport;
    memory.write32(0x003a4a40u,1u);memory.write32(0x003a4a48u,2u);
    memory.write32(0x003b4b44u,3u);memory.write32(0x003b4b50u,4u);
    memory.write32(0x003a4c20u,1u);memory.write32(0x003a4c24u,0xffffffffu);
    const std::vector<uint8_t> saveBefore(ram,ram+PS2_RAM_SIZE);
    require(!saveSamples.sample(0,nullptr,0,saveReport)
            && saveSamples.sample(0,ram,PS2_RAM_SIZE,saveReport)
            && !saveSamples.sample(1,ram,PS2_RAM_SIZE,saveReport)
            && saveSamples.sample(180000u,ram,PS2_RAM_SIZE,saveReport),"Save wait activation or late sampling was lost");
    require(saveReport.str().find("operation=1 state=2")!=std::string::npos
            && saveReport.str().find("window_polls=")!=std::string::npos
            && saveReport.str().find("delivered=0x")!=std::string::npos
            && saveReport.str().find("queue_counts=3,4 queue_active=1,-1")!=std::string::npos
            && std::equal(saveBefore.begin(),saveBefore.end(),ram),"Save wait report changed memory or lost input delivery");
    for(unsigned i=0;i<64u;++i) {
        memory.write32(0x003a4a48u,i);saveSamples.sample(200000u+i,ram,PS2_RAM_SIZE,saveReport);
    }
    const auto saveText=saveReport.str();
    require(std::count(saveText.begin(),saveText.end(),'\n')<=40u
            && !saveSamples.sample(200100u,ram,PS2_RAM_SIZE,saveReport),"Save wait churn exceeded the report budget");
    reset_guest_progress();
    EeKernelSnapshot copied;copied.sequence=77u;copied.threads.resize(1);
    copied.threads[0].id=1;copied.threads[0].pc=0x0026d360u;
    copied.threads[0].ra=0x001483f0u;copied.threads[0].sp=0x01fff000u;
    memory.write32(0x01fff000u,0x00150358u);
    memory.write32(0x003a51acu,0x26000u);memory.write32(0x2601cu,0x0026c870u);
    memory.write32(0x003a4a48u,2u);
    const std::vector<uint8_t> progressBefore(ram,ram+PS2_RAM_SIZE);
    memory.write32(0x1000a030u,0x26000u);
    const auto oldDstat=memory.readIORegister(0x1000e010u),oldChcr=memory.readIORegister(0x1000a000u);
    capture_guest_progress(copied,ram,PS2_RAM_SIZE,&runtime);
    require(std::equal(progressBefore.begin(),progressBefore.end(),ram)
            && memory.readIORegister(0x1000e010u)==oldDstat && memory.readIORegister(0x1000a000u)==oldChcr,
            "Progress capture changed live guest memory, DMA status, or the channel busy bit");
    memory.write32(0x2601cu,0u);
    GuestStallDiagnostics stalled;std::ostringstream stallReport;
    require(!stalled.sample(1000,5,60,stallReport) && !stalled.sample(5999,5,360,stallReport)
            && stalled.sample(6000,5,361,stallReport) && !stalled.sample(6001,5,362,stallReport),
            "Stall detection missed its idle deadline or duplicated reports");
    require(stallReport.str().find("pc=0x26d360")!=std::string::npos
            && stallReport.str().find("stack=150358,")!=std::string::npos
            && stallReport.str().find("callback=0x26c870")!=std::string::npos,
            "Stall report lost the captured EE continuation or read live RAM on the window thread");
    require(!stalled.sample(7000,6,420,stallReport) && !stalled.sample(11999,6,720,stallReport)
            && stalled.sample(12000,6,721,stallReport),"Stall recovery did not reset its deadline");
    for(unsigned i=0;i<16;++i)stalled.sample(30000u+i*15000u,6,1000u+i,stallReport);
    const auto stallText=stallReport.str();
    require(std::count(stallText.begin(),stallText.end(),'\n')==48u
            && !stalled.sample(1000000,6,60000,stallReport),"Stall reports exceeded their bounded budget");
    require(stalled.sample(1000001,6,60001,stallReport,true)
            && stallReport.str().find("reason=shutdown")!=std::string::npos,"Shutdown lost its pre-join guest snapshot");
    reset_guest_progress();
    EeKernelSnapshot large;
    large.threads.resize(40);large.semaphores.resize(30);large.eventFlags.resize(20);
    std::ostringstream bounded;
    diagnostics.report("bounded",1,large,{},bounded);
    const auto limited=bounded.str();
    require(limited.find("[startup:truncated] threads=24 semaphores=14 events=4")!=std::string::npos
            && std::count(limited.begin(),limited.end(),'\n')==53,
            "Large startup snapshot did not respect record limits");
}
}
