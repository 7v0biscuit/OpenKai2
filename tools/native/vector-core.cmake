# SPDX-License-Identifier: GPL-3.0-only
set(pcsx2_root "${project_root}/third_party/pcsx2-vu/upstream")
set(pcsx2_adapter "${project_root}/src/vector/pcsx2")
add_library(openkai2_vector_core STATIC
    "${pcsx2_root}/pcsx2/VUops.cpp"
    "${pcsx2_root}/pcsx2/VUflags.cpp"
    "${pcsx2_root}/pcsx2/VU0microInterp.cpp"
    "${pcsx2_root}/pcsx2/VU1microInterp.cpp"
    "${pcsx2_root}/pcsx2/x86/microVU.cpp"
    "${pcsx2_root}/common/AlignedMalloc.cpp"
    "${project_root}/src/vector/pcsx2_host.cpp")
file(GLOB emitter_sources CONFIGURE_DEPENDS "${pcsx2_root}/common/emitter/*.cpp")
target_sources(openkai2_vector_core PRIVATE ${emitter_sources})
target_compile_features(openkai2_vector_core PRIVATE cxx_std_20)
target_compile_definitions(openkai2_vector_core PRIVATE _M_X86=1 _M_X86_64=1
    WIN32_LEAN_AND_MEAN NOMINMAX FMT_HEADER_ONLY=1)
target_include_directories(openkai2_vector_core PRIVATE "${pcsx2_adapter}/shims"
    "${pcsx2_adapter}" "${pcsx2_root}" "${pcsx2_root}/pcsx2"
    "${pcsx2_root}/fmt/include" "${pcsx2_root}/3rdparty/fast_float/include" "${overlay_dir}/include"
    "${project_root}/src")
if(MSVC)
    # MSVC exposes SSE4.1 intrinsics without a matching /arch setting and does
    # not define this feature macro. Keep the documented SSE4.1 baseline.
    target_compile_definitions(openkai2_vector_core PRIVATE __SSE4_1__=1)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(openkai2_vector_core PRIVATE /clang:-msse4.1)
    endif()
endif()
if(MINGW)
    target_compile_options(openkai2_vector_core PRIVATE -include "${pcsx2_adapter}/mingw-crt.h")
endif()
if(NOT MSVC)
    target_compile_options(openkai2_vector_core PRIVATE -msse4.1 -fno-strict-aliasing
        -Wno-attributes -Wno-ignored-attributes)
endif()
target_link_libraries(openkai2_vector_core PRIVATE Threads::Threads)
