// SPDX-License-Identifier: GPL-3.0-only
#include "frame_presenter.h"
#include <iostream>
int main() {
    try {openkai2::verify_frame_presenter();std::cout<<"Asynchronous presentation regression passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
