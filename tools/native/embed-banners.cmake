# SPDX-License-Identifier: GPL-3.0-only
string(REPLACE "," ";" names "${NAMES}")
set(row "")
foreach(i RANGE 1 24)
    string(APPEND row "0x[0-9a-f][0-9a-f],")
endforeach()
set(body "")
set(table "")
foreach(name IN LISTS names)
    file(READ "${DIR}/${name}.jpg" hex HEX)
    string(LENGTH "${hex}" digits)
    math(EXPR size "${digits} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(${row})" "\\1\n" bytes "${bytes}")
    string(APPEND body "const unsigned char banner_${name}[] = {\n${bytes}\n};\n")
    string(APPEND table "    {banner_${name}, ${size}},\n")
endforeach()
file(WRITE "${OUTPUT}.tmp"
    "// Generated from ${DIR}\n#include \"launcher/banners.h\"\n"
    "namespace openkai2::launcher {\nnamespace {\n${body}}\n"
    "const BannerPicture banner_pictures[] = {\n${table}};\n}\n")
configure_file("${OUTPUT}.tmp" "${OUTPUT}" COPYONLY)
