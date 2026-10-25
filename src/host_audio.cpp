// SPDX-License-Identifier: GPL-3.0-only
#include "host_audio.h"
#include <iostream>
namespace openkai2 {
void start_host_audio(const void*,bool) {
    std::cerr << "[audio:output] enabled=0 reason=performance-development\n";
}
void stop_host_audio(const void*) {}
void submit_host_audio(const void*,unsigned,std::span<const int16_t>,uint32_t) {}
int test_host_audio() {
    std::cout << "Host audio is disabled in this build. No output device is opened.\n";
    return 0;
}
}
