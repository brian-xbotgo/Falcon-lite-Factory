#! /bin/bash
cur_dir=`pwd`
sdk_dir=${SDK_DIR%/}

set -e

project_dir=$sdk_dir

echo ">>>>>>>>>> $sdk_dir/external"

cp -rf $cur_dir/uvc_app/ $project_dir/external/ 
cp -rf $cur_dir/camera_engine_rkaiq/ $project_dir/external/

cp $cur_dir/dnsmasq.conf $project_dir/external/rkwifibt/conf/
cp $cur_dir/wpa_supplicant.conf $project_dir/external/rkwifibt/conf/

echo "<<<<<<<<<< $sdk_dir/external"