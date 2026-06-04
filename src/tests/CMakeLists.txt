set(TEST_SRCS
    BatteryTest.cpp
    CameraTest.cpp
    WifiTest.cpp
    RtcTest.cpp
    KeyTest.cpp
    MicTest.cpp
    SocTest.cpp
    TfCardTest.cpp
    MotorTest.cpp
    HallTest.cpp
    AgingTest.cpp
    SysTest.cpp
)

add_library(factory_tests OBJECT ${TEST_SRCS})

target_include_directories(factory_tests PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)

target_compile_features(factory_tests PUBLIC cxx_std_17)
