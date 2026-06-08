cmake_minimum_required(VERSION 3.16)
project(BaseFactory LANGUAGES C CXX)

if(NOT DEFINED FW_ROOT)
    set(FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

if(NOT TARGET factory_core)
    set(FACTORY_ENABLE_BLE OFF CACHE BOOL "Enable BLE support" FORCE)
    add_subdirectory(${FW_ROOT}/src ${CMAKE_BINARY_DIR}/fw_src)
endif()

# Base platform has no additional drivers; it uses Base* from platforms/common.

add_executable(factory_test ${FW_ROOT}/src/main.cpp)
target_link_libraries(factory_test
    factory_core
    factory_common
    factory_config
    factory_control
    factory_tests
    factory_platform_common
)
