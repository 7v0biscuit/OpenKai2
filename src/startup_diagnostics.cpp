// SPDX-License-Identifier: GPL-3.0-only
#include "startup_diagnostics.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
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
