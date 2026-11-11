# SPDX-License-Identifier: GPL-3.0-only
# Writes the launcher's display font as a byte array. Usage:
#   cmake -DFONT=<ttf> -DOUTPUT=<cpp> -P embed-font.cmake
if(NOT DEFINED FONT_SYMBOL)
    set(FONT_SYMBOL display_font)
endif()
file(READ "${FONT}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
set(row "")
foreach(i RANGE 1 24)
    string(APPEND row "0x[0-9a-f][0-9a-f],")
endforeach()
string(REGEX REPLACE "(${row})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp"
    "// Generated from ${FONT}\n#include \"launcher/display_font.h\"\n"
    "namespace openkai2::launcher {\nconst unsigned char ${FONT_SYMBOL}[] = {\n${bytes}\n};\n"
    "const std::size_t ${FONT_SYMBOL}_size = ${size};\n}\n")
configure_file("${OUTPUT}.tmp" "${OUTPUT}" COPYONLY)
