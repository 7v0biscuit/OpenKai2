// SPDX-License-Identifier: GPL-3.0-only
#include "host_input.h"
#include <iostream>
int main() {
    try {openkai2::verify_host_input();std::cout<<"Host input regression passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
