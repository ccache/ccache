#!/bin/sh -ex

echo "Warning: Source package support is rather experimental\n"

mkdir -p build_package_source_dir_test
cd build_package_source_dir_test
rm -rf *
cmake ..
cmake --build . --target package_source

# get out of git directory
# Unfortunately this random name will prevent ccache from caching results...
# ToDo: use '../../temp' instead?
tmp_dir=$(mktemp -d)

tar -xzf ccache-src.tar.gz -C $tmp_dir
cd $tmp_dir

cmake .
cmake --build . -- -j4
ctest --output-on-failure -j4

rm -rf $tmp_dir

echo "\n\nsource package is fine and can be used!\n\n"

