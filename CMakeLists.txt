cmake_minimum_required(VERSION 3.10)
project(Falcon_Air_Factory)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# =================================================================
# 0. LVGL library (built from source at ../lvgl_app/lvgl/)
# =================================================================
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/../lvgl_app/lvgl)

file(GLOB_RECURSE LVGL_SRCS ${LVGL_DIR}/src/*.c)
# Exclude demo files (not present in this tree, but guard anyway)
list(FILTER LVGL_SRCS EXCLUDE REGEX "lv_demo_.*\\.c$")

add_library(lvgl STATIC ${LVGL_SRCS})

target_include_directories(lvgl PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/lvgl    # our lv_conf.h (takes precedence, enables more fonts)
    ${LVGL_DIR}                              # lvgl.h, lv_api_map.h
    ${LVGL_DIR}/src                          # src/lv_conf_internal.h, etc.
    ${LVGL_DIR}/src/drivers                  # lv_drv_conf.h (for fbdev)
)


# =================================================================
# 1. Falcon_Air_Factory - Main executable
# =================================================================
set(FACTORY_SOURCES
    main.cpp

    # Core
    src/core/TestEngine.cpp
    src/core/AsyncTaskQueue.cpp

    # HAL - C++
    src/hal/GpioController.cpp
    src/hal/I2cController.cpp
    src/hal/MotorController.cpp
    src/hal/FactoryDisplay.cpp

    # HAL - C (motor + hall drivers)
    src/hal/MotorHal.c
    src/hal/HallSwitch.c

    # Test modules
    src/tests/BatteryTest.cpp
    src/tests/CameraTest.cpp
    src/tests/WifiTest.cpp
    src/tests/RtcTest.cpp
    src/tests/KeyTest.cpp
    src/tests/MicTest.cpp
    src/tests/SocTest.cpp
    src/tests/TfCardTest.cpp
    src/tests/MotorTest.cpp
    src/tests/HallTest.cpp
    src/tests/AgingTest.cpp
    src/tests/SysTest.cpp
)

add_executable(Falcon_Air_Factory ${FACTORY_SOURCES})

target_include_directories(Falcon_Air_Factory PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/tmi8152
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/include
    ${CMAKE_SOURCE_DIR}/third_party/lvgl       # lv_conf.h
    ${LVGL_DIR}                                  # lvgl.h
    ${CMAKE_SOURCE_DIR}/src/hal                  # for log.h used by MotorHal.c and HallSwitch.c
)

target_link_directories(Falcon_Air_Factory PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/lib
)

target_link_libraries(Falcon_Air_Factory lvgl mosquitto)

# =================================================================
# 2. soc_test - Standalone SoC test client
# =================================================================
add_executable(soc_test
    tools/soc_test.cpp
)

target_include_directories(soc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/include
)

target_link_directories(soc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/lib
)

target_link_libraries(soc_test mosquitto)

# =================================================================
# 3. factory_test_runner - Host-side factory test tool
# =================================================================
add_executable(factory_test_runner
    tools/factory_test_runner.cpp
)

target_include_directories(factory_test_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/include
)

target_link_directories(factory_test_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/lib
)

target_link_libraries(factory_test_runner mosquitto)
