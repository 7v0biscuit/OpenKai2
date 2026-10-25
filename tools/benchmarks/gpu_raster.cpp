// SPDX-License-Identifier: GPL-3.0-only
#include "render/gpu_checks.h"
#include <string>
int main(int argc,char** argv){return openkai2::benchmark_gpu_renderer(argc>1 ? unsigned(std::stoul(argv[1])) : 256u);}
