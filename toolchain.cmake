set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_DIR "$ENV{HOME}/RV1126B_LINUX6.1/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-g++")

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
