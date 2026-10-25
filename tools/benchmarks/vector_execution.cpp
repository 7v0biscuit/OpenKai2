// SPDX-License-Identifier: GPL-3.0-only
#include "vector_checks.h"
#include "vector_hotpath_checks.h"
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    if(argc>1 && (std::string(argv[1])=="--pipeline-reference" || std::string(argv[1])=="--pipeline-bench")) {
        const bool reference=std::string(argv[1])=="--pipeline-reference";
        const unsigned iterations=argc>2 ? unsigned(std::stoul(argv[2])) : 1000u;
        constexpr unsigned budgets[]={1u,2u,3u,4u,7u,13u,31u,127u};
        for(unsigned seed=0;seed<16u;++seed)for(unsigned i=0;i<8u;++i){
            const auto result=openkai2::vector_pipeline_workload(seed,budgets[i],reference ? 1u : iterations);
            std::cout<<"seed="<<seed<<" budget="<<budgets[i]<<" ns_per_run="<<result.nsPerRun<<" cycles="<<result.cycles
                <<" packets="<<result.packets<<" hash="<<std::hex<<result.hash<<std::dec<<'\n';
        }
        return 0;
    }
    if(argc>1 && (std::string(argv[1])=="--arithmetic-reference" || std::string(argv[1])=="--unpack-reference"
        || std::string(argv[1])=="--arithmetic-bench" || std::string(argv[1])=="--unpack-bench" || std::string(argv[1])=="--unpack-stream")) {
        const std::string option=argv[1];const bool arithmetic=option.find("arithmetic")!=std::string::npos;
        const unsigned iterations=option.find("reference")!=std::string::npos ? 1u : argc>2 ? unsigned(std::stoul(argv[2])) : 10u;
        for(unsigned group=0;group<8u;++group){
            const auto r=arithmetic ? openkai2::arithmetic_workload(group,iterations) : option=="--unpack-stream" ? openkai2::unpack_stream_workload(group,iterations) : openkai2::unpack_workload(group,iterations);
            std::cout<<"group="<<group<<" ns_per_operation="<<r.nanosecondsPerOperation<<" operations="<<r.operations<<" hash="<<std::hex<<r.hash<<std::dec<<'\n';
        }
        return 0;
    }
    const bool reference=argc>1 && std::string(argv[1])=="--reference";
    const unsigned iterations=argc>2 ? unsigned(std::stoul(argv[2])) : 10000u;
    for(unsigned mode=0;mode<16u;++mode)for(unsigned variant=0;variant<(reference ? 4u : 1u);++variant){
        const auto result=openkai2::vector_workload(mode,reference ? 3u+5u*variant : iterations);
        std::cout<<"mode="<<mode<<" variant="<<variant<<" ns_per_run="<<result.nsPerRun<<" cycles="<<result.cycles
            <<" packets="<<result.packets<<" hash="<<std::hex<<result.hash<<std::dec<<'\n';
    }
}
