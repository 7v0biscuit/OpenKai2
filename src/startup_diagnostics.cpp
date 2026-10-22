// SPDX-License-Identifier: GPL-3.0-only
#include "startup_diagnostics.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_address.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iomanip>

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
namespace {
struct MovieState {
    uint32_t player=0,physical=0,backend=0,backend_physical=0,state=0,backend_state=0;
    uint32_t sdk_error=0,scene_callback=0;
    bool active=false,backend_valid=false;
};
class MovieMemory {
    const uint8_t* ram_;
    size_t size_;
public:
    static constexpr uint32_t record=0x003b4f80u,table=0x00370ae0u;
    MovieMemory(const uint8_t* ram,size_t size):ram_(ram),size_(std::min<size_t>(size,PS2_RAM_SIZE)) {}
    bool range(uint32_t address,size_t length,uint32_t& physical) const {
        if (!ram_ || (address&3u)!=0u) return false;
        physical=(Ps2IsKseg01Address(address) || Ps2IsUncachedRamMirrorAddress(address))
            ? Ps2DirectMappedPhysicalAddress(address) : address;
        return size_t(physical)<=size_ && length<=size_-size_t(physical);
    }
    uint32_t word(uint32_t address) const {
        uint32_t value; std::memcpy(&value,ram_+address,4); return value;
    }
    MovieState snapshot() const {
        MovieState result;
        uint32_t physical=0,table_physical=0;
        if (range(0x00370cb4u,4u,physical)) result.sdk_error=word(physical);
        if (range(0x003a5448u,4u,physical)) {
            const uint32_t scene=word(physical);
            if (scene!=0u && range(scene,0x20u,physical)) result.scene_callback=word(physical+0x1cu);
        }
        if (!range(record,0x108u,physical)) return result;
        // The game stores its one active player in this record at 0x146008;
        // the logged caller loads it at 0x2A4EEC. Do not assume a pool slot.
        result.player=word(physical);
        if (result.player==0u || !range(result.player,0x128u,result.physical)
                || !range(word(result.physical),4u,table_physical) || table_physical!=table) return result;
        result.active=true;
        result.state=word(result.physical+8u);
        result.backend=word(result.physical+0x40u);
        result.backend_valid=result.backend!=0u && range(result.backend,0x9bcu,result.backend_physical);
        // mwPlyGetStat reads a word at +0x48 (0x19BD0C). Byte +1 belongs
        // to a different ADX interface and is not the movie backend's status.
        if (result.backend_valid) result.backend_state=word(result.backend_physical+0x48u);
        return result;
    }
    std::string filename() const {
        uint32_t physical=0;
        if (!range(record,0x108u,physical)) return {};
        std::string name;
        for (unsigned i=0;i<0x84u;++i) {
            const uint8_t c=ram_[physical+0x84u+i];
            if (!c) break;
            name.push_back(c>=32u && c<127u ? char(c) : '?');
        }
        return name;
    }
};
}
void report_movie_wait(const uint8_t* ram,size_t size,std::ostream& out) {
    const MovieMemory memory(ram,size);
    const auto movie=memory.snapshot();
    std::ostringstream text;
    text << "[startup:movie-wait] player=0x" << std::hex << movie.player << std::dec;
    if (!movie.active) {
        text << " interface=" << (movie.player ? "invalid" : "inactive") << '\n';
    } else {
        text << " interface=sofdec valid=" << memory.word(movie.physical+4u) << " state=" << movie.state
             << " file=" << std::quoted(memory.filename())
             << " backend=0x" << std::hex << movie.backend << std::dec;
        if (movie.backend_valid) {
            text << " backend_state=" << movie.backend_state
                 << " audio=" << memory.word(movie.backend_physical+0x9b8u);
            for (uint32_t offset:{0x10u,0x20u,0x24u,0x30u})
                text << " word_" << std::hex << offset << "=0x" << memory.word(movie.backend_physical+offset) << std::dec;
        } else text << " backend_range=invalid";
        for (uint32_t offset:{0x118u,0x11cu,0x120u,0x124u})
            text << " player_" << std::hex << offset << "=0x" << memory.word(movie.physical+offset) << std::dec;
        text << '\n';
    }
    uint32_t physical=0;
    if (memory.range(0x00370cb4u,4u,physical))
        text << "[startup:movie-sdk] last_error=" << int32_t(memory.word(physical)) << '\n';
    if (memory.range(0x003a5448u,4u,physical)) {
        const auto scene=memory.word(physical);
        if (scene!=0u && memory.range(scene,0x20u,physical))
            text << "[startup:movie-scene] object=0x" << std::hex << scene
                 << " callback=0x" << memory.word(physical+0x1cu)
                 << " handle=0x" << memory.word(physical+8u) << std::dec << '\n';
    }
    out << text.str() << std::flush;
}
bool MovieWaitDiagnostics::sample(uint64_t elapsed_ms,const uint8_t* ram,size_t size,std::ostream& out) {
    if (reports_>=12u) return false;
    const auto movie=MovieMemory(ram,size).snapshot();
    const bool changed=!initialized_ || movie.active!=active_ || movie.player!=player_ || movie.state!=state_
        || movie.backend!=backend_ || movie.backend_state!=backend_state_
        || movie.sdk_error!=sdk_error_ || movie.scene_callback!=scene_callback_;
    if (movie.active && (!active_ || movie.player!=player_)) {
        active_begin_=elapsed_ms;
        periodic_=0;
    }
    active_=movie.active;
    initialized_=true;
    player_=movie.player; state_=movie.state; backend_=movie.backend; backend_state_=movie.backend_state;
    sdk_error_=movie.sdk_error; scene_callback_=movie.scene_callback;
    constexpr uint64_t deadlines[]={5000u,15000u,60000u};
    const bool periodic=active_ && periodic_<3u && elapsed_ms-active_begin_>=deadlines[periodic_];
    if (periodic) ++periodic_;
    const bool transition=changed && changes_<8u;
    if (transition) ++changes_;
    if (!transition && !periodic) return false;
    ++reports_;
    std::ostringstream text;
    text << "[startup:movie-sample] elapsed_ms=" << elapsed_ms
         << " reason=" << (transition ? "state-change" : "periodic") << '\n';
    report_movie_wait(ram,size,text);
    out << text.str() << std::flush;
    return true;
}
void sample_movie_wait(PS2Runtime& runtime,const uint8_t* ram,size_t size,uint32_t return_pc,std::ostream& out) {
    if (return_pc!=0x00186450u) return;
    // This is the common CRI frame yield, including logo frames before any
    // movie exists. Observe activation immediately and time later snapshots
    // from activation, rather than spending the movie samples on the logos.
    static thread_local const auto begin=std::chrono::steady_clock::now();
    static thread_local MovieWaitDiagnostics diagnostics;
    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-begin).count());
    if (diagnostics.sample(elapsed,ram,size,out)) {
        std::ostringstream text;
        text << "[startup:display] frame_wait_ms=" << elapsed << std::hex;
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
