// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <iosfwd>
struct EeKernelSnapshot;
class GS;
class PS2Runtime;
namespace openkai2 {
void report_startup_graphics(GS&,uint64_t elapsed_ms,std::ostream&);
void report_movie_wait(const uint8_t* ram,size_t size,std::ostream&);
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
