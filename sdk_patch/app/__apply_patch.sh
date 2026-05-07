#! /bin/bash
cur_dir=`pwd`
sdk_dir=${SDK_DIR%/}

set -e

project_dir=$sdk_dir

echo ">>>>>>>>>> $sdk_dir/app"

cp -rf $cur_dir/rkipc/ $project_dir/app/ 

echo "<<<<<<<<<< $sdk_dir/app"
