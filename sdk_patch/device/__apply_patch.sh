#! /bin/bash
cur_dir=`pwd`
sdk_dir=${SDK_DIR%/}

set -e

project_dir=$sdk_dir

echo ">>>>>>>>>> $sdk_dir/device"
cp $cur_dir/rockchip/rv1126b/* $project_dir/device/rockchip/rv1126b/ -rf
cp $cur_dir/rockchip/common/ $project_dir/device/rockchip/ -rf
echo "<<<<<<<<<< $sdk_dir/device"
