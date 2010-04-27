#! /bin/sh

set -e

create_src() {
    n=$1
    i=0
    while [ $i -lt $n ]; do
        file=$i.cc
        cat <<EOF >$file
#include <algorithm>
#include <deque>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
int var$i;
EOF
        i=$(($i + 1))
    done
}

compile() {
    n=$1
    compiler=$2
    i=0
    objdir=objs
    rm -rf $objdir
    mkdir -p $objdir
    while [ $i -lt $n ]; do
        echo "  $compiler -c -O2 $i.cc -o $objdir/$i.o"
        $compiler -c $i.cc -O2 -o $objdir/$i.o
        i=$(($i + 1))
    done
}

now() {
    perl -e 'use Time::HiRes qw(time); print time'
}

elapsed() {
    perl -e 'use Time::HiRes qw(time); printf("%.3f\n", time - $ARGV[0])' $1
}

###############################################################################

if [ -n "$CXX" ]; then
    cxx="$CXX"
else
    cxx=c++
fi

ccache=../ccache
tmpdir=tmpdir.$$
CCACHE_DIR=.ccache
export CCACHE_DIR
CCACHE_NODIRECT=1
export CCACHE_NODIRECT

rm -rf $tmpdir
mkdir $tmpdir
cd $tmpdir

n=30
create_src $n

echo "Without ccache:"
t0=$(now)
compile $n $cxx
echo "Time: $(elapsed $t0)"

echo "With ccache, no direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
echo "Time: $(elapsed $t0)"

echo "With ccache, no direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
echo "Time: $(elapsed $t0)"

unset CCACHE_NODIRECT
rm -rf $CCACHE_DIR

echo "With ccache, direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
echo "Time: $(elapsed $t0)"

echo "With ccache, direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
echo "Time: $(elapsed $t0)"
