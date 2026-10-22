enable_testing()

function(openkai2_attach_generated_sources)
    option(OPENKAI2_RUNTIME_TESTS_ONLY "Build runtime integration tests without translated game code" OFF)
    if(NOT TARGET ps2EntryRunner)
        message(FATAL_ERROR "PS2Recomp runtime runner target is unavailable")
    endif()
    get_target_property(runner_sources ps2EntryRunner SOURCES)
    if(NOT OPENKAI2_RUNTIME_TESTS_ONLY)
        list(FILTER runner_sources EXCLUDE REGEX "src/runner/register_functions\\.cpp$")
    endif()
    list(FILTER runner_sources EXCLUDE REGEX "/(runner/)?main\\.cpp$")
    set_property(TARGET ps2EntryRunner PROPERTY SOURCES "${runner_sources}")
    file(GLOB generated_sources CONFIGURE_DEPENDS
        "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output/*.cpp")
    if(OPENKAI2_RUNTIME_TESTS_ONLY)
        set(generated_sources "")
    elseif(NOT generated_sources)
        message(FATAL_ERROR "No generated Budokai 2 sources; run ps2_recomp first")
    endif()
    get_filename_component(project_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    add_library(openkai2_disc STATIC "${project_root}/src/disc_image.cpp")
    target_include_directories(openkai2_disc PUBLIC "${project_root}/src")
    target_compile_features(openkai2_disc PUBLIC cxx_std_17)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${project_root}/tools/native/translation-entries.py"
        --emit-check-header "${CMAKE_BINARY_DIR}/openkai2_translation_entries.h"
        COMMAND_ERROR_IS_FATAL ANY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${project_root}/tools/native/translation-entries.py")
    if(NOT OPENKAI2_RUNTIME_TESTS_ONLY)
        execute_process(COMMAND "${Python3_EXECUTABLE}"
            "${project_root}/tools/native/translation-entries.py"
            --verify-generated "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output"
            COMMAND_ERROR_IS_FATAL ANY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${project_root}/tools/native/translation-entries.py")
    endif()
    set(overlay_dir "${CMAKE_BINARY_DIR}/openkai2-overlays")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${project_root}/tools/native/prepare-overlays.py"
        --upstream "${CMAKE_SOURCE_DIR}" --output "${overlay_dir}"
        COMMAND_ERROR_IS_FATAL ANY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${project_root}/tools/native/patches/disc-layout.patch"
        "${project_root}/tools/native/patches/iop-startup.patch"
        "${project_root}/tools/native/patches/dma-receive.patch"
        "${project_root}/tools/native/patches/startup-diagnostics.patch"
        "${project_root}/tools/native/patches/callback-stacks.patch"
        "${project_root}/tools/native/patches/thread-bootstrap.patch"
        "${project_root}/tools/native/patches/graphics-diagnostics.patch"
        "${project_root}/tools/native/patches/interlaced-display.patch"
        "${project_root}/tools/native/patches/sprite-coordinates.patch"
        "${project_root}/tools/native/prepare-overlays.py")
    get_target_property(iop_sources ps2_iop SOURCES)
    set(overlay_iop_sources "")
    foreach(source IN LISTS iop_sources)
        set(relative "${source}")
        if(IS_ABSOLUTE "${source}")
            file(RELATIVE_PATH relative "${CMAKE_SOURCE_DIR}/ps2xIOP" "${source}")
        endif()
        if(relative MATCHES "^src/emulator/(.+)$")
            set(source "${overlay_dir}/emulator/${CMAKE_MATCH_1}")
        endif()
        list(APPEND overlay_iop_sources "${source}")
    endforeach()
    set_property(TARGET ps2_iop PROPERTY SOURCES "${overlay_iop_sources}")
    target_sources(ps2_iop PRIVATE "${project_root}/src/cdvd_rpc.cpp")
    target_include_directories(ps2_iop BEFORE PRIVATE "${overlay_dir}")
    foreach(test IN ITEMS ps2_iop_emulator_tests ps2_iop_import_tests
            ps2_iop_compatibility_tests ps2_iop_import_version_tests)
        if(TARGET ${test})
            target_include_directories(${test} BEFORE PRIVATE "${overlay_dir}")
        endif()
    endforeach()
    target_link_libraries(ps2_iop PRIVATE openkai2_disc)
    add_library(openkai2_guest_checks STATIC "${project_root}/src/sound_checks.cpp"
        "${project_root}/src/iop_checks.cpp" "${project_root}/src/iop_wait_checks.cpp")
    target_include_directories(openkai2_guest_checks PUBLIC "${project_root}/src"
        PRIVATE "${CMAKE_SOURCE_DIR}/ps2xIOP/src")
    target_include_directories(openkai2_guest_checks BEFORE PRIVATE "${overlay_dir}")
    target_link_libraries(openkai2_guest_checks PUBLIC ps2_iop)
    add_executable(openkai2_sound_tests "${project_root}/tests/sound_register_tests.cpp")
    target_link_libraries(openkai2_sound_tests PRIVATE openkai2_guest_checks)
    add_test(NAME sound_registers COMMAND openkai2_sound_tests)
    add_executable(openkai2_iop_pipeline_tests "${project_root}/tests/iop_pipeline_tests.cpp")
    target_link_libraries(openkai2_iop_pipeline_tests PRIVATE openkai2_guest_checks)
    add_test(NAME iop_import_pipeline COMMAND openkai2_iop_pipeline_tests)
    add_executable(openkai2_iop_wait_tests "${project_root}/tests/iop_wait_tests.cpp")
    target_link_libraries(openkai2_iop_wait_tests PRIVATE openkai2_guest_checks)
    add_test(NAME iop_wait_diagnostics COMMAND openkai2_iop_wait_tests)
    get_target_property(runtime_sources ps2_runtime SOURCES)
    set(overlay_runtime_sources "")
    set(thread_overlay_count 0)
    foreach(source IN LISTS runtime_sources)
        if(source MATCHES "(^|/)src/lib/(ps2_memory|ps2_runtime)\\.cpp$")
            get_filename_component(runtime_file "${source}" NAME)
            set(source "${overlay_dir}/runtime/${runtime_file}")
        endif()
        if(source MATCHES "(^|/)src/lib/gs/gs_cpu_backend\\.cpp$")
            set(source "${overlay_dir}/runtime/gs/gs_cpu_backend.cpp")
        endif()
        string(REPLACE "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel/Stubs/"
            "${overlay_dir}/Kernel/Stubs/" source "${source}")
        string(REPLACE "\\" "/" normalized_source "${source}")
        if(normalized_source MATCHES "(^|/)src/lib/Kernel/Syscalls/Thread\\.cpp$")
            set(source "${overlay_dir}/Kernel/Syscalls/Thread.cpp")
            math(EXPR thread_overlay_count "${thread_overlay_count}+1")
            message(STATUS "EE thread implementation: ${source}")
        endif()
        list(APPEND overlay_runtime_sources "${source}")
    endforeach()
    if(NOT thread_overlay_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one patched EE thread implementation; found ${thread_overlay_count}")
    endif()
    set_property(TARGET ps2_runtime PROPERTY SOURCES "${overlay_runtime_sources}")
    target_include_directories(ps2_runtime PRIVATE "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel/Stubs")
    target_link_libraries(ps2_runtime PRIVATE openkai2_disc)
    add_library(openkai2_thread_checks STATIC "${project_root}/src/thread_checks.cpp")
    target_include_directories(openkai2_thread_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_thread_checks PUBLIC ps2_runtime)
    add_executable(openkai2_thread_tests "${project_root}/tests/thread_bootstrap_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_thread_tests PRIVATE openkai2_thread_checks)
    if(MSVC)
        target_link_options(openkai2_thread_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_thread_tests)
    add_test(NAME thread_bootstrap COMMAND openkai2_thread_tests)
    add_library(openkai2_callback_checks STATIC "${project_root}/src/callback_checks.cpp")
    target_include_directories(openkai2_callback_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_callback_checks PUBLIC ps2_runtime)
    add_executable(openkai2_callback_tests "${project_root}/tests/callback_stack_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_callback_tests PRIVATE openkai2_callback_checks)
    if(MSVC)
        target_link_options(openkai2_callback_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_callback_tests)
    add_test(NAME callback_stacks COMMAND openkai2_callback_tests)
    target_sources(ps2_runtime PRIVATE "${project_root}/src/startup_diagnostics.cpp")
    target_include_directories(ps2_runtime PRIVATE "${project_root}/src"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib")
    add_library(openkai2_startup_checks STATIC "${project_root}/src/startup_checks.cpp")
    target_include_directories(openkai2_startup_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_startup_checks PUBLIC ps2_runtime)
    add_executable(openkai2_startup_tests "${project_root}/tests/startup_diagnostics_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_startup_tests PRIVATE openkai2_startup_checks)
    if(MSVC)
        target_link_options(openkai2_startup_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_startup_tests)
    add_test(NAME startup_diagnostics COMMAND openkai2_startup_tests)
    add_library(openkai2_graphics_checks STATIC "${project_root}/src/graphics_checks.cpp")
    target_include_directories(openkai2_graphics_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_graphics_checks PUBLIC ps2_runtime)
    add_executable(openkai2_graphics_tests "${project_root}/tests/graphics_presentation_tests.cpp")
    target_link_libraries(openkai2_graphics_tests PRIVATE openkai2_graphics_checks)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_graphics_tests)
    add_test(NAME interlaced_presentation COMMAND openkai2_graphics_tests)
    add_library(openkai2_dma_checks STATIC "${project_root}/src/dma_checks.cpp")
    target_include_directories(openkai2_dma_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_dma_checks PUBLIC ps2_runtime)
    add_executable(openkai2_dma_tests "${project_root}/tests/dma_receive_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_dma_tests PRIVATE openkai2_dma_checks)
    if(MSVC)
        target_link_options(openkai2_dma_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_dma_tests)
    add_test(NAME scratchpad_dma_receive COMMAND openkai2_dma_tests)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_disc openkai2_guest_checks)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_dma_checks)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_startup_checks)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_graphics_checks)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_callback_checks openkai2_thread_checks)
    if(WIN32)
        target_link_libraries(ps2EntryRunner PRIVATE comdlg32)
    endif()
    target_sources(ps2EntryRunner PRIVATE ${generated_sources} "${project_root}/src/main.cpp"
        "${project_root}/src/runtime_checks.cpp" "${project_root}/src/stream_checks.cpp"
        "${project_root}/src/sound_transfer_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp" "${project_root}/src/mpeg_checks.cpp")
    # Keep the newly exposed IPU sync routine separate so adding its source
    # preserves the existing translation batches during incremental builds.
    set_source_files_properties("$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output/entry_109390_0x1093f8.cpp"
        TARGET_DIRECTORY ps2EntryRunner PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
    set_source_files_properties("${project_root}/src/main.cpp" "${project_root}/src/runtime_checks.cpp"
        "${project_root}/src/stream_checks.cpp" "${project_root}/src/sound_transfer_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp" "${project_root}/src/mpeg_checks.cpp"
        TARGET_DIRECTORY ps2EntryRunner PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
    set_property(SOURCE "${project_root}/src/runtime_checks.cpp" "${project_root}/src/stream_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp"
        TARGET_DIRECTORY ps2EntryRunner APPEND PROPERTY
        INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
    target_include_directories(ps2EntryRunner BEFORE PRIVATE
        "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output")
    # Initial execution diagnosis: keep the very large generated translation
    # inexpensive to compile while retaining the upstream runtime's optimization.
    if(MSVC)
        target_compile_options(ps2EntryRunner PRIVATE /Od /bigobj)
    else()
        target_compile_options(ps2EntryRunner PRIVATE -O0 -g0)
    endif()
    get_target_property(runner_pch ps2EntryRunner PRECOMPILE_HEADERS)
    if(NOT runner_pch)
        set(runner_pch "")
    endif()
    set_property(TARGET ps2EntryRunner PROPERTY PRECOMPILE_HEADERS
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/gcc-portability.h;${runner_pch}")
    list(LENGTH generated_sources generated_count)
    message(STATUS "Attached ${generated_count} generated Budokai 2 sources")
    add_executable(openkai2_cdvd_tests "${project_root}/tests/cdvd_iso_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_include_directories(openkai2_cdvd_tests PRIVATE "${overlay_dir}" "${CMAKE_SOURCE_DIR}/ps2xIOP/src")
    target_link_libraries(openkai2_cdvd_tests PRIVATE ps2_runtime openkai2_disc)
    if(MSVC)
        target_link_options(openkai2_cdvd_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_cdvd_tests)
    add_test(NAME cdvd_iso_layout COMMAND openkai2_cdvd_tests)
    if(OPENKAI2_RUNTIME_TESTS_ONLY)
        add_test(NAME launcher_help COMMAND ps2EntryRunner --help)
        set_tests_properties(launcher_help PROPERTIES
            PASS_REGULAR_EXPRESSION "The original game ISO is required")
    endif()
endfunction()
cmake_language(DEFER CALL openkai2_attach_generated_sources)
