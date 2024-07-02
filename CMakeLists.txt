cmake_minimum_required(VERSION 3.25)

set(PICO_BOARD pico_w)
include(pico_sdk_import.cmake)

project(zuluide_http_picow C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(zuluide_http_picow)

target_sources(zuluide_http_picow PRIVATE src/main.cpp src/url_decode.cpp src/ZuluControlI2CClient.cpp)

#pico_enable_stdio_uart(zuluide_http_picow ENABLED)
pico_enable_stdio_usb(zuluide_http_picow ENABLED)

add_dependencies(zuluide_http_picow pico_i2c_slave)

pico_add_extra_outputs(zuluide_http_picow)

target_compile_options(zuluide_http_picow PRIVATE -Wall)

target_include_directories(zuluide_http_picow PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        )

file(READ ${CMAKE_CURRENT_LIST_DIR}/resources/control.html FILE_CONTENT)
file(READ ${CMAKE_CURRENT_LIST_DIR}/resources/control.js CONTROL_JS_CONTENT)
file(READ ${CMAKE_CURRENT_LIST_DIR}/resources/control2.js CONTROL_2_JS_CONTENT)
file(READ ${CMAKE_CURRENT_LIST_DIR}/resources/style.css STYLE_CSS_CONTENT)

configure_file(${CMAKE_CURRENT_LIST_DIR}/src/index_html.in ${CMAKE_CURRENT_LIST_DIR}/src/index_html.h @ONLY ESCAPE_QUOTES)


target_compile_definitions(zuluide_http_picow PRIVATE
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        WIFI_SSID=\"${WIFI_SSID}\"
        )

target_link_libraries(zuluide_http_picow
        pico_i2c_slave
        pico_stdlib
        pico_lwip_http
        pico_cyw43_arch_lwip_threadsafe_background)
