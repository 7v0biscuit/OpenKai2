# SPDX-License-Identifier: GPL-3.0-only
function(openkai2_add_launcher_ui project_root enable_tests)
    if(NOT WIN32)
        return()
    endif()
    set(font "${project_root}/third_party/bangers/Bangers-Regular.ttf")
    set(font_source "${CMAKE_CURRENT_BINARY_DIR}/openkai2_launcher_font.cpp")
    add_custom_command(OUTPUT "${font_source}"
        COMMAND "${CMAKE_COMMAND}" -DFONT=${font} -DOUTPUT=${font_source}
            -P "${project_root}/tools/native/embed-font.cmake"
        DEPENDS "${font}" "${project_root}/tools/native/embed-font.cmake" VERBATIM)
    add_library(openkai2_launcher_ui STATIC
        "${project_root}/src/launcher/window.cpp"
        "${project_root}/src/launcher/update_windows.cpp"
        "${font_source}")
    target_include_directories(openkai2_launcher_ui PRIVATE "${project_root}/src")
    target_compile_features(openkai2_launcher_ui PUBLIC cxx_std_17)
    target_link_libraries(openkai2_launcher_ui PUBLIC openkai2_launcher_model openkai2_discord_presence
        comdlg32 shell32 ole32 uuid gdi32 winhttp)
    if(MSVC)
        target_compile_options(openkai2_launcher_ui PRIVATE /utf-8)
    endif()
    if(enable_tests)
        add_executable(openkai2_launcher_ui_tests "${project_root}/tests/launcher_ui_tests.cpp")
        target_link_libraries(openkai2_launcher_ui_tests PRIVATE openkai2_launcher_ui)
        if(MSVC)
            target_compile_options(openkai2_launcher_ui_tests PRIVATE /utf-8)
        endif()
        add_test(NAME launcher_native_interaction COMMAND openkai2_launcher_ui_tests)
        set_tests_properties(launcher_native_interaction PROPERTIES TIMEOUT 90)
    endif()
endfunction()
