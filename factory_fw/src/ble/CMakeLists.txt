set(BLE_SRCS
    BleAdvertiser.cpp
    BleDeviceInfo.cpp
    BleGattServer.cpp
    BleMqttBridge.cpp
    BleUtility.cpp
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

# Dependencies: mosquitto (vendored + SDK sysroot), glib/gio (SDK sysroot), pthread
target_link_libraries(factory_ble PUBLIC
    mosquitto
    glib-2.0
    gio-2.0
    pthread
)
