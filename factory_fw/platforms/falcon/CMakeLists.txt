cmake_minimum_required(VERSION 3.16)
project(FalconFactory LANGUAGES C CXX)

if(NOT DEFINED FW_ROOT)
    set(FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

if(NOT TARGET factory_core)
    add_subdirectory(${FW_ROOT}/src/core          ${CMAKE_BINARY_DIR}/fw_core)
    add_subdirectory(${FW_ROOT}/src/common        ${CMAKE_BINARY_DIR}/fw_common)
    add_subdirectory(${FW_ROOT}/src/config        ${CMAKE_BINARY_DIR}/fw_config)
    add_subdirectory(${FW_ROOT}/src/hal           ${CMAKE_BINARY_DIR}/fw_hal)
    add_subdirectory(${FW_ROOT}/src/tests         ${CMAKE_BINARY_DIR}/fw_tests)
    add_subdirectory(${FW_ROOT}/src/platforms/common ${CMAKE_BINARY_DIR}/fw_platform_common)
endif()

set(FALCON_DRIVER_SRCS
    FalconPlatform.cpp
    drivers/RkMppEncoder.cpp
    drivers/Gc4663CameraDriver.cpp
    drivers/Cw221xBatteryDriver.cpp
    drivers/Tmi8152MotorDriver.cpp
    drivers/HallSwitchDriver.cpp
    drivers/LvglDisplayDriver.cpp
    drivers/Rk3576Recorder.cpp
    drivers/Rk3576WifiManager.cpp
)
add_library(platform_falcon STATIC ${FALCON_DRIVER_SRCS})
target_include_directories(platform_falcon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)

# 声明式依赖：让 CMake 自动处理链接顺序
# platform_falcon 引用了 SysfsGpioDriver（定义在 factory_platform_common）
target_link_libraries(platform_falcon PUBLIC factory_platform_common)

add_executable(factory_test ${FW_ROOT}/src/main.cpp)
target_link_libraries(factory_test
    factory_core
    factory_common
    factory_config
    factory_hal
    factory_tests
    -Wl,--whole-archive
    platform_falcon
    -Wl,--no-whole-archive
)
