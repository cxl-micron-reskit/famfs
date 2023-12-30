#!/usr/bin/env bash

source scripts/test_funcs.sh
counter=0
while [ true ]; do
    ./scripts/teardown.sh
    ./run_smoke.sh
    rc="$?"
    echo "============================================================================"
    echo "smoke_loop.sh: loop $counter (rc=$rc)"
    echo "============================================================================"
    ((counter++))
    sleep 3
    if (( rc > 0 )); then
	echo "smoke_loop: stopping on error"
	exit -1
    fi
done
