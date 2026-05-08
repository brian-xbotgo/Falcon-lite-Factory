set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED SDK_DIR)
    set(SDK_DIR "$ENV{SDK_DIR}")
endif()
if(NOT SDK_DIR)
    message(FATAL_ERROR "SDK_DIR not set. Usage: cmake -DSDK_DIR=/path/to/rv1126b_sdk ..")
endif()

set(TOOLCHAIN_DIR "${SDK_DIR}/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-g++")

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
