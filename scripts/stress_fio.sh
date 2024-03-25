#!/usr/bin/env bash

#set -x

CWD=$(pwd)

# Defaults running from the directory where this file lives
DEV="/dev/dax0.0"
SCRIPTS=$CWD/scripts
BIN=$CWD/release
RUNTIME=60

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
        (-s|--scripts)
            SCRIPTS=$1
            shift;
            ;;
        (-r|--runtime)
            RUNTIME=$1
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

NPROC=$(cat /proc/cpuinfo | awk '/^processor/{print $3}' | wc -l)
NJOBS=$((NPROC - 1))

# Get the device node name, ex: dax0.0 from /dev/dax0.0
DEVICE=$(echo $DEV | awk -F '/' '{print $3}')
# Get device size in bytes from sysfs, this works only for dax devices.
DEV_SIZE_BYTES=$(cat /sys/bus/dax/devices/$DEVICE/size)

FILES_PER_THRD=8
TOTAL_FILE_COUNT=$((NJOBS * FILES_PER_THRD))
TWO_MB=$((2*1024*1024))
# Reserve 32MiB for metadata and log (for now, this is more than needed)
RESRV_SPACE=$((32*1024*1024))
# Usable device capacity
DEV_SIZE=$((DEV_SIZE_BYTES - RESRV_SPACE))
# Individual file size
F_SIZE=$((DEV_SIZE / TOTAL_FILE_COUNT))

# 2MiB aligned file size
FILE_SIZE=$(((F_SIZE / TWO_MB) * TWO_MB))

# Just to track if any space is wasted due to alignment
WASTAGE=$(((DEV_SIZE - (TOTAL_FILE_COUNT  * FILE_SIZE)) / 1024))
# Individual file size in MiB
FSIZE_MB=$((FILE_SIZE / (1024*1024)))

NAME="$FILES_PER_THRD-$FSIZE_MB-MB-files-per-thread"

echo "Full device size  : $DEV_SIZE_BYTES"
echo "Usable device size: $DEV_SIZE"
echo "Number of jobs    : $NJOBS"
echo "Files per job     : $FILES_PER_THRD"
echo "File size (MiB)   : $FSIZE_MB"
echo "Runtime           : $RUNTIME" 

#echo "last_proc=$last_proc"

if [[ $FSIZE_MB == 0 ]]; then
    echo "stress_fio.sh: Cannot create $FSIZE_MB size files "
    exit 0
fi

for (( cpu = 0; cpu < $NJOBS; cpu++ ))
do
    for (( fnum = 0; fnum < $FILES_PER_THRD; fnum++ ))
    do
        fname="$NAME.$cpu.$fnum"
        echo "Creating famfs file $MPT/$fname"
        sudo $BIN/famfs creat -v -s $FILE_SIZE $MPT/$fname || fail "failed to create file $fname"
    done
done

#sudo chown jmg:jmg $MPT/ten*

echo "Running fio for $RUNTIME seconds"
echo "fio --name=$NAME  --nrfiles=$FILES_PER_THRD --bs=2M --group_reporting=1 "
echo "    --alloc-size=1048576 --filesize=$FILE_SIZE --readwrite=write --fallocate=none "
echo "    --numjobs=$NJOBS --create_on_open=0 --directory=$MPT --time_based --runtime=$RUNTIME"

#FIO_OUTPUT=$(sudo /usr/local/Repos/Fio/fio/fio -name=$NAME \
FIO_OUTPUT=$(sudo $FIO_PATH -name=$NAME \
    --nrfiles=$FILES_PER_THRD \
    --bs=2M \
    --group_reporting=1 \
    --alloc-size=1048576 \
    --filesize=$FILE_SIZE \
    --readwrite=write \
    --fallocate=none \
    --numjobs=$NJOBS \
    --create_on_open=0 \
    --directory=$MPT \
    --time_based \
    --runtime=$RUNTIME | tee /dev/fd/2; exit ${PIPESTATUS[0]}) || exit 1
    #--runtime=$RUNTIME) || fail "fio failed"

#echo "$FIO_OUTPUT"

echo "*************************************************************************************"
echo "                    stress_fio.sh completed successfully"
echo "*************************************************************************************"

exit 0
