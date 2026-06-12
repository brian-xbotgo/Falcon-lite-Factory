cmake_minimum_required(VERSION 3.16)
project(FalconFactory LANGUAGES C CXX)

if(NOT DEFINED FW_ROOT)
    set(FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

if(NOT TARGET factory_core)
    set(FACTORY_ENABLE_BLE ON CACHE BOOL "Enable BLE support" FORCE)
    add_compile_definitions(HAVE_MOSQUITTO)
    add_subdirectory(${FW_ROOT}/src ${CMAKE_BINARY_DIR}/fw_src)
    add_subdirectory(${FW_ROOT}/third_party/lvgl ${CMAKE_BINARY_DIR}/third_party/lvgl)
endif()

set(FALCON_DRIVER_SRCS
    FalconRuntime.cpp
    FalconPlatform.cpp
    drivers/RkMppEncoder.cpp
    drivers/Gc4663CameraDriver.cpp
    drivers/Om70x0xBatteryDriver.cpp
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
    ${FW_ROOT}/third_party/lvgl
    ${FW_ROOT}/third_party/lvgl/src
    ${FW_ROOT}/third_party/lvgl/src/drivers
)

target_link_libraries(platform_falcon PUBLIC
    factory_platform_common
    factory_control
    rkwifibt
    rockchip_mpp
    lvgl
)

target_link_directories(platform_falcon PUBLIC ${FW_ROOT}/third_party/mosquitto/lib)
target_link_libraries(platform_falcon PUBLIC mosquitto)

add_executable(factory_test ${FW_ROOT}/src/main.cpp)
target_link_libraries(factory_test
    factory_core
    factory_common
    factory_config
    factory_control
    factory_tests
    factory_ble
    -Wl,--whole-archive
    platform_falcon
    -Wl,--no-whole-archive
)
