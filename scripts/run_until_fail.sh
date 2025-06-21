#!/bin/bash

PROGRAM="$1"

if [[ -z "$PROGRAM" ]]; then
    echo "Usage: $0 /path/to/program [args...]"
    exit 1
fi

echo "Starting loop: will run '$PROGRAM' until it fails..."

while true; do
    "$@"
    EXIT_CODE=$?

    if [[ $EXIT_CODE -ne 0 ]]; then
        echo "Program exited with non-zero status: $EXIT_CODE"
        break
    fi
done

echo "Done."
