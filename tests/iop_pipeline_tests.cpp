// SPDX-License-Identifier: GPL-3.0-only
#include "iop_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_iop_import_pipeline();
        std::cout << "IOP import pipeline regression passed: thread arguments, RPC handler, and return values.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
