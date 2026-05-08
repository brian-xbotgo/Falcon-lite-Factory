#!/bin/bash
set -e

if [ -z "${SDK_DIR}" ]; then
    echo "ERROR: SDK_DIR not set yet!"
    exit 1
fi

echo "SDK_DIR: ${SDK_DIR}"
echo "toolchain: $(pwd)/toolchain.cmake"

rm -rf build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
cmake --build build -j$(nproc)


#  运行示例：                                                                                                                                                                                
#  $ export SDK_DIR=/path/to/sdk                                                                                                                                                             
#  $ ./build.sh 
