#!/bin/sh -ex

echo "Warning: Binary package support is rather experimental\n"

mkdir -p build_package_dir_test
cd build_package_dir_test
rm -rf *
cmake ..
cmake --build . --target package

# get out of git directory just to be sure
tmp_dir=$(mktemp -d)

tar -xzf ccache-binary.tar.gz -C $tmp_dir

CCACHE=$tmp_dir/ccache-binary/bin/ccache ../test/run

rm -rf $tmp_dir

echo "\n\nbinary package is fine and can be used!\n\n"
