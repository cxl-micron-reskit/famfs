#!/bin/env bash

if [ ! -f $1 ]; then
    echo "Must supply a valgrind log file to parse as command line arg"
    exit -1
fi

logfile="$1"

count=$(grep -c "ERROR SUMMARY" $1)
if (($count <= 0)); then
    echo ":== Error: No valgrind output found in file $1"
    exit -1;
fi

count=$(grep "ERROR SUMMARY" $1 | grep -cv "0 errors from 0 contexts")
#echo "$count"
if (($count == 0)); then
    echo ":== Congratulations: no errors found by Valgrind"
    exit 0
fi

echo ":== Error: Valgrind found errors..."
echo

lines=$(grep "ERROR SUMMARY" $1 | \
	    grep -v "0 errors from 0 contexts" )

echo "$lines" | while IFS= read -r line; do
    # Perform operations on each line
    echo "Processing line: $line"
    id=$(echo $line | awk '{print $1}')
    #echo "id: $id"
    grep "$id" "$logfile"
done

exit $count

