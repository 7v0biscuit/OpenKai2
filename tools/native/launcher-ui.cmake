# SPDX-License-Identifier: GPL-3.0-only
function(openkai2_add_launcher_ui project_root enable_tests)
    if(NOT WIN32)
        return()
    endif()
    set(ui_font "${project_root}/third_party/openkai2-display/OpenKai2Display-Regular.ttf")
    set(ui_font_source "${CMAKE_CURRENT_BINARY_DIR}/openkai2_launcher_ui_font.cpp")
    add_custom_command(OUTPUT "${ui_font_source}"
        COMMAND "${CMAKE_COMMAND}" -DFONT=${ui_font} -DOUTPUT=${ui_font_source} -DFONT_SYMBOL=ui_font
            -P "${project_root}/tools/native/embed-font.cmake"
        DEPENDS "${ui_font}" "${project_root}/tools/native/embed-font.cmake" VERBATIM)
    add_library(openkai2_launcher_ui STATIC
        "${project_root}/src/launcher/window.cpp"
        "${project_root}/src/launcher/update_windows.cpp"
        "${project_root}/src/net_socket.cpp"
        "${project_root}/src/netplay.cpp"
        "${project_root}/src/room.cpp"
        "${project_root}/src/relay.cpp"
        "${project_root}/src/mqtt.cpp"
        "${project_root}/src/online.cpp"
        "${project_root}/src/nat.cpp"
        "${project_root}/src/dev_identity.cpp"
        "${project_root}/src/ed25519.cpp"
        "${ui_font_source}"
        )
    target_include_directories(openkai2_launcher_ui PRIVATE "${project_root}/src")
    target_compile_features(openkai2_launcher_ui PUBLIC cxx_std_17)
    target_link_libraries(openkai2_launcher_ui PUBLIC openkai2_launcher_model openkai2_discord_presence openkai2_speedrun
        comctl32 comdlg32 shell32 ole32 uuid gdi32 gdiplus windowscodecs winhttp ws2_32)
    if(MSVC)
        target_compile_options(openkai2_launcher_ui PRIVATE /utf-8)
    endif()
    if(enable_tests)
        add_executable(openkai2_launcher_ui_tests "${project_root}/tests/launcher_ui_tests.cpp")
        target_link_libraries(openkai2_launcher_ui_tests PRIVATE openkai2_launcher_ui dbghelp)
        if(MSVC)
            target_compile_options(openkai2_launcher_ui_tests PRIVATE /utf-8)
        endif()
        add_test(NAME launcher_native_interaction COMMAND openkai2_launcher_ui_tests)
        set_tests_properties(launcher_native_interaction PROPERTIES TIMEOUT 90)
    endif()
endfunction()
