// SPDX-License-Identifier: GPL-3.0-only
#include "iop_wait_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_iop_wait_diagnostics();
        std::cout << "IOP wait diagnostics regression passed: read-only semaphore, event, and instruction snapshots.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
