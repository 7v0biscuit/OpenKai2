// SPDX-License-Identifier: GPL-3.0-only
#include "vector_checks.h"
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    const bool reference=argc>1 && std::string(argv[1])=="--reference";
    const unsigned iterations=argc>2 ? unsigned(std::stoul(argv[2])) : 10000u;
    for(unsigned mode=0;mode<16u;++mode)for(unsigned variant=0;variant<(reference ? 4u : 1u);++variant){
        const auto result=openkai2::vector_workload(mode,reference ? 3u+5u*variant : iterations);
        std::cout<<"mode="<<mode<<" variant="<<variant<<" ns_per_run="<<result.nsPerRun<<" cycles="<<result.cycles
            <<" packets="<<result.packets<<" hash="<<std::hex<<result.hash<<std::dec<<'\n';
    }
}
