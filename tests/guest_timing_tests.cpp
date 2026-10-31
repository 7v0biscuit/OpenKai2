// SPDX-License-Identifier: GPL-3.0-only
#include "guest_timing.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
int main(){
    using namespace openkai2;
    std::ostringstream captured;
    struct Restore {std::streambuf* previous;~Restore(){std::cerr.rdbuf(previous);enable_guest_timings(false);}} restore{std::cerr.rdbuf(captured.rdbuf())};
    auto require=[](bool value){if(!value)throw std::runtime_error("Guest timing scope regression");};
    for(unsigned i=0;i<512u;++i){GuestTimingScope disabled(GuestWork::dispatch,0xdeadu);}
    flush_guest_timings();require(captured.str().empty());
    require(sample_draw_history());
    enable_guest_timings(true);
    bool otherFull=true;
    std::thread other([&]{
        enable_guest_timings(true);
        for(unsigned i=0;i<512u;++i){GuestTimingScope foreign(GuestWork::dispatch,0xdeadu);otherFull&=sample_draw_history();}
        enable_guest_timings(false);
    });other.join();require(otherFull);
    unsigned retained=0;for(unsigned i=0;i<128u;++i)retained+=sample_draw_history();
    require(retained==2u);
    for(unsigned i=0;i<256u;++i){
        GuestTimingScope outer(GuestWork::dispatch,0x1000u);
        if(i==255u){
            for(unsigned j=0;j<256u;++j){
                GuestTimingScope nested(GuestWork::dispatch,0x2000u);
                GuestTimingScope micro(GuestWork::vu0,0x30u);
            }
            try {throw std::runtime_error("unwind");}catch(const std::runtime_error&){}
        }
    }
    try {
        for(unsigned i=0;i<256u;++i){GuestTimingScope throwing(GuestWork::dispatch,0x3000u);if(i==255u)throw 1;}
    }catch(int){}
    for(unsigned i=0;i<256u;++i){GuestTimingScope recovered(GuestWork::dispatch,0x4000u);}
    flush_guest_timings();const auto out=captured.str();
    require(out.find("scope=inclusive")!=std::string::npos && out.find("sampling=1/256")!=std::string::npos);
    for(auto key:{"target=0x1000", "target=0x3000", "target=0x4000", "stage=vu0 target=0x30"})
        require(out.find(key)!=std::string::npos);
    require(out.find("target=0x2000")==std::string::npos && out.find("target=0xdead")==std::string::npos);
    captured.str("");captured.clear();flush_guest_timings();require(captured.str().empty());
    enable_guest_timings(false);require(sample_draw_history());
    std::cout<<"Guest timing regression passed: sampling, nested scopes, independent subsystems, unwinding, and flush.\n";
}
