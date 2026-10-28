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
    execute_process(COMMAND git -C "${project_root}" rev-parse HEAD
        OUTPUT_VARIABLE runtime_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT runtime_revision)
        set(runtime_revision "unknown")
    endif()
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/openkai2_build_revision.h"
        CONTENT "#define OPENKAI2_SOURCE_REVISION \"@runtime_revision@\"\n" @ONLY)
    set_property(SOURCE "${project_root}/src/main.cpp" TARGET_DIRECTORY ps2EntryRunner
        APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
    add_library(openkai2_disc STATIC "${project_root}/src/disc_image.cpp")
    target_include_directories(openkai2_disc PUBLIC "${project_root}/src")
    target_compile_features(openkai2_disc PUBLIC cxx_std_17)
    if(WIN32)
        target_link_libraries(openkai2_disc PUBLIC bcrypt)
    endif()
    add_library(openkai2_launcher_model STATIC "${project_root}/src/launcher/settings.cpp"
        "${project_root}/src/launcher/saves.cpp" "${project_root}/src/launcher/session.cpp" "${project_root}/src/launcher/update.cpp")
    target_link_libraries(openkai2_launcher_model PUBLIC openkai2_disc)
    target_compile_features(openkai2_launcher_model PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(openkai2_launcher_model PRIVATE /utf-8)
    endif()
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_launcher_model)
    if(WIN32)
        # The first icon labels the executable; GLFW gives its game window GLFW_ICON.
        set(icon "${project_root}/packaging/windows/openkai2.ico")
        file(WRITE "${CMAKE_BINARY_DIR}/openkai2_icon.rc" "1 ICON \"${icon}\"\nGLFW_ICON ICON \"${icon}\"\n")
        target_sources(ps2EntryRunner PRIVATE "${CMAKE_BINARY_DIR}/openkai2_icon.rc")
    endif()
    include("${project_root}/tools/native/launcher-ui.cmake")
    openkai2_add_launcher_ui("${project_root}" TRUE)
    if(WIN32)
        target_link_libraries(ps2EntryRunner PRIVATE openkai2_launcher_ui)
    endif()
    add_executable(openkai2_launcher_tests "${project_root}/tests/launcher_model_tests.cpp")
    target_link_libraries(openkai2_launcher_tests PRIVATE openkai2_launcher_model)
    if(MSVC)
        target_compile_options(openkai2_launcher_tests PRIVATE /utf-8)
    endif()
    add_test(NAME launcher_settings_saves COMMAND openkai2_launcher_tests)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(Threads REQUIRED)
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
        execute_process(COMMAND "${Python3_EXECUTABLE}"
            "${project_root}/tools/native/patch-projection.py"
            --directory "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${Python3_EXECUTABLE}"
            "${project_root}/tools/native/patch-vector-macros.py"
            --directory "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output"
            COMMAND_ERROR_IS_FATAL ANY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${project_root}/tools/native/translation-entries.py")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${project_root}/tools/native/patch-projection.py"
        "${project_root}/tools/native/patch-vector-macros.py")
    set(overlay_dir "${CMAKE_BINARY_DIR}/openkai2-overlays")
    include("${project_root}/tools/native/vector-core.cmake")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${project_root}/tools/native/prepare-overlays.py"
        --upstream "${CMAKE_SOURCE_DIR}" --output "${overlay_dir}"
        COMMAND_ERROR_IS_FATAL ANY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${project_root}/tools/native/patches/launcher-display.patch"
        "${project_root}/tools/native/patches/window-redraw-pacing.patch"
        "${project_root}/tools/native/patches/disc-timing.patch"
        "${project_root}/tools/native/patches/fpu-conversion.patch"
        "${project_root}/tools/native/patches/soft-double-math.patch"
        "${project_root}/tools/native/patches/vector-data-memory.patch"
        "${project_root}/tools/native/patches/gif-path-timings.patch"
        "${project_root}/tools/native/patches/spu2-mirror.patch"
        "${project_root}/tools/native/patches/vif-unpack-lanes.patch"
        "${project_root}/tools/native/patches/simulation-pacing.patch"
        "${project_root}/tools/native/patches/vector-native-backend.patch"
        "${project_root}/tools/native/patches/guest-timing.patch"
        "${project_root}/tools/native/patches/vu-context.patch"
        "${project_root}/tools/native/patches/draw-history-sampling.patch"
        "${project_root}/tools/native/patches/texture-cache-binding.patch"
        "${project_root}/tools/native/patches/tested-pixel-writer.patch"
        "${project_root}/tools/native/patches/host-audio-device.patch"
        "${project_root}/tools/native/patches/vector-throughput.patch"
        "${project_root}/tools/native/patches/vector-scheduling.patch"
        "${project_root}/tools/native/patches/vector-arithmetic.patch"
        "${project_root}/tools/native/patches/vif-unpack.patch"
        "${project_root}/tools/native/patches/raster-shading.patch"
        "${project_root}/tools/native/patches/vector-product-flags.patch"
        "${project_root}/tools/native/patches/vector-transfers.patch"
        "${project_root}/tools/native/patches/vector-operands.patch"
        "${project_root}/tools/native/patches/vector-flag-batches.patch"
        "${project_root}/tools/native/patches/vector-fetch-validation.patch"
        "${project_root}/tools/native/patches/vector-constant-register.patch"
        "${project_root}/tools/native/patches/vector-fmac-kernel.patch"
        "${project_root}/tools/native/patches/gpu-rendering.patch"
        "${project_root}/tools/native/patches/flat-depth.patch"
        "${project_root}/tools/native/patches/display-pacing.patch"
        "${project_root}/tools/native/patches/gif-vertex-state.patch"
        "${project_root}/tools/native/patches/gif-tag-format.patch"
        "${project_root}/tools/native/patches/vector-code-diagnostics.patch"
        "${project_root}/tools/native/patches/work-timings.patch"
        "${project_root}/tools/native/patches/disc-layout.patch"
        "${project_root}/tools/native/patches/iop-startup.patch"
        "${project_root}/tools/native/patches/dma-receive.patch"
        "${project_root}/tools/native/patches/startup-diagnostics.patch"
        "${project_root}/tools/native/patches/callback-stacks.patch"
        "${project_root}/tools/native/patches/thread-bootstrap.patch"
        "${project_root}/tools/native/patches/thread-wakeup.patch"
        "${project_root}/tools/native/patches/thread-priority.patch"
        "${project_root}/tools/native/patches/graphics-diagnostics.patch"
        "${project_root}/tools/native/patches/interlaced-display.patch"
        "${project_root}/tools/native/patches/sprite-coordinates.patch"
        "${project_root}/tools/native/patches/field-presentation.patch"
        "${project_root}/tools/native/patches/mpeg-ipu-input.patch"
        "${project_root}/tools/native/patches/mpeg-native-scheduling.patch"
        "${project_root}/tools/native/patches/movie-performance-diagnostics.patch"
        "${project_root}/tools/native/patches/sprite-sampling.patch"
        "${project_root}/tools/native/patches/sprite-throughput.patch"
        "${project_root}/tools/native/patches/host-input.patch"
        "${project_root}/tools/native/patches/asynchronous-presentation.patch"
        "${project_root}/tools/native/patches/input-observation.patch"
        "${project_root}/tools/native/patches/texture-pages.patch"
        "${project_root}/tools/native/patches/empty-dma-completion.patch"
        "${project_root}/tools/native/patches/parallel-sprites.patch"
        "${project_root}/tools/native/patches/guest-progress.patch"
        "${project_root}/tools/native/patches/interrupt-critical-sections.patch"
        "${project_root}/tools/native/patches/sprite-formats.patch"
        "${project_root}/tools/native/patches/pixel-blending.patch"
        "${project_root}/tools/native/patches/spu2-pcm-output.patch"
        "${project_root}/tools/native/patches/iop-import-cache.patch"
        "${project_root}/tools/native/patches/small-triangles.patch"
        "${project_root}/tools/native/patches/iop-execution.patch"
        "${project_root}/tools/native/patches/ct32-pixels.patch"
        "${project_root}/tools/native/patches/early-depth.patch"
        "${project_root}/tools/native/patches/prepared-clut.patch"
        "${project_root}/tools/native/patches/draw-clut-sampling.patch"
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
    target_compile_definitions(ps2_iop PRIVATE OPENKAI2_SPU2_PCM=1)
    target_sources(ps2_iop PRIVATE "${project_root}/src/cdvd_rpc.cpp")
    target_include_directories(ps2_iop BEFORE PRIVATE "${overlay_dir}")
    foreach(test IN ITEMS ps2_iop_emulator_tests ps2_iop_import_tests
            ps2_iop_compatibility_tests ps2_iop_import_version_tests)
        if(TARGET ${test})
            target_include_directories(${test} BEFORE PRIVATE "${overlay_dir}" "${project_root}/src")
        endif()
    endforeach()
    include("${project_root}/tools/native/spu2-core.cmake")
    target_link_libraries(ps2_iop PRIVATE openkai2_disc openkai2_spu2)
    add_library(openkai2_guest_checks STATIC "${project_root}/src/sound_checks.cpp"
        "${project_root}/src/iop_checks.cpp" "${project_root}/src/iop_wait_checks.cpp")
    target_compile_definitions(openkai2_guest_checks PRIVATE OPENKAI2_SPU2_PCM=1)
    target_include_directories(openkai2_guest_checks PUBLIC "${project_root}/src"
        PRIVATE "${CMAKE_SOURCE_DIR}/ps2xIOP/src")
    target_include_directories(openkai2_guest_checks BEFORE PRIVATE "${overlay_dir}")
    target_link_libraries(openkai2_guest_checks PUBLIC ps2_iop)
    add_executable(openkai2_spu2_tests "${project_root}/tests/spu2_mirror_tests.cpp")
    target_link_libraries(openkai2_spu2_tests PRIVATE openkai2_spu2)
    add_test(NAME spu2_mirror COMMAND openkai2_spu2_tests)
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
        if(source MATCHES "(^|/)src/lib/gs/ps2_gs_memory\\.cpp$")
            continue()
        endif()
        if(source MATCHES "(^|/)src/lib/gs/gs_cpu_backend\\.cpp$")
            set(source "${project_root}/tools/native/raster-unit.cpp")
            set_property(SOURCE "${source}" TARGET_DIRECTORY ps2_runtime APPEND PROPERTY
                INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/gs")
        endif()
        if(source MATCHES "(^|/)src/lib/vu/ps2_vu1_(upper|lower)\\.cpp$")
            continue()
        endif()
        if(source MATCHES "(^|/)src/lib/vu/ps2_vu1_core\\.cpp$")
            set(source "${project_root}/tools/native/vector-unit.cpp")
            set_property(SOURCE "${source}" TARGET_DIRECTORY ps2_runtime APPEND PROPERTY
                INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/vu")
        endif()
        if(source MATCHES "(^|/)src/lib/ps2_audio\\.cpp$")
            set(source "${project_root}/src/silent_audio_backend.cpp")
        endif()
        if(source MATCHES "(^|/)src/lib/Kernel/EeScheduler\\.cpp$")
            set(source "${overlay_dir}/Kernel/EeScheduler.cpp")
        endif()
        if(source MATCHES "(^|/)src/lib/(ps2_memory|ps2_runtime|ps2_pad|ps2_iop_host|ps2_vif1_interpreter)\\.cpp$")
            get_filename_component(runtime_file "${source}" NAME)
            set(source "${overlay_dir}/runtime/${runtime_file}")
        endif()
        if(source MATCHES "(^|/)src/lib/gs/(gs_cpu_backend|gs_frontend)\\.cpp$")
            get_filename_component(graphics_file "${source}" NAME)
            set(source "${overlay_dir}/runtime/gs/${graphics_file}")
        endif()
        string(REPLACE "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel/Stubs/"
            "${overlay_dir}/Kernel/Stubs/" source "${source}")
        string(REPLACE "\\" "/" normalized_source "${source}")
        if(normalized_source MATCHES "(^|/)src/lib/Kernel/Syscalls/Thread\\.cpp$")
            set(source "${overlay_dir}/Kernel/Syscalls/Thread.cpp")
            math(EXPR thread_overlay_count "${thread_overlay_count}+1")
            message(STATUS "EE thread implementation: ${source}")
        endif()
        if(source MATCHES "/gs/(gs_cpu_backend|gs_frontend|ps2_gs_memory)\\.cpp$")
            set_property(SOURCE "${source}" TARGET_DIRECTORY ps2_runtime APPEND PROPERTY
                INCLUDE_DIRECTORIES "${overlay_dir}/include")
        endif()
        list(APPEND overlay_runtime_sources "${source}")
    endforeach()
    if(NOT thread_overlay_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one patched EE thread implementation; found ${thread_overlay_count}")
    endif()
    set_property(TARGET ps2_runtime PROPERTY SOURCES "${overlay_runtime_sources}")
    target_sources(ps2_runtime PRIVATE "${project_root}/src/vector/backend.cpp")
    target_link_libraries(ps2_runtime PRIVATE openkai2_vector_core)
    target_sources(ps2_runtime PRIVATE "${project_root}/src/runtime_layout.cpp" "${project_root}/src/host_audio.cpp" "${project_root}/src/work_timings.cpp"
        "${project_root}/src/graphics_settings.cpp" "${project_root}/src/render/presentation_filter.cpp")
    file(READ "${project_root}/src/render/gs_compute.comp" openkai2_gpu_shader)
    file(READ "${project_root}/src/render/gs_transfer.comp" openkai2_gpu_transfer_shader)
    configure_file("${project_root}/src/render/shader_source.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/openkai2_gpu_shader.h" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project_root}/src/render/gs_compute.comp"
        "${project_root}/src/render/gs_transfer.comp")
    # Keep individual literals below MSVC's string-literal limit. The original
    # AMD headers remain byte-for-byte intact in the source tree.
    set(openkai2_fsr_parts "")
    foreach(header ffx_a.h ffx_fsr1.h)
        set(path "${project_root}/third_party/fidelityfx-fsr/${header}")
        file(READ "${path}" contents)
        string(LENGTH "${contents}" length)
        set(offset 0)
        while(offset LESS length)
            string(SUBSTRING "${contents}" ${offset} 12000 part)
            string(APPEND openkai2_fsr_parts "R\"OKFSR(${part})OKFSR\",\n")
            math(EXPR offset "${offset}+12000")
        endwhile()
        string(APPEND openkai2_fsr_parts "\"\\n\",\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endforeach()
    configure_file("${project_root}/src/render/fsr_source.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/openkai2_fsr_shader.h" @ONLY)
    target_sources(ps2_runtime PRIVATE "${project_root}/src/render/gpu_device.cpp"
        "${project_root}/src/render/gpu_backend.cpp" "${project_root}/src/render/gpu_window.cpp")
    find_path(OPENKAI2_GLFW_INCLUDE_DIR GLFW/glfw3.h HINTS
        "${raylib_SOURCE_DIR}/external/glfw/include" "${raylib_SOURCE_DIR}/src/external/glfw/include" REQUIRED)
    target_include_directories(ps2_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${OPENKAI2_GLFW_INCLUDE_DIR}")
    target_link_libraries(ps2_runtime PRIVATE ${CMAKE_DL_LIBS})
    add_executable(openkai2_display_tests
        "${project_root}/tests/display_filter_tests.cpp"
        "${project_root}/src/render/presentation_filter.cpp"
        "${project_root}/src/graphics_settings.cpp")
    target_compile_features(openkai2_display_tests PRIVATE cxx_std_17)
    target_include_directories(openkai2_display_tests PRIVATE
        "${project_root}/src" "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(openkai2_display_tests PRIVATE raylib)

    target_include_directories(ps2_runtime BEFORE PUBLIC "${overlay_dir}/include")
    target_include_directories(ps2_runtime BEFORE PRIVATE "${overlay_dir}" "${overlay_dir}/runtime")
    target_include_directories(ps2_runtime PRIVATE "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel/Stubs")
    target_link_libraries(ps2_runtime PRIVATE openkai2_disc)
    add_library(openkai2_thread_checks STATIC "${project_root}/src/thread_checks.cpp")
    target_sources(openkai2_thread_checks PRIVATE "${project_root}/src/save_checks.cpp"
        "${project_root}/src/host_input_checks.cpp" "${project_root}/src/frame_presenter_checks.cpp"
        "${project_root}/src/pad_checks.cpp")
    add_executable(openkai2_presenter_tests "${project_root}/tests/frame_presenter_tests.cpp"
        "${project_root}/src/frame_presenter.cpp" "${project_root}/src/frame_presenter_checks.cpp" "${project_root}/src/display_settings.cpp")
    target_include_directories(openkai2_presenter_tests PRIVATE "${project_root}/src")
    target_compile_features(openkai2_presenter_tests PRIVATE cxx_std_17)
    target_link_libraries(openkai2_presenter_tests PRIVATE Threads::Threads)
    add_test(NAME asynchronous_presentation COMMAND openkai2_presenter_tests)
    add_executable(openkai2_input_tests "${project_root}/tests/host_input_tests.cpp"
        "${project_root}/src/host_input_checks.cpp")
    target_include_directories(openkai2_input_tests PRIVATE "${project_root}/src")
    target_compile_features(openkai2_input_tests PRIVATE cxx_std_17)
    add_test(NAME host_input COMMAND openkai2_input_tests)
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
    target_sources(ps2_runtime PRIVATE "${project_root}/src/startup_diagnostics.cpp"
        "${project_root}/src/guest_timing.cpp"
        "${project_root}/src/raster_workers.cpp"
        "${project_root}/src/frame_presenter.cpp" "${project_root}/src/display_settings.cpp")
    target_include_directories(ps2_runtime PRIVATE "${project_root}/src"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/lib")
    add_executable(openkai2_guest_timing_tests "${project_root}/tests/guest_timing_tests.cpp"
        "${project_root}/src/guest_timing.cpp")
    target_include_directories(openkai2_guest_timing_tests PRIVATE "${project_root}/src")
    target_compile_features(openkai2_guest_timing_tests PRIVATE cxx_std_17)
    target_link_libraries(openkai2_guest_timing_tests PRIVATE Threads::Threads)
    add_test(NAME guest_timing COMMAND openkai2_guest_timing_tests)
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
    add_library(openkai2_graphics_checks STATIC "${project_root}/src/gif_vertex_checks.cpp" "${project_root}/src/graphics_checks.cpp" "${project_root}/src/raster_shading_checks.cpp")
    target_include_directories(openkai2_graphics_checks PUBLIC "${project_root}/src")
    target_include_directories(openkai2_graphics_checks BEFORE PRIVATE "${overlay_dir}/include")
    target_link_libraries(openkai2_graphics_checks PUBLIC ps2_runtime)
    add_executable(openkai2_graphics_tests "${project_root}/tests/graphics_presentation_tests.cpp")
    target_link_libraries(openkai2_graphics_tests PRIVATE openkai2_graphics_checks)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_graphics_tests)
    add_test(NAME interlaced_presentation COMMAND openkai2_graphics_tests)
    add_library(openkai2_gpu_checks STATIC "${project_root}/src/render/gpu_checks.cpp")
    target_include_directories(openkai2_gpu_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_gpu_checks PUBLIC ps2_runtime openkai2_vector_checks)
    add_executable(openkai2_gpu_tests "${project_root}/tests/gpu_renderer_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_gpu_tests PRIVATE openkai2_gpu_checks)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_gpu_tests)
    add_executable(openkai2_gpu_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/gpu_raster.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_gpu_bench PRIVATE openkai2_gpu_checks)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_gpu_bench)
    foreach(gpu_tool openkai2_gpu_tests openkai2_gpu_bench)
        if(MSVC)
            target_link_options(${gpu_tool} PRIVATE /STACK:8388608 /FORCE:MULTIPLE)
        elseif(MINGW)
            target_link_options(${gpu_tool} PRIVATE "-Wl,--stack,8388608")
        endif()
    endforeach()
    add_test(NAME gpu_renderer COMMAND openkai2_gpu_tests)
    set_tests_properties(gpu_renderer PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MESA_SHADER_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/gpu-shader-cache;LIBGL_ALWAYS_SOFTWARE=1")

    add_executable(openkai2_vector_tests "${project_root}/tests/vector_pipeline_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xTest/src/ps2_vu1_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_include_directories(openkai2_vector_tests PRIVATE "${CMAKE_SOURCE_DIR}/ps2xTest/include")
    target_link_libraries(openkai2_vector_tests PRIVATE openkai2_vector_checks)
    if(MSVC)
        target_link_options(openkai2_vector_tests PRIVATE /STACK:8388608 /FORCE:MULTIPLE)
    elseif(MINGW)
        target_link_options(openkai2_vector_tests PRIVATE "-Wl,--stack,8388608")
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_vector_tests)
    add_test(NAME vector_pipelines COMMAND openkai2_vector_tests)
    # These snapshots intentionally preserve the original interpreter's pipeline
    # cycle accounting. The PCSX2 backend has separate ISA and geometry checks.
    set_tests_properties(vector_pipelines PROPERTIES ENVIRONMENT "OPENKAI2_VECTOR_BACKEND=legacy")
    add_library(openkai2_vector_backend_checks STATIC "${project_root}/src/vector/backend_checks.cpp")
    target_compile_features(openkai2_vector_backend_checks PRIVATE cxx_std_20)
    target_include_directories(openkai2_vector_backend_checks PRIVATE "${project_root}/src" "${overlay_dir}/include")
    target_link_libraries(openkai2_vector_backend_checks PUBLIC openkai2_vector_core)
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_vector_backend_checks)
    add_executable(openkai2_vector_backend_tests "${project_root}/tests/vector_backend_tests.cpp")
    target_compile_features(openkai2_vector_backend_tests PRIVATE cxx_std_20)
    target_include_directories(openkai2_vector_backend_tests PRIVATE "${project_root}/src" "${overlay_dir}/include")
    target_link_libraries(openkai2_vector_backend_tests PRIVATE openkai2_vector_backend_checks)
    add_test(NAME vector_native_backend COMMAND openkai2_vector_backend_tests)
    add_library(openkai2_vector_checks STATIC "${project_root}/src/vector_checks.cpp" "${project_root}/src/vector_pipeline_checks.cpp" "${project_root}/src/vector_transfer_checks.cpp" "${project_root}/src/vector_runtime_checks.cpp" "${project_root}/src/vector_hotpath_checks.cpp")
    target_include_directories(openkai2_vector_checks PUBLIC "${project_root}/src")
    target_link_libraries(openkai2_vector_checks PUBLIC ps2_runtime)
    add_executable(openkai2_vector_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/vector_execution.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_vector_bench PRIVATE openkai2_vector_checks)
    if(MSVC)
        target_link_options(openkai2_vector_bench PRIVATE /STACK:8388608 /FORCE:MULTIPLE)
    elseif(MINGW)
        target_link_options(openkai2_vector_bench PRIVATE "-Wl,--stack,8388608")
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_vector_bench)
    add_executable(openkai2_shading_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/raster_shading.cpp")
    target_link_libraries(openkai2_shading_bench PRIVATE openkai2_graphics_checks)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_shading_bench)
    add_executable(openkai2_submission_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/draw_submission.cpp")
    target_include_directories(openkai2_submission_bench PRIVATE "${project_root}/src")
    target_link_libraries(openkai2_submission_bench PRIVATE ps2_runtime)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_submission_bench)
    add_executable(openkai2_depth_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/depth_raster.cpp")
    target_include_directories(openkai2_depth_bench PRIVATE "${project_root}/src")
    target_link_libraries(openkai2_depth_bench PRIVATE ps2_runtime)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_depth_bench)
    add_executable(openkai2_tested_pixels_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/tested_pixels.cpp")
    target_include_directories(openkai2_tested_pixels_bench PRIVATE "${project_root}/src")
    target_link_libraries(openkai2_tested_pixels_bench PRIVATE ps2_runtime)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_tested_pixels_bench)
    add_executable(openkai2_texture_cache_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/texture_cache.cpp")
    target_include_directories(openkai2_texture_cache_bench PRIVATE "${project_root}/src")
    target_link_libraries(openkai2_texture_cache_bench PRIVATE ps2_runtime)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_texture_cache_bench)
    add_executable(openkai2_palette_bench EXCLUDE_FROM_ALL "${project_root}/tools/benchmarks/palette_raster.cpp")
    target_include_directories(openkai2_palette_bench PRIVATE "${project_root}/src")
    target_link_libraries(openkai2_palette_bench PRIVATE ps2_runtime)
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_palette_bench)
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
    target_link_libraries(ps2EntryRunner PRIVATE openkai2_vector_checks openkai2_gpu_checks)
    target_sources(ps2EntryRunner PRIVATE ${generated_sources} "${project_root}/src/main.cpp"
        "${project_root}/src/runtime_checks.cpp" "${project_root}/src/stream_checks.cpp"
        "${project_root}/src/sound_transfer_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/draw_setup_checks.cpp" "${project_root}/src/battle_command_checks.cpp" "${project_root}/src/battle_update_checks.cpp" "${project_root}/src/fight_state_checks.cpp" "${project_root}/src/attack_effect_checks.cpp" "${project_root}/src/impact_effect_checks.cpp" "${project_root}/src/actor_command_checks.cpp" "${project_root}/src/pause_menu_checks.cpp" "${project_root}/src/story_checks.cpp" "${project_root}/src/animation_checks.cpp" "${project_root}/src/aura_checks.cpp" "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp" "${project_root}/src/mpeg_checks.cpp"
        "${project_root}/src/mpeg_decode_checks.cpp")
    # Keep the newly exposed IPU sync routine separate so adding its source
    # preserves the existing translation batches during incremental builds.
    set_source_files_properties("$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output/entry_109390_0x1093f8.cpp"
        TARGET_DIRECTORY ps2EntryRunner PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
    set_source_files_properties("${project_root}/src/main.cpp" "${project_root}/src/runtime_checks.cpp"
        "${project_root}/src/stream_checks.cpp" "${project_root}/src/sound_transfer_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/draw_setup_checks.cpp" "${project_root}/src/battle_command_checks.cpp" "${project_root}/src/battle_update_checks.cpp" "${project_root}/src/fight_state_checks.cpp" "${project_root}/src/attack_effect_checks.cpp" "${project_root}/src/impact_effect_checks.cpp" "${project_root}/src/actor_command_checks.cpp" "${project_root}/src/pause_menu_checks.cpp" "${project_root}/src/story_checks.cpp" "${project_root}/src/animation_checks.cpp" "${project_root}/src/aura_checks.cpp" "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp" "${project_root}/src/mpeg_checks.cpp"
        "${project_root}/src/mpeg_decode_checks.cpp"
        TARGET_DIRECTORY ps2EntryRunner PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
    set_property(SOURCE "${project_root}/src/runtime_checks.cpp" "${project_root}/src/stream_checks.cpp" "${project_root}/src/asset_checks.cpp"
        "${project_root}/src/draw_setup_checks.cpp" "${project_root}/src/battle_command_checks.cpp" "${project_root}/src/battle_update_checks.cpp" "${project_root}/src/fight_state_checks.cpp" "${project_root}/src/attack_effect_checks.cpp" "${project_root}/src/impact_effect_checks.cpp" "${project_root}/src/actor_command_checks.cpp" "${project_root}/src/pause_menu_checks.cpp" "${project_root}/src/story_checks.cpp" "${project_root}/src/animation_checks.cpp" "${project_root}/src/aura_checks.cpp" "${project_root}/src/fade_checks.cpp" "${project_root}/src/movie_checks.cpp"
        TARGET_DIRECTORY ps2EntryRunner APPEND PROPERTY
        INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
    target_include_directories(ps2EntryRunner BEFORE PRIVATE
        "${overlay_dir}/include"
        "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output")
    # Optimize translated game code in development packages as well as release
    # builds. Keep the oversized translation separate to bound compiler
    # memory use; ordinary game and SDK routines retain full optimization.
    set(large_translation "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output/sub_002C2824_0x2c2824.cpp")
    if(MSVC)
        target_compile_options(ps2EntryRunner PRIVATE /O2 /bigobj)
        set_source_files_properties("${large_translation}" TARGET_DIRECTORY ps2EntryRunner
            PROPERTIES COMPILE_OPTIONS /Od SKIP_UNITY_BUILD_INCLUSION TRUE)
    else()
        target_compile_options(ps2EntryRunner PRIVATE -O2 -g0 -fno-strict-aliasing -fwrapv)
        set_source_files_properties("${large_translation}" TARGET_DIRECTORY ps2EntryRunner
            PROPERTIES COMPILE_OPTIONS -O0 SKIP_UNITY_BUILD_INCLUSION TRUE)
    endif()
    file(GLOB_RECURSE runtime_public_headers CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/include/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${runtime_public_headers})
    get_target_property(runner_pch ps2EntryRunner PRECOMPILE_HEADERS)
    if(NOT runner_pch)
        set(runner_pch "")
    endif()
    # Upstream precompiled headers use absolute paths. Include-directory
    # precedence alone cannot redirect them to the reviewed public headers.
    string(REPLACE "${CMAKE_SOURCE_DIR}/ps2xRuntime/include/" "${overlay_dir}/include/"
        runner_pch "${runner_pch}")
    set_property(TARGET ps2EntryRunner PROPERTY PRECOMPILE_HEADERS
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/gcc-portability.h;${runner_pch}")
    list(LENGTH generated_sources generated_count)
    message(STATUS "Attached ${generated_count} generated Budokai 2 sources")
    add_executable(openkai2_cdvd_tests "${project_root}/tests/cdvd_iso_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_include_directories(openkai2_cdvd_tests PRIVATE "${overlay_dir}" "${CMAKE_SOURCE_DIR}/ps2xIOP/src")
    target_compile_definitions(openkai2_cdvd_tests PRIVATE OPENKAI2_SPU2_PCM=1)
    target_link_libraries(openkai2_cdvd_tests PRIVATE ps2_runtime openkai2_disc)
    if(MSVC)
        target_link_options(openkai2_cdvd_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    ps2x_stage_ffmpeg_runtime_dlls(openkai2_cdvd_tests)
    add_test(NAME cdvd_iso_layout COMMAND openkai2_cdvd_tests)
    add_executable(openkai2_fpu_tests "${project_root}/tests/fpu_conversion_tests.cpp")
    target_include_directories(openkai2_fpu_tests BEFORE PRIVATE "${overlay_dir}/include")
    target_link_libraries(openkai2_fpu_tests PRIVATE ps2_runtime)
    add_test(NAME fpu_conversion COMMAND openkai2_fpu_tests)
    add_executable(openkai2_soft_double_tests "${project_root}/tests/soft_double_tests.cpp"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/src/runner/register_functions.cpp")
    target_link_libraries(openkai2_soft_double_tests PRIVATE ps2_runtime)
    if(MSVC)
        target_link_options(openkai2_soft_double_tests PRIVATE /FORCE:MULTIPLE)
    endif()
    add_test(NAME soft_double_math COMMAND openkai2_soft_double_tests)
    if(OPENKAI2_RUNTIME_TESTS_ONLY)
        add_test(NAME launcher_help COMMAND ps2EntryRunner --help)
        set_tests_properties(launcher_help PROPERTIES
            PASS_REGULAR_EXPRESSION "The original game ISO is required")
    endif()
    # Dependency files cannot detect a newly shadowing header in an existing
    # include directory. Invalidate cached objects whenever the public header
    # surface changes, including additions and precompiled-header redirects.
    file(GLOB_RECURSE mirrored_headers "${overlay_dir}/include/*")
    set(header_fingerprints "")
    foreach(header IN LISTS mirrored_headers)
        file(SHA256 "${header}" fingerprint)
        file(RELATIVE_PATH relative "${overlay_dir}/include" "${header}")
        string(APPEND header_fingerprints "${relative}:${fingerprint}\n")
    endforeach()
    string(SHA256 runtime_header_signature "${header_fingerprints}")
    target_compile_definitions(ps2_runtime PUBLIC
        OPENKAI2_RUNTIME_HEADER_SIGNATURE="${runtime_header_signature}")
    # Direct include paths on consumers can precede transitive PUBLIC paths.
    # Mirror all public headers and consistently prefer them on every check.
    get_property(project_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS project_targets)
        if(target MATCHES "^openkai2_")
            target_include_directories(${target} BEFORE PRIVATE "${overlay_dir}/include")
            target_compile_definitions(${target} PRIVATE
                OPENKAI2_RUNTIME_HEADER_SIGNATURE="${runtime_header_signature}")
        endif()
    endforeach()

endfunction()
cmake_language(DEFER CALL openkai2_attach_generated_sources)
