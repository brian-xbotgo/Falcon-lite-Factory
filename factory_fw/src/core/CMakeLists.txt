set(CORE_SRCS
    ModuleRegistry.cpp
    AsyncTaskQueue.cpp
    TestEngine.cpp
    TestResult.cpp
    TestContext.cpp
)

add_library(factory_core STATIC ${CORE_SRCS})

target_include_directories(factory_core PUBLIC
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party/nlohmann
)

target_compile_features(factory_core PUBLIC cxx_std_17)
