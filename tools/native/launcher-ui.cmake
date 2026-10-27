# SPDX-License-Identifier: GPL-3.0-only
function(openkai2_add_launcher_ui project_root enable_tests)
    if(NOT WIN32)
        return()
    endif()
    add_library(openkai2_launcher_ui STATIC
        "${project_root}/src/launcher/window.cpp"
        "${project_root}/src/launcher/update_windows.cpp")
    target_compile_features(openkai2_launcher_ui PUBLIC cxx_std_17)
    target_link_libraries(openkai2_launcher_ui PUBLIC openkai2_launcher_model
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
