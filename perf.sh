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
    local n=$1
    local compiler="$2"
    local i=0
    local objdir=objs
    rm -rf $objdir
    mkdir -p $objdir
    while [ $i -lt $n ]; do
        echo -n .
        $compiler -c $i.cc -O2 -o $objdir/$i.o
        i=$(($i + 1))
    done
    echo
}

now() {
    perl -e 'use Time::HiRes qw(time); print time'
}

elapsed() {
    perl -e 'use Time::HiRes qw(time); printf("%.3f\n", time - $ARGV[0])' $1
}

stat() {
    local desc="$1"
    local time=$2
    local ref_time=$3
    local perc=$(perl -e "print 100 * $time / $ref_time")
    local factor=$(perl -e "print $ref_time / $time")
    printf "%-43s %5.2f s (%6.2f %%) (%5.2f x)\n" "$desc:" $time $perc $factor
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

if [ "$#" -gt 0 ]; then
    n=$1
else
    n=30
fi
create_src $n

echo "Without ccache:"
t0=$(now)
compile $n $cxx
t_wo=$(elapsed $t0)

echo "With ccache, no direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
t_p_m=$(elapsed $t0)

echo "With ccache, no direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
t_p_h=$(elapsed $t0)

unset CCACHE_NODIRECT
rm -rf $CCACHE_DIR

echo "With ccache, direct, cache miss:"
t0=$(now)
compile $n "$ccache $cxx"
t_d_m=$(elapsed $t0)

echo "With ccache, direct, cache hit:"
t0=$(now)
compile $n "$ccache $cxx"
t_d_h=$(elapsed $t0)

echo
stat "Without ccache" $t_wo $t_wo
stat "With ccache, preprocessor mode, cache miss" $t_p_m $t_wo
stat "With ccache, preprocessor mode, cache hit" $t_p_h $t_wo
stat "With ccache, direct mode, cache miss" $t_d_m $t_wo
stat "With ccache, direct mode, cache hit" $t_d_h $t_wo
