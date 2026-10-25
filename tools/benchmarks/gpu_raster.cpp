// SPDX-License-Identifier: GPL-3.0-only
#include "render/gpu_checks.h"
#include <string>
int main(int argc,char** argv){
    if(argc>1 && std::string(argv[1])=="--footprints")return openkai2::benchmark_gpu_footprints(argc>2 ? unsigned(std::stoul(argv[2])) : 10000u);
    if(argc>1 && std::string(argv[1])=="--submission")return openkai2::benchmark_gpu_submission(argc>2 ? unsigned(std::stoul(argv[2])) : 10000u);
    return openkai2::benchmark_gpu_renderer(argc>1 ? unsigned(std::stoul(argv[1])) : 256u);
}
