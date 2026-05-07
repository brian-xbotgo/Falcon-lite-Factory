#! /bin/bash
cur_dir=`pwd`
sdk_dir=${SDK_DIR%/}

set -e

project_dir=$sdk_dir

echo ">>>>>>>>>> $sdk_dir/buildroot"

cp -rf $cur_dir/configs/ $project_dir/buildroot/ 
cp -rf $cur_dir/dl/ $project_dir/buildroot/ 
cp -rf $cur_dir/package/ $project_dir/buildroot/
if [ -f $project_dir/buildroot/board/rockchip/rv1126b/fs-overlay-ipc/etc/init.d/S99rkipc ]; then 
    rm -rf $project_dir/buildroot/board/rockchip/rv1126b/fs-overlay-ipc/etc/init.d/S99rkipc
    echo "delete $project_dir/buildroot/board/rockchip/rv1126b/fs-overlay-ipc/etc/init.d/S99rkipc"
fi

echo "<<<<<<<<<< $sdk_dir/buildroot"
