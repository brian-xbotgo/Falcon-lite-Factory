set(COMMON_SRCS
    ShellUtils.cpp
)

add_library(factory_common STATIC ${COMMON_SRCS})

target_include_directories(factory_common PUBLIC
    ${FW_ROOT}/include
)

target_compile_features(factory_common PUBLIC cxx_std_17)
