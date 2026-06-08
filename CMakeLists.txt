cmake_minimum_required(VERSION 3.16)
project(FactoryFirmware LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

set(TARGET_PLATFORM "falcon" CACHE STRING "Target platform")
if(TARGET_PLATFORM STREQUAL "falcon")
    set(FACTORY_ENABLE_BLE ON CACHE BOOL "Enable BLE support" FORCE)
    add_compile_definitions(HAVE_MOSQUITTO)
endif()

add_subdirectory(src)
add_subdirectory(third_party/lvgl)
add_subdirectory("platforms/${TARGET_PLATFORM}")
