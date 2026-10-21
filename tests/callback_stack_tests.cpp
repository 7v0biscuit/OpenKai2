// SPDX-License-Identifier: GPL-3.0-only
#include "callback_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_callback_stacks();
        std::cout << "Callback stack regression passed: bootstrap frame, nested stacks, and arena bounds.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
