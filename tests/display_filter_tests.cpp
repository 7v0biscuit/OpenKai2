// SPDX-License-Identifier: GPL-3.0-only
#include "render/presentation_filter.h"
#include <exception>
#include <iostream>
int main() {
    try {
        return openkai2::test_presentation_filters();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
