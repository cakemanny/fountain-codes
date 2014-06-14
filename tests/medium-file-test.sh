#!/bin/bash

echo
echo = = = = = = Medium File Test = = = = = =
echo
echo This tests whether the client-server pair can accurately transfer a
echo medium file and checks for deterministic completion time

if [[ -x ../client && -x ../server ]]; then
    echo Client and Server binaries found
else
    echo Binaries not found
    exit 1
fi

testfile=scalaz-cheatsheet.pdf
input=testfile1.pdf
output=testfile2.pdf

if [ ! -r $testfile ]; then
    echo Medium sized pdf not found
    exit 1
fi

# Copy in case the program buggers our pdf
cp $testfile $input

rm -f $output

../server --blocksize=1024 $input 2>/dev/null &
SERVID_PID=$!

echo Waiting for server to setup...
sleep 2
echo Starting client

starttime=$(date +"%s")

../client --output=$output 2>/dev/null

endtime=$(date +"%s")
totaltime=$(( $endtime - $starttime ))

kill $SERVID_PID

if [ ! -r $output ]; then
    echo $output was not created
    exit 1
fi

exitvalue=1
echo
if [ -z "$(cmp $input $output)" ]; then
    if [[ $totaltime > 4 ]]; then
        echo '    ':::FAILED::: Files matched but this took longer than 4 seconds. Total time was $totaltime
    else
        echo '    ':::TEST PASSED::: The files match
        exitvalue=0
    fi
else
    echo '    ':::FAILED::: $input and $output do not match
fi
echo

rm -f $input $output

exit $exitvalue

