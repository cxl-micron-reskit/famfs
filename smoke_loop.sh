#!/usr/bin/env bash

source scripts/test_funcs.sh

if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi

smoke_args="$1"

counter=0
while [ true ]; do
    MPT=$MPT ./run_smoke.sh "${smoke_args}" --nosleep
    rc="$?"
    echo "==================================================================="
    echo "smoke_loop.sh: loop $counter (rc=$rc) ($(date))"
    echo "==================================================================="
    ((counter++))
    sleep 3
    if (( rc > 0 )); then
	echo "smoke_loop: stopping on error"
	exit -1
    fi
done
