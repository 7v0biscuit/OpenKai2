function(openkai2_attach_generated_sources)
    if(NOT TARGET ps2EntryRunner)
        message(FATAL_ERROR "PS2Recomp runtime runner target is unavailable")
    endif()
    get_target_property(runner_sources ps2EntryRunner SOURCES)
    list(FILTER runner_sources EXCLUDE REGEX "src/runner/register_functions\\.cpp$")
    set_property(TARGET ps2EntryRunner PROPERTY SOURCES "${runner_sources}")
    file(GLOB generated_sources CONFIGURE_DEPENDS
        "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output/*.cpp")
    if(NOT generated_sources)
        message(FATAL_ERROR "No generated Budokai 2 sources; run ps2_recomp first")
    endif()
    target_sources(ps2EntryRunner PRIVATE ${generated_sources})
    target_include_directories(ps2EntryRunner BEFORE PRIVATE
        "$ENV{OPENKAI2_WORK_DIR}/budokai2-native/output")
    # Initial execution diagnosis: keep the very large generated translation
    # inexpensive to compile while retaining the upstream runtime's optimization.
    target_compile_options(ps2EntryRunner PRIVATE -O0 -g0)
    target_precompile_headers(ps2EntryRunner PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/gcc-portability.h")
    list(LENGTH generated_sources generated_count)
    message(STATUS "Attached ${generated_count} generated Budokai 2 sources")
endfunction()
cmake_language(DEFER CALL openkai2_attach_generated_sources)
