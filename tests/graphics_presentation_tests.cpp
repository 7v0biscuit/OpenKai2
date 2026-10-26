// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include <exception>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="--gif-only")openkai2::verify_gif_vertices();
        else openkai2::verify_interlaced_presentation();
        std::cout<<"Runtime interlaced presentation regression passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
