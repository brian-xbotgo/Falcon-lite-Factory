set(CONFIG_SRCS
    PlatformConfig.cpp
)

add_library(factory_config STATIC ${CONFIG_SRCS})

target_include_directories(factory_config PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)

target_compile_features(factory_config PUBLIC cxx_std_17)
