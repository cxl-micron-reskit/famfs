#!/usr/bin/env bash

#set -x

CWD=$(pwd)

# Defaults running from the directory where this file lives
SCRIPTS=$CWD/scripts
BIN=$CWD/release
RUNTIME=60
TWO_MB=$((2*1024*1024))
NJOBS=$(nproc)
FILES_PER_THRD=8

if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$UMOUNT" ]; then
    UMOUNT="umount"
fi

# Override defaults as needed
while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
        (-f|--fiopath)
            FIO_PATH=$1
            shift;
            ;;
        (-b|--bin)
            BIN=$1
            shift
            ;;
        (-r|--runtime)
            RUNTIME=$1
            shift;
            ;;
        (-s|--size)
            TOTAL_SIZE=$1
            shift;
            ;;
        (-j|--jobs)
            NJOBS=$1
            shift;
            ;;
	(-p|--path)
	    FILES_DIR=$1
	    shift;
	    ;;
	(-v|--verbose)
	    VERBOSE=" -vv "
	    ;;
        *)
            echo "Unrecognized command line arg: $flag"
	    exit 0
            ;;

    esac
done

echo "CWD:      $CWD"
echo "BIN:      $BIN"
echo "SCRIPTS:  $SCRIPTS"
echo "PATH:     $PATH"
echo "RUNTIME:  $RUNTIME"

echo "NJOBS:    $NJOBS"
echo "TOTAL_SIZE:     $TOTAL_SIZE"

if [ -z "$FILES_DIR" ]; then
    echo "--path <files_dir> is required"
    exit -1
fi

source $SCRIPTS/test_funcs.sh

FILE_CT=$((NJOBS * FILES_PER_THRD))

# If size is not specified, use the full available capacity
if [[ $TOTAL_SIZE -eq 0 ]]; then
    echo "stress_fio.sh: must specify -s <total_size>"
    exit -1
fi

# Famfs files take up 2MB at minmum
MAX_FILES=$((TOTAL_SIZE / TWO_MB))
echo "MAX_FILES for this test size = $MAX_FILES"
if [[ $FILE_CT -gt $MAX_FILES ]]; then
    FILES_PER_THRD=$((MAX_FILES / NJOBS))
    FILE_CT=$((NJOBS * FILES_PER_THRD))

    echo "Total files $((NJOBS * FILES_PER_THRD)) is more than max files possible $MAX_FILES for given size $TOTAL_SIZE"
    echo "Updating Total files to $FILE_CT and Files per thread to $FILES_PER_THRD"
fi

echo "TOTAL_SIZE: $TOTAL_SIZE"
echo "FILE_CT: $FILE_CT"
# Individual file size
F_SIZE=$((TOTAL_SIZE / FILE_CT))
FSIZE_2M_A=$F_SIZE

echo "F_SIZE $F_SIZE"
echo "FSIZE_2M_A $FSIZE_2M_A"

# 2MiB aligned file size in bytes
if [[ $F_SIZE -gt $TWO_MB ]]; then
    FSIZE_2M_A=$(((F_SIZE / TWO_MB) * TWO_MB))
fi

# Just to track if any space is wasted due to alignment
WASTAGE=$(((TOTAL_SIZE - (FILE_CT  * FSIZE_2M_A)) / 1024))

# Individual file size in MiB
FSIZE_MB=$((FSIZE_2M_A / (1024*1024)))

if [[ $FSIZE_2M_A == 0 ]]; then
    echo "Error: Cannot create $FSIZE_MB size files"
    exit 0
fi

NAME="$FILES_PER_THRD-$FSIZE_MB-MB-files-per-thread"

#echo "Creating directory $MPT/$PID"
NFILES=$((NJOBS * FILES_PER_THRD))
echo "Creating $NFILES files, each sized $FSIZE_2M_A in directory $FILES_DIR"
sudo $BIN/famfs mkdir $FILES_DIR

for (( cpu = 0; cpu < $NJOBS; cpu++ ))
do
    for (( fnum = 0; fnum < $FILES_PER_THRD; fnum++ ))
    do
        fname="$NAME.$cpu.$fnum"
        #echo "Creating famfs file $FILES_DIR/$fname"
        sudo $BIN/famfs creat $VERBOSE -s $FSIZE_2M_A $FILES_DIR/$fname \
	    || fail "failed to create file $fname"
    done
done

echo ""
echo "Size used             : $TOTAL_SIZE"
echo "Number of jobs        : $NJOBS"
echo "Files per job         : $FILES_PER_THRD"
echo "Total files           : $FILE_CT"
echo "File size (Bytes)     : $F_SIZE"
echo "2 MiB aligned fsize   : $FSIZE_2M_A"
echo "File size (MiB)       : $FSIZE_MB MB"
echo "Runtime               : $RUNTIME"
echo "Size of all files     : $((FSIZE_2M_A * FILE_CT))"
echo "Files location        : $FILES_DIR"
echo ""

FIO_CMD="fio -name=$NAME \
    --nrfiles=$FILES_PER_THRD \
    --bs=2M \
    --group_reporting=1 \
    --alloc-size=1048576 \
    --filesize=$FSIZE_2M_A \
    --readwrite=write \
    --fallocate=none \
    --numjobs=$NJOBS \
    --create_on_open=0 \
    --directory=$FILES_DIR \
    --time_based \
    --runtime=$RUNTIME"
echo "Running fio for $RUNTIME seconds"
echo "$FIO_CMD"
echo ""

FIO_OUTPUT=$(sudo $FIO_CMD | tee /dev/fd/2; exit ${PIPESTATUS[0]}) || exit 1

#echo "$FIO_OUTPUT"

echo "*************************************************************************"
echo "                    stress_fio.sh completed successfully"
echo "*************************************************************************"

exit 0
