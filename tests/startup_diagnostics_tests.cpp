// SPDX-License-Identifier: GPL-3.0-only
#include "startup_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_startup_diagnostics();
        std::cout << "Startup diagnostics regression passed: bounded, read-only EE wait snapshots.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
