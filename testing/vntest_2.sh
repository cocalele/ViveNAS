#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh
#set -xv

BUILD_ROOT=$DIR/..
export PATH=$BUILD_ROOT/bin:$PATH

FIFO_IN=/tmp/test_in
AOF_SRC_DAT=/tmp/test_src.dat
AOF_OUT_DAT=/tmp/test_out.dat

MNT_DIR=./mnt
TEST_FILE=$MNT_DIR/test.txt

function cleanup {
  kill $HELPER_PID
  kill $SLP_PID
#  rm  -f /tmp/pfhead  $FIFO_IN $AOF_SRC_DAT $AOF_OUT_DAT
}
trap cleanup EXIT


#dd if=/dev/urandom bs=1M count=10 of=$AOF_SRC_DAT
rm -f $FIFO_IN
assert mkfifo $FIFO_IN
sleep 1000 > $FIFO_IN &
SLP_PID=$!

vn_test_helper $TEST_FILE  < $FIFO_IN &
HELPER_PID=$!
sleep 2

check_proc $HELPER_PID
#1 test write
echo "w Helloworld" > $FIFO_IN
#2 test sync
echo "s" > $FIFO_IN
sleep 1
#helper will exit on fail
check_proc $HELPER_PID


date > $MNT_DIR/A1.txt
assert_equal "$?" "0"
date >> $MNT_DIR/A1.txt
assert_equal "$?" "0"

echo "q" > $FIFO_IN
assert wait $HELPER_PID
kill $SLP_PID

info "========Test OK!=========="
