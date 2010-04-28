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

stat() {
    desc=$1
    time=$2
    ref_time=$3
    perc=$(perl -e "print 100 * $time / $ref_time")
    factor=$(perl -e "print $ref_time / $time")
    printf "%-36s %.3f s (%6.2f %%) (%5.2f x)\n" "$desc:" $time $perc $factor
}

###############################################################################

if [ -n "$CXX" ]; then
    cxx="$CXX"
else
    cxx=/usr/bin/c++
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
t_wo=$(elapsed $t0)
echo "Time: $t_wo"

echo "With ccache, no direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
t_p_m=$(elapsed $t0)
echo "Time: $t_p_m"

echo "With ccache, no direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
t_p_h=$(elapsed $t0)
echo "Time: $t_p_h"

unset CCACHE_NODIRECT
rm -rf $CCACHE_DIR

echo "With ccache, direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
t_d_m=$(elapsed $t0)
echo "Time: $t_d_m"

echo "With ccache, direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
t_d_h=$(elapsed $t0)
echo "Time: $t_d_h"

echo
stat "Without ccache" $t_wo $t_wo
stat "With ccache, no direct, cache miss" $t_p_m $t_wo
stat "With ccache, no direct, cache hit" $t_p_h $t_wo
stat "With ccache, direct, cache miss" $t_d_m $t_wo
stat "With ccache, direct, cache hit" $t_d_h $t_wo
