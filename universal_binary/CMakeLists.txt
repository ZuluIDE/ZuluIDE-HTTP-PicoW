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

set(COMBINED_UF2 zuluide_http_picow.uf2)
set(PLATFORM_UF2s ${CMAKE_CURRENT_BINARY_DIR}/platform_picow/zuluide_http_picow.uf2 ${CMAKE_CURRENT_BINARY_DIR}/platform_pico2w/zuluide_http_picow.uf2)

add_custom_command(
    OUTPUT ${COMBINED_UF2}
    COMMAND ${CMAKE_COMMAND} -E cat ${PLATFORM_UF2s} > ${COMBINED_UF2}
    DEPENDS ${PLATFORM_UF2s}
)

add_custom_target(platform_combined ALL
    DEPENDS ${COMBINED_UF2}
)
