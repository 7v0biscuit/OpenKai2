// SPDX-License-Identifier: GPL-3.0-only
#include "render/gpu_checks.h"
#include <string>
int main(int argc,char** argv){return openkai2::run_gpu_checks(argc>1 && std::string(argv[1])=="--required");}
