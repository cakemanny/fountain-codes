#!/bin/bash

echo
echo = = = = = = Small File Test = = = = = =
echo
echo This tests whether the client-server pair can accurately transfer a small
echo file

if [[ -x ../client && -x ../server ]]; then
    echo Client and Server binaries found
else
    echo Binaries not found
    exit 1
fi

cat > testfile1.txt <<EOF
old pond . . .
a frog leaps in
water’s sound

the first cold shower
even the monkey seems to want
a little coat of straw

the wind of Mt. Fuji
I've brought on my fan!
a gift from Edo

how many gallons
of Edo's rain did you drink?
cuckoo
EOF

rm -f testfile2.txt

../server testfile1.txt 2>/dev/null &
SERVID_PID=$!

echo Waiting to server to set itself up...
sleep 3
echo Starting client

../client --output=testfile2.txt 2>/dev/null

kill $SERVID_PID

if [ ! -r testfile2.txt ]; then
    echo testfile2.txt was not created
    exit 1
fi

if [ -z "$(cmp testfile1.txt testfile2.txt)" ]; then
    echo
    echo :::TEST PASSED::: The files match
    echo
else
    echo testfile1.txt and testfile2.txt do not match
    exit 1
fi

rm -f testfile{1,2}.txt

