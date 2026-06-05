set(HAL_SRCS
    RecorderController.cpp
    I2cController.cpp
    V4l2Recorder.cpp
    GpioController.cpp
)

add_library(factory_hal STATIC ${HAL_SRCS})

target_include_directories(factory_hal PUBLIC
    ${FW_ROOT}/include
)

target_link_libraries(factory_hal PUBLIC factory_platform_common)

target_compile_features(factory_hal PUBLIC cxx_std_17)
