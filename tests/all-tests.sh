#!/bin/bash

# Really want to be in this directory
cd "$(dirname "$0")"


./small-file-test.sh
# Don't really need to run medium

[[ ! -x ../client ]] && { echo client missing; exit 1; }
[[ ! -x ../server ]] && { echo server missing; exit 1; }


echo
echo Various size tests:

testfile=scalaz-cheatsheet.pdf
input=testfile1.pdf
output=testfile2.pdf
[[ ! -r $testfile ]] && { echo $testfile not found; exit 1; }

perform_test() {
    echo testfile=$testfile, bs=$1, ss=$2
    cp $testfile $input
    rm -f $output
    ../server --blocksize=$1 --sectionsize=$2 $input 2>server-$1-$2.log &
    server_pid=$!
    sleep 0.5
    starttime=$(python3 -c 'import time; print(time.time())')
    ../client --output=$output 2>client-$1-$2.log
    endtime=$(python3 -c 'import time; print(time.time())')
    kill $server_pid
    if [[ ! -r $output ]]; then
        echo $output was not created
    fi

    python3 -c "print('time taken: %6f' % ($endtime - $starttime))"

    if [[ -z "$(cmp $input $output)" ]]; then
        echo "    ::: PASSED ::: The files match"
        rm -f server-$1-$2.log client-$1-$2.log
    else
        echo "    ::: FAILED ::: $input and $output do not match"
    fi
    rm -f $input $output
}

for bs in 128 256 512 1024; do
    for ss in 128 256 512; do
        perform_test $bs $ss
    done
done


