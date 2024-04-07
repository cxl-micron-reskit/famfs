#!/usr/bin/env bash

#set -x

CWD=$(pwd)

# Defaults running from the directory where this file lives
DEV="/dev/dax0.0"
SCRIPTS=$CWD/scripts
BIN=$CWD/release
RUNTIME=60
TWO_MB=$((2*1024*1024))
NJOBS=0

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
        (-d|--device)
            DEV=$1
            shift;
            ;;
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
            SIZE=$1
            shift;
            ;;
        (-j|--jobs)
            NJOBS=$1
            shift;
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

source $SCRIPTS/test_funcs.sh

# If NJOBS is not specified, use all cpus
if [[ $NJOBS -eq 0 ]]; then
    NPROC=$(cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l)
    NJOBS=$((NPROC - 1))
fi

# Get the device node name, ex: dax0.0 from /dev/dax0.0
DEVICE=$(echo $DEV | awk -F '/' '{print $3}')

# Get device size in bytes from sysfs, this works only for dax devices.
DEV_SIZE_BYTES=$(cat /sys/bus/dax/devices/$DEVICE/size)
FILES_PER_THRD=8
TOTAL_FILE_COUNT=$((NJOBS * FILES_PER_THRD))

# Reserve 32MiB for metadata and log (for now, this is more than needed)
RESRV_SPACE=$((32*1024*1024))

# Usable device capacity
DEV_SIZE=$((DEV_SIZE_BYTES - RESRV_SPACE))

if [[ $SIZE -gt $DEV_SIZE ]]; then
    echo "Error: Test size $SIZE greater than device size $DEV_SIZE_BYTES"
    exit 0
fi

# If size is not specified, use the full available capacity
if [[ $SIZE -eq 0 ]]; then
    echo "User specified size is 0, using full usable device capacity"
    SIZE=$DEV_SIZE
fi

# Famfs files take up 2MB at minmum
MAX_FILES=$((SIZE / TWO_MB))
echo "MAX_FILES for this test size = $MAX_FILES"
if [[ $TOTAL_FILE_COUNT -gt $MAX_FILES ]]; then
    FILES_PER_THRD=$((MAX_FILES / NJOBS))
    TOTAL_FILE_COUNT=$((NJOBS * FILES_PER_THRD))

    echo "Total files $((NJOBS * FILES_PER_THRD)) is more than max files possible $MAX_FILES for given size $SIZE"
    echo "Updating Total files to $TOTAL_FILE_COUNT and Files per thread to $FILES_PER_THRD"
fi

# Individual file size
F_SIZE=$((SIZE / TOTAL_FILE_COUNT))
FSIZE_2M_A=$F_SIZE

# 2MiB aligned file size in bytes
if [[ $F_SIZE -gt $TWO_MB ]]; then
    FSIZE_2M_A=$(((F_SIZE / TWO_MB) * TWO_MB))
fi

# Just to track if any space is wasted due to alignment
WASTAGE=$(((SIZE - (TOTAL_FILE_COUNT  * FSIZE_2M_A)) / 1024))

# Individual file size in MiB
FSIZE_MB=$((FSIZE_2M_A / (1024*1024)))

if [[ $FSIZE_2M_A == 0 ]]; then
    echo "Error: Cannot create $FSIZE_MB size files"
    exit 0
fi

NAME="$FILES_PER_THRD-$FSIZE_MB-MB-files-per-thread"

PID=$$
FILES_DIR=$MPT/test_$PID

#echo "Creating directory $MPT/$PID"
echo "Creating directory $FILES_DIR"
sudo $BIN/famfs mkdir $FILES_DIR

for (( cpu = 0; cpu < $NJOBS; cpu++ ))
do
    for (( fnum = 0; fnum < $FILES_PER_THRD; fnum++ ))
    do
        fname="$NAME.$cpu.$fnum"
        echo "Creating famfs file $FILES_DIR/$fname"
        sudo $BIN/famfs creat -v -s $FSIZE_2M_A $FILES_DIR/$fname || fail "failed to create file $fname"
    done
done

echo "Running fio for $RUNTIME seconds"
echo "fio --name=$NAME  --nrfiles=$FILES_PER_THRD --bs=2M --group_reporting=1 "
echo "    --alloc-size=1048576 --filesize=$FSIZE_2M_A --readwrite=write --fallocate=none "
echo "    --numjobs=$NJOBS --create_on_open=0 --directory=$FILES_DIR --time_based --runtime=$RUNTIME"
echo ""

#FIO_OUTPUT=$(sudo /usr/local/Repos/Fio/fio/fio -name=$NAME \
FIO_OUTPUT=$(sudo $FIO_PATH -name=$NAME \
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
    --runtime=$RUNTIME | tee /dev/fd/2; exit ${PIPESTATUS[0]}) || exit 1
    #--runtime=$RUNTIME) || fail "fio failed"

#echo "$FIO_OUTPUT"

echo ""
echo "Full device size      : $DEV_SIZE_BYTES"
echo "Usable device size    : $DEV_SIZE"
echo "User give size        : $SIZE"
echo "Number of jobs        : $NJOBS"
echo "Files per job         : $FILES_PER_THRD"
echo "Total files           : $TOTAL_FILE_COUNT"
echo "File size (Bytes)     : $F_SIZE"
echo "2 MiB aligned fsize   : $FSIZE_2M_A"
echo "File size (MiB)       : $FSIZE_MB MB"
echo "Runtime               : $RUNTIME"
echo "Size of all files     : $((FSIZE_2M_A * TOTAL_FILE_COUNT))"
echo "Files location        : $FILES_DIR"
echo ""

echo "*************************************************************************************"
echo "                    stress_fio.sh completed successfully"
echo "*************************************************************************************"

exit 0
