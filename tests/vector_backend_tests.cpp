// SPDX-License-Identifier: GPL-3.0-only
#include "vector/backend_checks.h"
#include <exception>
#include <iostream>
int main() {
    try {
        openkai2::verify_vector_backend();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Vector backend check failed: " << error.what() << '\n';
        return 1;
    }
}
