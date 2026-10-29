# SPDX-License-Identifier: GPL-3.0-only
function(openkai2_add_soundtouch project_root)
    if(TARGET openkai2_soundtouch)
        return()
    endif()
    set(root "${project_root}/third_party/soundtouch/upstream")
    set(names AAFilter cpu_detect_x86 FIFOSampleBuffer FIRFilter InterpolateCubic
        InterpolateLinear InterpolateShannon mmx_optimized RateTransposer SoundTouch
        sse_optimized TDStretch)
    set(sources "")
    foreach(name IN LISTS names)
        list(APPEND sources "${root}/source/SoundTouch/${name}.cpp")
    endforeach()
    add_library(openkai2_soundtouch STATIC ${sources})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/soundtouch-config")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/soundtouch-config/soundtouch_config.h"
        "// Configured by OpenKai2 CMake; sample format is set on the target.\n#pragma once\n")
    target_include_directories(openkai2_soundtouch PUBLIC "${root}/soundtouch"
        "${CMAKE_CURRENT_BINARY_DIR}/soundtouch-config")
    target_compile_features(openkai2_soundtouch PUBLIC cxx_std_17)
    target_compile_definitions(openkai2_soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES ST_NO_EXCEPTION_HANDLING)
endfunction()
