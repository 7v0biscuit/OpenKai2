// SPDX-License-Identifier: GPL-3.0-only
#include "dma_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_scratchpad_dma();
        std::cout << "Scratchpad DMA receive regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
