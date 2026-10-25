// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_interlaced_presentation();
        std::cout<<"Runtime interlaced presentation regression passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
