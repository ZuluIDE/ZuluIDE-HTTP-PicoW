# This builds an universal binary that works for both PicoW and Pico2W.
# During development you can use individual builds by defining PICO_BOARD
# for the root folder CMakeLists.txt:
#
# cmake -B build -DPICO_BOARD=pico2_w; cmake --build build
#
# To build the universal binary, run cmake in the universal_binary directory:
#
# cd universal_binary
# cmake -B build; cmake --build build

cmake_minimum_required(VERSION 3.25)
project(zuluide_http_picow_universal C CXX ASM)
include(ExternalProject)

ExternalProject_Add(platform_picow
            SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/platform_picow
            CMAKE_ARGS
                "-DPICO_BOARD=pico_w"
            BUILD_ALWAYS 1 # force dependency checking
            INSTALL_COMMAND ""
)

ExternalProject_Add(platform_pico2w
            SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/platform_pico2w
            CMAKE_ARGS
                "-DPICO_BOARD=pico2_w"
            BUILD_ALWAYS 1 # force dependency checking
            INSTALL_COMMAND ""
)


# Pico bootrom allows approx 500 ms after last block before it disconnects.
# Depending on operating system, if we put whole RP2040 image first, the
# computer may give an error message even though flashing succeeds.
# Instead, arrange the combined UF2 like this:
# [All except last block of RP2040] [All of RP2350] [Last block of RP2040]

set(RP2040_UF2 ${CMAKE_CURRENT_BINARY_DIR}/platform_picow/zuluide_http_picow.uf2)
set(RP2040_UF2_PART1 ${CMAKE_CURRENT_BINARY_DIR}/platform_picow/zuluide_http_picow_part1.uf2)
set(RP2040_UF2_PART2 ${CMAKE_CURRENT_BINARY_DIR}/platform_picow/zuluide_http_picow_part2.uf2)
set(SPLIT_UF2_PY ${CMAKE_CURRENT_SOURCE_DIR}/split_uf2.py)

add_custom_command(
    OUTPUT ${RP2040_UF2_PART1} ${RP2040_UF2_PART2}
    COMMAND python ${SPLIT_UF2_PY} ${RP2040_UF2} ${RP2040_UF2_PART1} ${RP2040_UF2_PART2}
    DEPENDS ${RP2040_UF2} ${SPLIT_UF2_PY}
    COMMENT "Splitting ${RP2040_UF2}"
    VERBATIM
)

set(PLATFORM_UF2s
    ${RP2040_UF2_PART1}
    ${CMAKE_CURRENT_BINARY_DIR}/platform_pico2w/zuluide_http_picow.uf2
    ${RP2040_UF2_PART2}
)
set(COMBINED_UF2 zuluide_http_picow.uf2)

add_custom_command(
    OUTPUT ${COMBINED_UF2}
    COMMAND ${CMAKE_COMMAND} -E cat ${PLATFORM_UF2s} > ${COMBINED_UF2}
    DEPENDS ${PLATFORM_UF2s}
)

add_custom_target(platform_combined ALL
    DEPENDS ${COMBINED_UF2}
)
