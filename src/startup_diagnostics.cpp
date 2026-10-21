// SPDX-License-Identifier: GPL-3.0-only
#include "startup_diagnostics.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

namespace openkai2 {
namespace {
const char* status_name(EeThreadStatus state) {
    switch (state) {
    case EeThreadStatus::Running: return "running";
    case EeThreadStatus::Ready: return "ready";
    case EeThreadStatus::Waiting: return "waiting";
    case EeThreadStatus::WaitingSuspended: return "waiting-suspended";
    case EeThreadStatus::Suspended: return "suspended";
    case EeThreadStatus::Dormant: return "dormant";
    }
    return "unknown";
}
const char* wait_name(EeWaitReason reason) {
    switch (reason) {
    case EeWaitReason::None: return "none";
    case EeWaitReason::Sleep: return "sleep";
    case EeWaitReason::Semaphore: return "semaphore";
    case EeWaitReason::EventFlag: return "event";
    case EeWaitReason::VSync: return "vsync";
    case EeWaitReason::External: return "external";
    case EeWaitReason::Mpeg: return "mpeg";
    }
    return "unknown";
}
constexpr uint64_t sample_times[]={5000u,15000u,30000u,60000u};
constexpr size_t limit=16;
}
void report_startup_graphics(GS& gs,uint64_t elapsed_ms,std::ostream& out) {
    const auto state=gs.getDebugSnapshot();
    std::vector<uint8_t> pixels;
    uint32_t width=0,height=0,display=0,source=0;
    bool preferred=false;
    const bool frame=gs.copyLatchedHostPresentationFrame(pixels,width,height,&display,&source,&preferred);
    size_t nonblack=0;
    uint32_t hash=2166136261u;
    for (size_t i=0;i+3<pixels.size();i+=4) {
        if (pixels[i] || pixels[i+1] || pixels[i+2]) ++nonblack;
        for (unsigned channel=0;channel<3;++channel) hash=(hash^pixels[i+channel])*16777619u;
    }
    std::ostringstream text;
    text << "[startup:graphics] elapsed_ms=" << elapsed_ms << " frame=" << frame
         << " size=" << width << 'x' << height << " display_fbp=" << display
         << " source_fbp=" << source << " preferred=" << preferred
         << " nonblack_pixels=" << nonblack << " rgb_hash=0x" << std::hex << hash << std::dec
         << " transfer_pixels=" << state.transferCopiedPixels << '/' << state.transferTotalPixels << '\n';
    for (unsigned i=0;i<2;++i) {
        const auto& context=state.ctx[i];
        text << "[startup:gs-context] id=" << i << " fbp=" << context.frame.fbp
             << " fbw=" << context.frame.fbw << " psm=" << unsigned(context.frame.psm)
             << " mask=0x" << std::hex << context.frame.fbmsk
             << " test=0x" << context.test << " alpha=0x" << context.alpha << std::dec
             << " offset=" << context.xyoffset.ofx << ',' << context.xyoffset.ofy
             << " scissor=" << context.scissor.x0 << ',' << context.scissor.y0 << ':'
             << context.scissor.x1 << ',' << context.scissor.y1 << '\n';
    }
    const auto history=gs.getDebugHistory();
    unsigned reported=0;
    // Prefer recent drawing/transfer events over per-frame presentation noise.
    for (auto at=history.rbegin();at!=history.rend() && reported<8;++at) {
        const auto& event=*at;
        if (event.kind!=GSDebugEventKind::Draw && event.kind!=GSDebugEventKind::Transfer) continue;
        ++reported;
        text << "[startup:gs-event] sequence=" << event.seq << " tick=" << event.vsyncTick
             << " kind=" << (event.kind==GSDebugEventKind::Draw ? "draw" : "transfer")
             << " primitive=" << unsigned(event.prim.type) << " vertices=" << event.vertexCount
             << " xy=" << event.xMin << ',' << event.yMin << ':' << event.xMax << ',' << event.yMax
             << " alpha=" << unsigned(event.aMin) << ':' << unsigned(event.aMax)
             << " fbp=" << event.frame.fbp << " texture=" << event.tex0.tbp0
             << " pixels=" << event.transferPixels << '\n';
    }
    out << text.str() << std::flush;
}
void report_movie_wait(const uint8_t* ram,size_t size,std::ostream& out) {
    auto range=[&](uint32_t address,size_t length) {
        return ram && size_t(address)<=size && length<=size-size_t(address);
    };
    auto word=[&](uint32_t address) { uint32_t value; std::memcpy(&value,ram+address,4); return value; };
    constexpr uint32_t object=0x00370cb8u,table=0x00370ae0u;
    std::ostringstream text;
    text << "[startup:movie-wait]";
    if (!range(object,0x44u) || word(object)!=table) {
        text << " interface=inactive\n";
    } else {
        const uint32_t backend=word(object+0x40u);
        const uint32_t physical=backend&0x1fffffffu;
        text << " interface=sofdec valid=" << word(object+4u) << " state=" << word(object+8u)
             << " backend=0x" << std::hex << backend << std::dec;
        if (backend!=0u && (backend<0x20000000u || (backend>=0x80000000u && backend<0xc0000000u))
                && range(physical,0x9bcu)) {
            text << " backend_status=" << unsigned(ram[physical+1u])
                 << " backend_valid=" << word(physical+0x48u)
                 << " audio=" << word(physical+0x9b8u);
            for (uint32_t offset:{0x10u,0x20u,0x24u,0x30u})
                text << " word_" << std::hex << offset << "=0x" << word(physical+offset) << std::dec;
        } else text << " backend_range=invalid";
        text << '\n';
    }
    out << text.str() << std::flush;
}
void sample_movie_wait(PS2Runtime& runtime,const uint8_t* ram,size_t size,uint32_t return_pc,std::ostream& out) {
    if (return_pc!=0x00186450u) return;
    // Runs on the EE executor, where guest memory is stable. The host window
    // thread only inspects the GS's locked snapshots.
    static thread_local const auto begin=std::chrono::steady_clock::now();
    static thread_local unsigned samples=0;
    constexpr uint64_t deadlines[]={0u,5000u,15000u,60000u};
    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-begin).count());
    if (samples<4u && elapsed>=deadlines[samples]) {
        ++samples;
        report_movie_wait(ram,size,out);
        std::ostringstream text;
        text << "[startup:display] movie_wait_ms=" << elapsed << std::hex;
        for (auto reg: {0x12000000u,0x12000020u,0x12000070u,0x12000080u,0x12000090u,0x120000a0u})
            text << " reg_" << reg << "=0x" << runtime.memory().read64(reg);
        out << text.str() << '\n' << std::flush;
    }
}
bool StartupDiagnostics::due(uint64_t elapsed_ms) const {
    return samples_<4 && elapsed_ms>=sample_times[samples_];
}
bool StartupDiagnostics::sample(uint64_t elapsed_ms,const EeKernelSnapshot& state,StartupActivity activity,std::ostream& out) {
    if (!due(elapsed_ms)) return false;
    ++samples_;
    report("periodic",elapsed_ms,state,activity,out);
    return true;
}
void StartupDiagnostics::report(const char* reason,uint64_t elapsed_ms,const EeKernelSnapshot& state,StartupActivity activity,std::ostream& out) const {
    std::ostringstream text;
    text << "[startup:state] reason=" << reason << " elapsed_ms=" << elapsed_ms
         << " sequence=" << state.sequence << " ee_cycles=" << state.eeCycle
         << " running_thread=" << state.runningThreadId << " threads=" << state.threads.size()
         << " dma=" << activity.dma << " gif=" << activity.gif
         << " gs=" << activity.gs << " vif=" << activity.vif << '\n';
    for (size_t i=0;i<std::min(limit,state.threads.size());++i) {
        const auto& thread=state.threads[i];
        text << "[startup:thread] id=" << thread.id << " state=" << status_name(thread.status)
             << " wait=" << wait_name(thread.waitReason) << " object=" << thread.waitId
             << " priority=" << thread.currentPriority << " suspended=" << thread.suspendCount
             << " wakeups=" << thread.wakeupCount << " invocations=" << thread.invocationDepth
             << " pc=0x" << std::hex << thread.pc << " ra=0x" << thread.ra
             << " sp=0x" << thread.sp << " gp=0x" << thread.contextGp
             << " entry=0x" << thread.entry << std::dec << '\n';
    }
    for (size_t i=0;i<std::min(limit,state.semaphores.size());++i) {
        const auto& semaphore=state.semaphores[i];
        text << "[startup:semaphore] id=" << semaphore.id << " count=" << semaphore.count
             << " maximum=" << semaphore.maxCount << " waiters=" << semaphore.waiters << '\n';
    }
    for (size_t i=0;i<std::min(limit,state.eventFlags.size());++i) {
        const auto& event=state.eventFlags[i];
        text << "[startup:event] id=" << event.id << " bits=0x" << std::hex << event.bits
             << " initial=0x" << event.initBits << " attributes=0x" << event.attr
             << std::dec << " waiters=" << event.waiters << '\n';
    }
    if (state.threads.size()>limit || state.semaphores.size()>limit || state.eventFlags.size()>limit)
        text << "[startup:truncated] threads=" << state.threads.size()-std::min(limit,state.threads.size())
             << " semaphores=" << state.semaphores.size()-std::min(limit,state.semaphores.size())
             << " events=" << state.eventFlags.size()-std::min(limit,state.eventFlags.size()) << '\n';
    out << text.str() << std::flush;
}
}
