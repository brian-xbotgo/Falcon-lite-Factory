# configure and build and install to the 'prefix' directory.
# how to use: run it in the current directory
#     CROSS_COMPILE="/media/disk1/rv1106_uvc_live/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf" \
#     ./build.sh

# setting
filename=mosquitto-2.0.20.tar.gz
xf_dir=mosquitto-2.0.20
filelink=https://mosquitto.org/files/source/${filename}

SYSROOT=$SDK_DIR/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host/aarch64-buildroot-linux-gnu/sysroot
OPENSSL_INC=${SYSROOT}/usr/include
OPENSSL_LIB=${SYSROOT}/usr/lib

if [ "$1" == "clean" ]; then
    rm -rf $xf_dir out
    echo "clean done"
    exit 0
fi

if [ -z "${CROSS_COMPILE}" ]; then
    CROSS_COMPILE="$SDK_DIR/buildroot/output/rockchip_rv1126b_ipc_64_evb1_v10/rockchip_rv1126b_ipc/host/bin/aarch64-buildroot-linux-gnu"
else
    CROSS_COMPILE=${CROSS_COMPILE%-}
fi

#14 threads to download
if [ "$1" == "download" ]; then
    if [ -f "$filename" ]; then
        echo "$filename already exists"
        exit 0
    fi

    if [ ! -f "$filename" ]; then
        axel -k -a -n 14 $filelink
    fi

    if [ ! -f "$filename" ]; then
        echo "Failed downloading $filename from $filelink"
        exit -1
    else
        echo "$filename download successful"
    fi

    exit 0
fi

export CC="-gcc"
export GCC="-gcc"
export CXX="-g++"
export CPP="-cpp"
export STRIP="-strip"

export CFLAGS="--sysroot=${SYSROOT} -I${OPENSSL_INC} -fPIC -DWITH_TLS -DOPENSSL_API_COMPAT=0x10100000L"
export LDFLAGS="--sysroot=${SYSROOT} -L${OPENSSL_LIB} -lcrypto -lssl -lpthread -lrt"
export CPPFLAGS="--sysroot=${SYSROOT} -I${OPENSSL_INC}"

out_dir=out
export OUT_DIR="$(pwd)/$out_dir"
echo =========================  $OUT_DIR ====================

# generic code

parent_dir=`pwd`/..

[ ! -f "$filename" ] && axel -k -a -n 14 $filelink
[ ! -f "$filename" ] && \
    (echo "Failed downloading $filename from $filelink"; exit -1; )

[ ! -d "$xf_dir" ] && { tar xf $filename ; cd $xf_dir; patch -p0 < ../config_mk.patch; \
    patch -p0 < ../makefile_set.patch; cd -;}

[ ! -d "$out_dir" ] && mkdir $out_dir

# enter to the directory
pushd $xf_dir
#make  -j10 install
make PREFIX=${OUT_DIR} CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}" CPPFLAGS="${CPPFLAGS}" -j10 install
popd
