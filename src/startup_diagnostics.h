// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
#include <iosfwd>
struct EeKernelSnapshot;
class GS;
class PS2Runtime;
namespace openkai2 {
void report_startup_graphics(GS&,uint64_t elapsed_ms,std::ostream&);
void report_movie_wait(const uint8_t* ram,size_t size,std::ostream&);
class SaveWaitDiagnostics {
    bool initialized_=false;
    unsigned reports_=0,changes_=0,periodic_=0;
    std::array<uint32_t,6> previous_{};
public:
    bool sample(uint64_t elapsed_ms,const uint8_t* ram,size_t size,std::ostream&);
};
class MovieWaitDiagnostics {
    bool initialized_=false, active_=false;
    unsigned changes_=0, periodic_=0, reports_=0;
    uint64_t active_begin_=0;
    uint32_t player_=0, state_=0, backend_=0, backend_state_=0;
    uint32_t sdk_error_=0, scene_callback_=0;
public:
    bool sample(uint64_t elapsed_ms,const uint8_t* ram,size_t size,std::ostream&);
};
void sample_movie_wait(PS2Runtime&,const uint8_t* ram,size_t size,uint32_t return_pc,std::ostream&);
// Capture RAM on the EE executor, then report the copied state on the window
// thread. The watchdog never reads live guest memory or changes a guest wait.
void reset_guest_progress();
void capture_guest_progress(const EeKernelSnapshot&,const uint8_t*,size_t,PS2Runtime* runtime=nullptr);
class GuestStallDiagnostics {
    uint64_t previous_reads_=0,changed_ms_=0,next_idle_ms_=5000;
    unsigned reports_=0;
public:
    bool sample(uint64_t elapsed_ms,uint64_t reads,uint64_t polls,std::ostream&,bool final=false);
};
struct StartupActivity {
    uint64_t dma=0, gif=0, gs=0, vif=0;
};
class StartupDiagnostics {
    unsigned samples_=0;
public:
    bool due(uint64_t elapsed_ms) const;
    bool sample(uint64_t elapsed_ms,const EeKernelSnapshot&,StartupActivity,std::ostream&);
    void report(const char* reason,uint64_t elapsed_ms,const EeKernelSnapshot&,StartupActivity,std::ostream&) const;
};
}
