# SPDX-License-Identifier: GPL-3.0-only
function(openkai2_add_launcher_ui project_root enable_tests)
    if(NOT WIN32)
        return()
    endif()
    set(font "${project_root}/third_party/barlow-condensed/BarlowCondensed-SemiBold.ttf")
    set(font_source "${CMAKE_CURRENT_BINARY_DIR}/openkai2_launcher_font.cpp")
    add_custom_command(OUTPUT "${font_source}"
        COMMAND "${CMAKE_COMMAND}" -DFONT=${font} -DOUTPUT=${font_source}
            -P "${project_root}/tools/native/embed-font.cmake"
        DEPENDS "${font}" "${project_root}/tools/native/embed-font.cmake" VERBATIM)
    set(ui_font "${project_root}/third_party/inter/Inter.ttf")
    set(ui_font_source "${CMAKE_CURRENT_BINARY_DIR}/openkai2_launcher_ui_font.cpp")
    add_custom_command(OUTPUT "${ui_font_source}"
        COMMAND "${CMAKE_COMMAND}" -DFONT=${ui_font} -DOUTPUT=${ui_font_source} -DFONT_SYMBOL=ui_font
            -P "${project_root}/tools/native/embed-font.cmake"
        DEPENDS "${ui_font}" "${project_root}/tools/native/embed-font.cmake" VERBATIM)
    set(banner_dir "${project_root}/src/launcher/banners")
    set(banner_names capsule balls radar scouter budokai_black budokai_board majin_buu babidi majin_vegeta saiyan_pod spirit_bomb nimbus potara)
    set(banner_files "")
    foreach(name IN LISTS banner_names)
        list(APPEND banner_files "${banner_dir}/${name}.jpg")
    endforeach()
    string(REPLACE ";" "," banner_list "${banner_names}")
    set(banner_source "${CMAKE_CURRENT_BINARY_DIR}/openkai2_launcher_banners.cpp")
    add_custom_command(OUTPUT "${banner_source}"
        COMMAND "${CMAKE_COMMAND}" -DDIR=${banner_dir} -DNAMES=${banner_list} -DOUTPUT=${banner_source}
            -P "${project_root}/tools/native/embed-banners.cmake"
        DEPENDS ${banner_files} "${project_root}/tools/native/embed-banners.cmake" VERBATIM)
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
        "${font_source}"
        "${ui_font_source}"
        "${banner_source}")
    target_include_directories(openkai2_launcher_ui PRIVATE "${project_root}/src")
    target_compile_features(openkai2_launcher_ui PUBLIC cxx_std_17)
    target_link_libraries(openkai2_launcher_ui PUBLIC openkai2_launcher_model openkai2_discord_presence openkai2_speedrun
        comctl32 comdlg32 shell32 ole32 uuid gdi32 windowscodecs winhttp ws2_32)
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
