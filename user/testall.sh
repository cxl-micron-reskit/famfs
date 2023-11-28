#!/usr/bin/env bash

source test_funcs.sh

./test0.sh       || exit
sleep 4
./test1.sh       || exit
sleep 4
./test2.sh       || exit
sleep 4
./test3.sh       || exit
sleep 4
./test_errors.sh || exit
sleep 4
./teardown.sh
