// SPDX-License-Identifier: GPL-3.0-only
#include "raster_shading_checks.h"
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    const bool reference=argc>1 && std::string(argv[1])=="--reference";
    const unsigned iterations=reference ? 1u : argc>2 ? unsigned(std::stoul(argv[2])) : 64u;
    const unsigned workers=argc>3 ? unsigned(std::stoul(argv[3])) : 1u;
    for(unsigned mode=0;mode<128u;++mode) {
        const auto r=openkai2::raster_shading_workload(mode,iterations,workers);
        std::cout<<"mode="<<mode<<" workers="<<workers<<" ns_per_draw="<<r.nanosecondsPerDraw<<" hash="<<std::hex<<r.hash<<std::dec<<'\n';
    }
}
