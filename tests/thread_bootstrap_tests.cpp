// SPDX-License-Identifier: GPL-3.0-only
#include "thread_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_thread_bootstrap();
        openkai2::verify_thread_wakeup();
        std::cout << "EE thread bootstrap regression passed: inherited priority, worker creation, and wake syscall results.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
