// SPDX-License-Identifier: GPL-3.0-only
#include "sound_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_sound_registers();
        std::cout << "SPU2 initialization regression passed: both cores, DMA status, and guest polling.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
