set(CORE_SRCS
    core/ModuleRegistry.cpp
    core/AsyncTaskQueue.cpp
    core/TestEngine.cpp
    core/TestResult.cpp
    core/TestContext.cpp
)

add_library(factory_core STATIC ${CORE_SRCS})
target_include_directories(factory_core PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
if(FACTORY_ENABLE_BLE)
    target_include_directories(factory_core PUBLIC
        ${FW_ROOT}/third_party/mosquitto/include
    )
endif()
target_compile_features(factory_core PUBLIC cxx_std_17)

set(COMMON_SRCS
    common/ShellUtils.cpp
)

add_library(factory_common STATIC ${COMMON_SRCS})
target_include_directories(factory_common PUBLIC
    ${FW_ROOT}/include
)
target_compile_features(factory_common PUBLIC cxx_std_17)

set(CONFIG_SRCS
    config/PlatformConfig.cpp
)

add_library(factory_config STATIC ${CONFIG_SRCS})
target_include_directories(factory_config PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
target_compile_features(factory_config PUBLIC cxx_std_17)

set(PLATFORM_COMMON_SRCS
    platforms/common/BaseEncoder.cpp
    platforms/common/BaseCameraDriver.cpp
    platforms/common/BaseBatteryDriver.cpp
    platforms/common/BaseMotorDriver.cpp
    platforms/common/BaseHallDriver.cpp
    platforms/common/BaseDisplayDriver.cpp
    platforms/common/SysfsGpioDriver.cpp
    platforms/common/BaseRecorder.cpp
    platforms/common/BaseWifiManager.cpp
    platforms/common/PlatformFactory.cpp
)

add_library(factory_platform_common STATIC ${PLATFORM_COMMON_SRCS})
target_include_directories(factory_platform_common PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
target_compile_features(factory_platform_common PUBLIC cxx_std_17)

set(CONTROL_SRCS
    control/RecorderController.cpp
    control/I2cController.cpp
    control/V4l2Recorder.cpp
    control/GpioController.cpp
    control/StatusLedController.cpp
    control/AudioCapture.cpp
    control/G711Encoder.cpp
    control/Mp4Muxer.cpp
)

add_library(factory_control STATIC ${CONTROL_SRCS})
target_include_directories(factory_control PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
target_link_libraries(factory_control PUBLIC factory_platform_common)
target_compile_features(factory_control PUBLIC cxx_std_17)

set(TEST_SRCS
    tests/BatteryTest.cpp
    tests/CameraTest.cpp
    tests/WifiTest.cpp
    tests/RtcTest.cpp
    tests/KeyTest.cpp
    tests/MicTest.cpp
    tests/SocTest.cpp
    tests/TfCardTest.cpp
    tests/MotorTest.cpp
    tests/HallTest.cpp
    tests/AgingTest.cpp
    tests/SysTest.cpp
)

add_library(factory_tests OBJECT ${TEST_SRCS})
target_include_directories(factory_tests PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
target_compile_features(factory_tests PUBLIC cxx_std_17)

if(FACTORY_ENABLE_BLE)
    set(BLE_SRCS
        ble/BleAdvertiser.cpp
        ble/BleDeviceInfo.cpp
        ble/BleGattServer.cpp
        ble/BleMqttBridge.cpp
        ble/BleUtility.cpp
    )

    add_library(factory_ble STATIC ${BLE_SRCS})
    target_include_directories(factory_ble PUBLIC
        ${FW_ROOT}/include
        ${FW_ROOT}/third_party
        ${FW_ROOT}/third_party/mosquitto/include
        ${CMAKE_SYSROOT}/usr/include/glib-2.0
        ${CMAKE_SYSROOT}/usr/lib/glib-2.0/include
    )
    target_compile_features(factory_ble PUBLIC cxx_std_17)
    target_link_libraries(factory_ble PUBLIC
        mosquitto
        glib-2.0
        gio-2.0
        pthread
    )
endif()
