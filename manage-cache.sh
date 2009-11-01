#!/bin/bash
#
# 2004-05-12 lars@gustaebel.de

CCACHE_DIR=${CCACHE_DIR:-$HOME/.ccache}

echo "Do you want to compress or decompress the ccache in $CCACHE_DIR?"
read -p "Type c or d: " mode

if [ "$mode" != "c" ] && [ "$mode" != "d" ]
then
    exit 1
fi

is_compressed() {
    test "$(head -c 2 $1)" = $'\x1f\x8b'
    return $?
}

tmpfile=$(mktemp)

for dir in 0 1 2 3 4 5 6 7 8 9 a b c d e f
do
    # process ccache subdir
    echo -n "$dir "

    # find cache files
    find $CCACHE_DIR/$dir -type f -name '*-*' |
    sort > $tmpfile

    oldsize=$(cat $CCACHE_DIR/$dir/stats | cut -d ' ' -f 13)
    newsize=0

    while read file
    do
        # empty files will be ignored since compressing
        # them makes them bigger
        test $(stat -c %s $file) -eq 0 && continue

        if [ $mode = c ]
        then
            if ! is_compressed $file
            then
                gzip $file
                mv $file.gz $file
            fi
        else
            if is_compressed $file
            then
                mv $file $file.gz
                gzip -d $file.gz
            fi
        fi

        # calculate new size statistic for this subdir
        let newsize=$newsize+$(stat -c "%B*%b" $file)/1024
    done < $tmpfile

    # update statistic file
    read -a numbers < $CCACHE_DIR/$dir/stats
    numbers[12]=$newsize
    echo "${numbers[*]} " > $CCACHE_DIR/$dir/stats
done
echo

# clean up
rm $tmpfile

