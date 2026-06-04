set(PLATFORM_COMMON_SRCS
    NullPlatform.cpp
    NullEncoder.cpp
    NullCameraDriver.cpp
    NullBatteryDriver.cpp
    NullMotorDriver.cpp
    NullHallDriver.cpp
    NullDisplayDriver.cpp
    SysfsGpioDriver.cpp
    NullRecorder.cpp
    NullWifiManager.cpp
    PlatformFactory.cpp
)

add_library(factory_platform_common STATIC ${PLATFORM_COMMON_SRCS})

target_include_directories(factory_platform_common PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party/nlohmann
)

target_compile_features(factory_platform_common PUBLIC cxx_std_17)
