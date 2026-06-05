cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE LVGL_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(FILTER LVGL_SRCS EXCLUDE REGEX "lv_demo_.*\\.c$")

add_library(lvgl STATIC ${LVGL_SRCS})

target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers
)
