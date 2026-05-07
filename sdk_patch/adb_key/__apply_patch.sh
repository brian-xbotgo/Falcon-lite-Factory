#! /bin/bash
cur_dir=`pwd`
set -e

project_dir=~/.android/

mkdir -p ~/.android/

echo ">>>>>>>>>> ~/.android/"

cd $project_dir

cp -rf $cur_dir/adbkey* $project_dir/

echo "<<<<<<<<<< ~/.android/"
