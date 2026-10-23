// SPDX-License-Identifier: GPL-3.0-only
#include "startup_diagnostics.h"
#include "host_input.h"
#include "raster_workers.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_address.h"
#include "Kernel/Stubs/Pad.h"
#include "Kernel/Stubs/MemoryCard.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <mutex>

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
        uint32_t backend=0;
        if (movie.backend_valid && memory.range(movie.backend,0x1cd0u,backend)) {
            // The preparation loop at 0x199210 tests parameters 5/6 and
            // driver 6/7 status words. Record those exact inputs and the
            // bounded native driver table without calling guest code.
            text << "[startup:movie-pipeline] requested_state=" << memory.word(backend+0x4cu)
                 << " pending=" << memory.word(backend+0x44u);
            for (uint32_t index:{5u,6u,7u})
                text << " parameter_" << index << '=' << memory.word(backend+0x9a0u+index*4u);
            text << '\n';
            bool seen[20]{};
            for (uint32_t offset:{0x1ab4u,0x1af4u,0x1af8u,0x1afcu,0x1b38u,0x1b3cu,0x1c48u,0x1c8cu}) {
                const auto index=memory.word(backend+offset);
                if (index>=20u || seen[index]) continue;
                seen[index]=true;
                const uint32_t stream=backend+0x1140u+index*0x74u;
                const auto sj=memory.word(stream+0x14u);
                text << "[startup:movie-stream] slot=" << index
                     << " enabled=" << memory.word(stream+4u)
                     << " state=" << memory.word(stream+8u) << ',' << memory.word(stream+12u)
                     << " sj=0x" << std::hex << sj;
                uint32_t physical=0;
                if (sj && memory.range(sj,0x40u,physical)) {
                    for (uint32_t word:{0u,4u,8u,12u,0x10u,0x14u,0x18u,0x1cu,0x20u,0x24u})
                        text << " sj_" << word << "=0x" << memory.word(physical+word);
                } else text << " range=invalid";
                text << std::dec << '\n';
            }
            uint32_t physical=0;
            if (memory.range(movie.backend,0x2ba0u,physical))
                text << "[startup:movie-service] iterations=" << memory.word(physical+0x2b78u) << '\n';
            for (uint32_t index=0;index<9u;++index) {
                const uint32_t driver=backend+0x1aa0u+index*0x44u;
                const auto table=memory.word(driver+0xcu);
                if (!table && index!=6u && index!=7u) continue;
                text << "[startup:movie-driver] slot=" << index
                     << " status=" << memory.word(driver) << ',' << memory.word(driver+4u)
                     << " work=0x" << std::hex << memory.word(driver+8u) << " table=0x" << table;
                uint32_t physical=0;
                if (table && memory.range(table,12u,physical))
                    text << " prepare=0x" << memory.word(physical+8u);
                else text << " prepare=invalid";
                text << std::dec << '\n';
            }
            const auto video=memory.word(backend+0x1b30u);
            physical=0;
            if (video && memory.range(video,0xab0u,physical)) {
                text << "[startup:movie-video] work=0x" << std::hex << video;
                for (uint32_t offset:{0xcu,0x10u,0x7cu,0x138u,0x9d8u,0xa48u,0xa50u,
                                      0xa54u,0xa58u,0xa5cu,0xa60u,0xa88u,0xa8cu,0xaa8u,0xaacu})
                    text << " word_" << offset << "=0x" << memory.word(physical+offset);
                text << std::dec << '\n';
            }
        }
    }
    uint32_t physical=0;
    if (memory.range(0x00370cb4u,4u,physical))
        text << "[startup:movie-sdk] last_error=" << int32_t(memory.word(physical)) << '\n';
    if (movie.active && memory.range(0x00370c4cu,0x2cu,physical))
        text << "[startup:movie-guards] initialized=" << memory.word(physical)
             << " global_busy=" << memory.word(physical+0x28u)
             << " player_busy=" << memory.word(movie.physical+0x60u)
             << " callback_busy=" << memory.word(movie.physical+0x64u) << '\n';
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
bool SaveWaitDiagnostics::sample(uint64_t elapsed_ms,const uint8_t* ram,size_t size,std::ostream& out) {
    if (!ram || size<0x003a4aa4u || reports_>=20u) return false;
    const MovieMemory memory(ram,size);
    uint32_t scene=0,scenePhysical=0,callback=0;
    if (size>=0x003a544cu) {
        scene=memory.word(0x003a5448u);
        if (scene && memory.range(scene,32u,scenePhysical)) callback=memory.word(scenePhysical+28u);
    }
    std::array<uint32_t,6> current={memory.word(0x003a4a40u),memory.word(0x003a4a48u),
        memory.word(0x003a4a4cu),memory.word(0x003a4a9cu),memory.word(0x003a4aa0u),callback};
    constexpr uint64_t deadlines[]={120000u,180000u,240000u,300000u,600000u};
    const bool changed=(!initialized_ || current!=previous_) && changes_<12u;
    const bool periodic=periodic_<5u && elapsed_ms>=deadlines[periodic_];
    initialized_=true;previous_=current;
    if (!changed && !periodic) return false;
    if(changed)++changes_;if(periodic)++periodic_;++reports_;
    const auto pad=ps2_stubs::getPadDebugSnapshot();
    const auto host=host_input_stats(0);
    std::ostringstream text;
    text << "[startup:save-wait] elapsed_ms=" << elapsed_ms
         << " port=" << memory.word(0x003a4a38u) << " slot=" << memory.word(0x003a4a3cu)
         << " operation=" << current[0] << " state=" << current[1] << " result=" << int32_t(current[2])
         << " pending=" << memory.word(0x003a4a44u) << " samples=" << memory.word(0x003a4a58u)
         << " completion=0x" << std::hex << current[3] << " next=0x" << current[4]
         << " scene=0x" << scene << " scene_callback=0x" << callback
         << " buttons=0x" << pad.ports[0][0].lastButtons
         << " host_pressed=0x" << host.pressed << " delivered=0x" << host.delivered << std::dec
         << " game_pad_reads=" << pad.ports[0][0].readCount << " window_polls=" << host.polls
         << " parallel_draws=" << raster_parallel_batches() << '\n';
    if(size>=0x003b4b54u) {
        const auto frame=memory.word(0x003a4b68u),retry=memory.word(0x003a4b70u);
        text << "[startup:frame-wait] enabled=" << (frame&255u)
             << " pending=" << (memory.word(0x003a4b6cu)&255u) << " retry=" << (retry&255u)
             << " countdown=" << int32_t(memory.word(0x003a4b58u))
             << " period=" << int32_t(memory.word(0x003a4b54u))
             << " queue_counts=" << (memory.word(0x003b4b44u)&65535u) << ',' << (memory.word(0x003b4b50u)&65535u)
             << " queue_active=" << int32_t(memory.word(0x003a4c20u)) << ',' << int32_t(memory.word(0x003a4c24u))
             << " dma_owners=" << std::hex << memory.word(0x003b4b00u) << ',' << memory.word(0x003b4b08u)
             << " fields_since_update=" << std::dec << memory.word(0x003a4b74u) << '\n';
    }
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
    static thread_local SaveWaitDiagnostics saveDiagnostics;
    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-begin).count());
    saveDiagnostics.sample(elapsed,ram,size,out);
    if (diagnostics.sample(elapsed,ram,size,out)) {
        std::ostringstream text;
        text << "[startup:display] frame_wait_ms=" << elapsed << std::hex;
        for (auto reg: {0x12000000u,0x12000020u,0x12000070u,0x12000080u,0x12000090u,0x120000a0u})
            text << " reg_" << reg << "=0x" << runtime.memory().read64(reg);
        out << text.str() << '\n' << std::flush;
    }
}
namespace {
struct GuestProgress {
    uint64_t sequence=0;
    uint32_t pc=0,ra=0,sp=0;
    std::array<uint32_t,32> stack{};
    std::array<uint32_t,16> words{};
    std::array<std::array<uint32_t,10>,3> dma{};
    std::array<uint32_t,12> queued{};
    uint32_t dstat=0,owner=0,status=0;
    std::chrono::steady_clock::time_point captured{};
};
std::mutex progressMutex;
GuestProgress guestProgress;
std::chrono::steady_clock::time_point nextCapture;
}
void reset_guest_progress() {
    std::lock_guard lock(progressMutex);guestProgress={};nextCapture={};
}
void capture_guest_progress(const EeKernelSnapshot& state,const uint8_t* ram,size_t size,PS2Runtime* runtime) {
    if(!ram || size<0x003b4b54u)return;
    // Only the EE executor calls this hook. Publish at most four copies per
    // second; keep all guest RAM reads on that executor.
    const auto now=std::chrono::steady_clock::now();
    if(now<nextCapture)return;
    nextCapture=now+std::chrono::milliseconds(250);
    GuestProgress copy;copy.sequence=state.sequence;copy.captured=now;
    auto read=[&](uint32_t addr) {
        uint32_t value=0;const uint32_t physical=(Ps2IsKseg01Address(addr) || Ps2IsUncachedRamMirrorAddress(addr))
            ? Ps2DirectMappedPhysicalAddress(addr) : addr;
        if(size_t(physical)<=size && sizeof(value)<=size-size_t(physical))std::memcpy(&value,ram+physical,4);
        return value;
    };
    for(const auto& thread:state.threads)if(thread.id==1) {
        copy.pc=thread.pc;copy.ra=thread.ra;copy.sp=thread.sp;
        for(unsigned i=0;i<copy.stack.size();++i)if(uint64_t(copy.sp)+4u*i<=UINT32_MAX)copy.stack[i]=read(copy.sp+4u*i);
    }
    constexpr uint32_t addresses[]={0x003a4a40u,0x003a4a48u,0x003a4a4cu,0x003a4a58u,
        0x003a51acu,0x003a51b4u,0x003a51a4u,0x003a5164u,
        0x003a4b6cu,0x003a4b70u,0x003b4b44u,0x003b4b50u,0x003a4c20u,0x003a4c24u};
    for(unsigned i=0;i<14u;++i)copy.words[i]=read(addresses[i]);
    const uint32_t object=copy.words[4]&0x1fffffffu;
    if(object && size_t(object)<=size && 0x34u<=size-size_t(object)) {
        copy.words[14]=read(object+28u);copy.words[15]=read(object+48u);
    }
    const uint32_t queue=read(0x003a4ac0u),head=read(0x003b4b44u)>>16u;
    for(unsigned i=0;i<copy.queued.size();++i)if(uint64_t(queue)+uint64_t(head)*24u+4u*i<=UINT32_MAX)
        copy.queued[i]=read(queue+head*24u+4u*i);
    copy.owner=read(0x003b4b00u);
    if(runtime) {
        // These modeled graphics channel registers do not acknowledge IRQs
        // or advance transfers on reads. Stay on the EE executor.
        constexpr uint32_t channels[]={0x10008000u,0x10009000u,0x1000a000u};
        for(unsigned i=0;i<3u;++i) {
            auto& words=copy.dma[i];const auto base=channels[i];
            for(unsigned j=0;j<6u;++j)words[j]=runtime->memory().readIORegister(base+j*16u);
            for(unsigned j=0;j<4u;++j)if(uint64_t(words[3])+4u*j<=UINT32_MAX)words[6+j]=read(words[3]+4u*j);
        }
        copy.dstat=runtime->memory().readIORegister(0x1000e010u);
        if(const auto* main=runtime->eeScheduler().thread(1))copy.status=main->context.cop0_status;
    }
    { std::lock_guard lock(progressMutex);guestProgress=copy; }
}
bool GuestStallDiagnostics::sample(uint64_t elapsed_ms,uint64_t reads,uint64_t polls,std::ostream& out,bool final) {
    if(reads!=previous_reads_) { previous_reads_=reads;changed_ms_=elapsed_ms;next_idle_ms_=5000; }
    const auto idle=elapsed_ms-changed_ms_;
    if(!final && (reads==0 || reports_>=8u || idle<next_idle_ms_))return false;
    ++reports_;next_idle_ms_=idle+15000u;
    GuestProgress copy;{ std::lock_guard lock(progressMutex);copy=guestProgress; }
    const auto age=copy.sequence ? std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-copy.captured).count() : -1;
    std::ostringstream text;
    text << "[startup:stall] reason=" << (final ? "shutdown" : "no-game-input") << " elapsed_ms=" << elapsed_ms << " idle_ms=" << idle
         << " game_pad_reads=" << reads << " window_polls=" << polls << " snapshot_age_ms=" << age
         << " sequence=" << copy.sequence << " parallel_draws=" << raster_parallel_batches() << " pc=0x" << std::hex << copy.pc
         << " ra=0x" << copy.ra << " sp=0x" << copy.sp << " stack=";
    for(const auto word:copy.stack)text << word << ',';
    text << "\n[startup:save-menu] object=0x" << copy.words[4] << " callback=0x" << copy.words[14]
         << " next=0x" << copy.words[5] << " fade_bits=0x" << copy.words[6]
         << " object_state=" << std::dec << copy.words[15] << " operation=" << copy.words[0]
         << " state=" << copy.words[1] << " result=" << int32_t(copy.words[2])
         << " samples=" << copy.words[3] << " selection=" << copy.words[7]
         << " frame_pending=" << (copy.words[8]&255u) << " frame_retry=" << (copy.words[9]&255u)
         << " queue_counts=" << (copy.words[10]&65535u) << ',' << (copy.words[11]&65535u)
         << " queue_active=" << int32_t(copy.words[12]) << ',' << int32_t(copy.words[13]) << '\n';
    text << "[startup:dma-queue] status=0x" << std::hex << copy.status << " dstat=0x" << copy.dstat
         << " owners=0x" << copy.owner << " items=";
    for(const auto word:copy.queued)text << word << ',';
    text << '\n';
    for(unsigned i=0;i<3u;++i) {
        const auto& words=copy.dma[i];
        text << "[startup:dma-channel] channel=" << std::dec << i << std::hex
             << " chcr=0x" << words[0] << " madr=0x" << words[1] << " qwc=0x" << words[2]
             << " tadr=0x" << words[3] << " asr=0x" << words[4] << ',' << words[5] << " tag=";
        for(unsigned j=6;j<10u;++j)text << words[j] << ',';
        text << '\n';
    }
    out << text.str() << std::flush;return true;
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
    const auto card=ps2_stubs::getMemoryCardDebugSnapshot();
    const auto input=ps2_stubs::getPadDebugSnapshot();
    text << "[startup:save] command=" << card.lastCmd << " result=" << card.lastResult
         << " open_files=" << card.openFiles.size() << " root=" << card.ports[0].rootPath.string() << '\n';
    for (unsigned i=0;i<2u;++i) {
        const auto& pad=input.ports[i][0];
        const auto host=host_input_stats(int(i));
        text << "[startup:pad] port=" << i << " open=" << pad.open << " reads=" << pad.readCount
             << " ok=" << pad.lastReadOk << " backend=" << pad.lastUsedBackend
             << " buttons=0x" << std::hex << pad.lastButtons
             << " host_pressed=0x" << host.pressed << " delivered=0x" << host.delivered
             << std::dec << " window_polls=" << host.polls << '\n';
    }
    out << text.str() << std::flush;
}
}
