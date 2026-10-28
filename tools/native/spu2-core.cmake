# SPDX-License-Identifier: GPL-3.0-only
set(spu2_root "${project_root}/third_party/pcsx2-spu2/upstream/pcsx2")
add_library(openkai2_spu2 STATIC
    "${spu2_root}/SPU2/ADSR.cpp"
    "${spu2_root}/SPU2/Dma.cpp"
    "${spu2_root}/SPU2/Mixer.cpp"
    "${spu2_root}/SPU2/ReadInput.cpp"
    "${spu2_root}/SPU2/RegTable.cpp"
    "${spu2_root}/SPU2/Reverb.cpp"
    "${spu2_root}/SPU2/ReverbResample.cpp"
    "${spu2_root}/SPU2/spu2sys.cpp"
    "${project_root}/src/audio/spu2_mirror.cpp")
target_compile_features(openkai2_spu2 PRIVATE cxx_std_20)
target_include_directories(openkai2_spu2 PRIVATE "${project_root}/src/audio/spu2/shims"
    "${spu2_root}" "${spu2_root}/SPU2" "${project_root}/third_party/pcsx2-vu/upstream"
    PUBLIC "${project_root}/src")
target_compile_definitions(openkai2_spu2 PRIVATE _M_X86=1 _M_X86_64=1 WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(openkai2_spu2 PRIVATE /utf-8 "/FI${project_root}/src/audio/spu2/shims/Spu2Prefix.h")
else()
    target_compile_options(openkai2_spu2 PRIVATE -msse4.1 -fno-strict-aliasing -Wno-attributes
        -include "${project_root}/src/audio/spu2/shims/Spu2Prefix.h")
endif()
