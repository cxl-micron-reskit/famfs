# Here is an example of a run_smoke.sh run

```
    [jmg@jmg user]$ ./run_smoke.sh 
    CWD:     /home/jmg/w/famfs/user
    BIN:     /home/jmg/w/famfs/user/debug
    SCRIPTS: /home/jmg/w/famfs/user/scripts
    ./run_smoke.sh: line 23: [!: command not found
    DEVTYPE=
    + sudo mkdir -p /mnt/famfs
    + grep -c famfs /proc/mounts
    0
    + sudo debug/mkfs.famfs -f -k /dev/pmem0
    kill_super: 1
    famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
    famfs_get_device_size: size=8589934592
    devsize: 8589934592
    Famfs superblock killed
    + sudo debug/mkfs.famfs /dev/pmem0
    famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
    famfs_get_device_size: size=8589934592
    devsize: 8589934592
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592

    Log stats:
      # of log entriesi in use: 0 of 25575
      Log size in use:          48
      No allocation errors found
    
    Capacity:
      Device capacity:         8.00G
      Bitmap capacity:         7.99G
      Sum of file sizes:       0.00G
      Allocated space:         0.00G
      Free space:              7.99G
      Space amplification:     -nan
      Percent used:            0.0%
    
    + sudo debug/mkfs.famfs /dev/pmem0
    famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
    famfs_get_device_size: size=8589934592
    devsize: 8589934592
    Device /dev/pmem0 already has a famfs superblock
    + sudo debug/famfs fsck /dev/pmem0
    famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
    famfs_get_device_size: size=8589934592
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 0 of 25575
      Log size in use:          48
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       0
      Allocated bytes:         0
      Free space:              8579448832
      Space amplification:     -nan
      Percent used:            0.0%
    
    + sudo insmod ../kmod/famfs.ko
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + grep famfs /proc/mounts
    /dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
    + grep /dev/pmem0 /proc/mounts
    /dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
    + grep /mnt/famfs /proc/mounts
    /dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo test -f /mnt/famfs/.meta/.superblock
    + sudo test -f /mnt/famfs/.meta/.log
    + sudo debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/test1
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/test1 relpath=test1
    + sudo debug/famfs verify -S 1 -f /mnt/famfs/test1
    filename: /mnt/famfs/test1
    Success: verified 4096 bytes in file /mnt/famfs/test1
    + sudo debug/famfs creat -r -s 4096 -S 2 /mnt/famfs/test2
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/test2 relpath=test2
    + sudo debug/famfs creat -r -s 4096 -S 3 /mnt/famfs/test3
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/test3 relpath=test3
    + sudo debug/famfs verify -S 1 -f /mnt/famfs/test1
    filename: /mnt/famfs/test1
    Success: verified 4096 bytes in file /mnt/famfs/test1
    + sudo debug/famfs verify -S 2 -f /mnt/famfs/test2
    filename: /mnt/famfs/test2
    Success: verified 4096 bytes in file /mnt/famfs/test2
    + sudo debug/famfs verify -S 3 -f /mnt/famfs/test3
    filename: /mnt/famfs/test3
    Success: verified 4096 bytes in file /mnt/famfs/test3
    + sudo debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/test1
    mode: 644
    famfs_file_create: file already exists: /mnt/famfs/test1
    do_famfs_cli_creat: failed to create file /mnt/famfs/test1
    + sudo umount /mnt/famfs
    + grep -c famfs /proc/mounts
    0
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + grep -c famfs /proc/mounts
    1
    + echo 'this logplay should fail because we haven'\''t done mkmeta yet'
    this logplay should fail because we haven't done mkmeta yet
    + sudo debug/famfs logplay /mnt/famfs
    do_famfs_cli_logplay: failed to open log file for filesystem /mnt/famfs
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo test -f /mnt/famfs/.meta/.superblock
    + sudo test -f /mnt/famfs/.meta/.log
    + sudo ls -lR /mnt/famfs
    /mnt/famfs:
    total 0
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 3 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: creating file test1 mode 644
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: creating file test2 mode 644
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: creating file test3 mode 644
    famfs_logplay: processed 3 log entries
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    famfs_file_map_create: failed MAP_CREATE for file /mnt/famfs/.meta/.superblock (errno 17)
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 3 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: File (/mnt/famfs/test1) already exists
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: File (/mnt/famfs/test2) already exists
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: File (/mnt/famfs/test3) already exists
    famfs_logplay: processed 3 log entries
    + sudo debug/famfs verify -S 1 -f /mnt/famfs/test1
    filename: /mnt/famfs/test1
    Success: verified 4096 bytes in file /mnt/famfs/test1
    + sudo debug/famfs verify -S 2 -f /mnt/famfs/test2
    filename: /mnt/famfs/test2
    Success: verified 4096 bytes in file /mnt/famfs/test2
    + sudo debug/famfs verify -S 3 -f /mnt/famfs/test3
    filename: /mnt/famfs/test3
    Success: verified 4096 bytes in file /mnt/famfs/test3
    + sudo debug/famfs fsck /mnt/famfs
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 3 of 25575
      Log size in use:          1032
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       12288
      Allocated bytes:         6291456
      Free space:              8573157376
      Space amplification:     512.00
      Percent used:            0.1%
    
    + set +x
    *************************************************************************************
    Test0 completed successfully
    *************************************************************************************
    DEVTYPE=
    + verify_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo umount /mnt/famfs
    + verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    0
    + grep -c /mnt/famfs /proc/mounts
    0
    + sudo debug/famfs fsck /mnt/famfs
    famfs_fsck: failed to open superblock file
    + sudo debug/famfs fsck /dev/pmem0
    famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
    famfs_get_device_size: size=8589934592
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 3 of 25575
      Log size in use:          1032
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       12288
      Allocated bytes:         6291456
      Free space:              8573157376
      Space amplification:     512.00
      Percent used:            0.1%
    
    + full_mount /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 3 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: creating file test1 mode 644
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: creating file test2 mode 644
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: creating file test3 mode 644
    famfs_logplay: processed 3 log entries
    + sudo debug/famfs fsck /mnt/famfs
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 3 of 25575
      Log size in use:          1032
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       12288
      Allocated bytes:         6291456
      Free space:              8573157376
      Space amplification:     512.00
      Percent used:            0.1%
    
    + sudo debug/famfs fsck /dev/pmem0
    famfs_fsck: error - cannot fsck by device (/dev/pmem0) when mounted
    + verify_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + F=test10
    + sudo debug/famfs creat -r -s 8192 -S 10 /mnt/famfs/test10
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/test10 relpath=test10
    + sudo debug/famfs verify -S 10 -f /mnt/famfs/test10
    filename: /mnt/famfs/test10
    Success: verified 8192 bytes in file /mnt/famfs/test10
    + F=bigtest0
    + sudo debug/famfs creat -v -r -S 42 -s 0x800000 /mnt/famfs/bigtest0
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest0 relpath=bigtest0
    
    bitmap before:
    
       0: 1111111110000000000000000000000000000000000000000000000000000000
    
    bitmap after:
    
       0: 1111111111111000000000000000000000000000000000000000000000000000
    
    Allocated offset: 18874368
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/bigtest0
    filename: /mnt/famfs/bigtest0
    Success: verified 8388608 bytes in file /mnt/famfs/bigtest0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/bigtest0_cp
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest0_cp relpath=bigtest0_cp
    famfs_cp returned 0
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/bigtest0_cp
    filename: /mnt/famfs/bigtest0_cp
    Success: verified 8388608 bytes in file /mnt/famfs/bigtest0_cp
    + sudo debug/famfs mkdir /mnt/famfs/subdir
    famfs_mkdir: creating directory /mnt/famfs/subdir
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir relpath=subdir
    + sudo debug/famfs mkdir /mnt/famfs/subdir
    famfs_mkdir: creating directory /mnt/famfs/subdir
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir relpath=subdir
    famfs_dir_create: failed to mkdir /mnt/famfs/subdir
    famfs_mkdir: failed to mkdir /mnt/famfs/subdir
    + sudo debug/famfs mkdir /mnt/famfs/bigtest0
    famfs_mkdir: creating directory /mnt/famfs/bigtest0
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest0 relpath=bigtest0
    famfs_dir_create: failed to mkdir /mnt/famfs/bigtest0
    famfs_mkdir: failed to mkdir /mnt/famfs/bigtest0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp0
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp0 relpath=subdir/bigtest0_cp0
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp1
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp1 relpath=subdir/bigtest0_cp1
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp2
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp2 relpath=subdir/bigtest0_cp2
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp3
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp3 relpath=subdir/bigtest0_cp3
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp4
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp4 relpath=subdir/bigtest0_cp4
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp5
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp5 relpath=subdir/bigtest0_cp5
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp6
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp6 relpath=subdir/bigtest0_cp6
    famfs_cp returned 0
    + sudo debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp7
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp7 relpath=subdir/bigtest0_cp7
    famfs_cp returned 0
    + sudo debug/famfs cp -v /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp8
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp8 relpath=subdir/bigtest0_cp8
    
    bitmap before:
    
       0: 1111111111111111111111111111111111111111111111111000000000000000
    
    bitmap after:
    
       0: 1111111111111111111111111111111111111111111111111111100000000000
    
    Allocated offset: 102760448
    famfs_cp returned 0
    + sudo debug/famfs cp -v /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp9
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/subdir/bigtest0_cp9 relpath=subdir/bigtest0_cp9
    
    bitmap before:
    
       0: 1111111111111111111111111111111111111111111111111111100000000000
    
    bitmap after:
    
       0: 1111111111111111111111111111111111111111111111111111111110000000
    
    Allocated offset: 111149056
    famfs_cp returned 0
    + sudo debug/famfs logplay -n /mnt/famfs
    dry_run selected
    famfs_logplay: log contains 17 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: 3 file=test10 size=8192
    famfs_logplay: 4 file=bigtest0 size=8388608
    famfs_logplay: 5 file=bigtest0_cp size=8388608
    famfs_logplay: 6 mkdir=subdir
    famfs_logplay: 7 file=subdir/bigtest0_cp0 size=8388608
    famfs_logplay: 8 file=subdir/bigtest0_cp1 size=8388608
    famfs_logplay: 9 file=subdir/bigtest0_cp2 size=8388608
    famfs_logplay: 10 file=subdir/bigtest0_cp3 size=8388608
    famfs_logplay: 11 file=subdir/bigtest0_cp4 size=8388608
    famfs_logplay: 12 file=subdir/bigtest0_cp5 size=8388608
    famfs_logplay: 13 file=subdir/bigtest0_cp6 size=8388608
    famfs_logplay: 14 file=subdir/bigtest0_cp7 size=8388608
    famfs_logplay: 15 file=subdir/bigtest0_cp8 size=8388608
    famfs_logplay: 16 file=subdir/bigtest0_cp9 size=8388608
    famfs_logplay: processed 17 log entries
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp0
    filename: /mnt/famfs/subdir/bigtest0_cp0
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp0
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp1
    filename: /mnt/famfs/subdir/bigtest0_cp1
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp1
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp2
    filename: /mnt/famfs/subdir/bigtest0_cp2
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp2
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp3
    filename: /mnt/famfs/subdir/bigtest0_cp3
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp3
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp4
    filename: /mnt/famfs/subdir/bigtest0_cp4
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp4
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp5
    filename: /mnt/famfs/subdir/bigtest0_cp5
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp5
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp6
    filename: /mnt/famfs/subdir/bigtest0_cp6
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp6
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp7
    filename: /mnt/famfs/subdir/bigtest0_cp7
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp7
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp8
    filename: /mnt/famfs/subdir/bigtest0_cp8
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp8
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp9
    filename: /mnt/famfs/subdir/bigtest0_cp9
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp9
    + sudo umount /mnt/famfs
    + verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    0
    + grep -c /mnt/famfs /proc/mounts
    0
    + full_mount /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 17 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: creating file test1 mode 644
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: creating file test2 mode 644
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: creating file test3 mode 644
    famfs_logplay: 3 file=test10 size=8192
    famfs_logplay: creating file test10 mode 644
    famfs_logplay: 4 file=bigtest0 size=8388608
    famfs_logplay: creating file bigtest0 mode 644
    famfs_logplay: 5 file=bigtest0_cp size=8388608
    famfs_logplay: creating file bigtest0_cp mode 100644
    famfs_logplay: 6 mkdir=subdir
    famfs_logplay: creating directory subdir
    famfs_logplay: 7 file=subdir/bigtest0_cp0 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp0 mode 100644
    famfs_logplay: 8 file=subdir/bigtest0_cp1 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp1 mode 100644
    famfs_logplay: 9 file=subdir/bigtest0_cp2 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp2 mode 100644
    famfs_logplay: 10 file=subdir/bigtest0_cp3 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp3 mode 100644
    famfs_logplay: 11 file=subdir/bigtest0_cp4 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp4 mode 100644
    famfs_logplay: 12 file=subdir/bigtest0_cp5 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp5 mode 100644
    famfs_logplay: 13 file=subdir/bigtest0_cp6 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp6 mode 100644
    famfs_logplay: 14 file=subdir/bigtest0_cp7 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp7 mode 100644
    famfs_logplay: 15 file=subdir/bigtest0_cp8 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp8 mode 100644
    famfs_logplay: 16 file=subdir/bigtest0_cp9 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp9 mode 100644
    famfs_logplay: processed 17 log entries
    + verify_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/bigtest0_cp
    filename: /mnt/famfs/bigtest0_cp
    Success: verified 8388608 bytes in file /mnt/famfs/bigtest0_cp
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp0
    filename: /mnt/famfs/subdir/bigtest0_cp0
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp0
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp1
    filename: /mnt/famfs/subdir/bigtest0_cp1
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp1
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp2
    filename: /mnt/famfs/subdir/bigtest0_cp2
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp2
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp3
    filename: /mnt/famfs/subdir/bigtest0_cp3
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp3
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp4
    filename: /mnt/famfs/subdir/bigtest0_cp4
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp4
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp5
    filename: /mnt/famfs/subdir/bigtest0_cp5
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp5
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp6
    filename: /mnt/famfs/subdir/bigtest0_cp6
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp6
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp7
    filename: /mnt/famfs/subdir/bigtest0_cp7
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp7
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp8
    filename: /mnt/famfs/subdir/bigtest0_cp8
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp8
    + sudo debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp9
    filename: /mnt/famfs/subdir/bigtest0_cp9
    Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp9
    + sudo debug/famfs fsck /mnt/famfs
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 17 of 25575
      Log size in use:          5624
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       100683776
      Allocated bytes:         109051904
      Free space:              8470396928
      Space amplification:     1.08
      Percent used:            1.3%
    
    + sudo debug/famfs fsck -v /mnt/famfs
    famfs_fsck: read 8388608 bytes of log
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 17 of 25575
      Log size in use:          5624
    famfs_build_bitmap: dev_size 8589934592 nbits 4091 bitmap_nbytes 512
    famfs_build_bitmap: superblock and log in bitmap:
       0: 1111100000000000000000000000000000000000000000000000000000000000
    famfs_build_bitmap: file=test1 size=4096
    famfs_build_bitmap: file=test2 size=4096
    famfs_build_bitmap: file=test3 size=4096
    famfs_build_bitmap: file=test10 size=8192
    famfs_build_bitmap: file=bigtest0 size=8388608
    famfs_build_bitmap: file=bigtest0_cp size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp0 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp1 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp2 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp3 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp4 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp5 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp6 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp7 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp8 size=8388608
    famfs_build_bitmap: file=subdir/bigtest0_cp9 size=8388608
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       100683776
      Allocated bytes:         109051904
      Free space:              8470396928
      Space amplification:     1.08
      Percent used:            1.3%
    
    Verbose:
      log_offset:        2097152
      log_len:           8388608
      sizeof(log header) 48
      sizeof(log_entry)  328
      last_log_index:    25574
      full log size:     8388648
      FAMFS_LOG_LEN:     8388608
      Remainder:         -40
      sizeof(struct famfs_file_creation): 304
      sizeof(struct famfs_file_access):   44
    
    + set +x
    *************************************************************************************
    Test1 completed successfully
    *************************************************************************************
    DEVTYPE=
    + verify_mounted /dev/pmem0 /mnt/famfs test2.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test2.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo debug/famfs fsck /mnt/famfs
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 17 of 25575
      Log size in use:          5624
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       100683776
      Allocated bytes:         109051904
      Free space:              8470396928
      Space amplification:     1.08
      Percent used:            1.3%
    
    + NOT_IN_FAMFS=no_leading_slash
    + sudo debug/famfs creat -s 0x400000 no_leading_slash
    mode: 644
    __open_relpath: path no_leading_slash appears not to be in a famfs mount
    famfs_map_superblock_by_path: failed to open superblock file read-only for filesystem no_leading_slash
    do_famfs_cli_creat: failed to create file no_leading_slash
    + LOG=/mnt/famfs/.meta/.log
    + sudo debug/famfs getmap /mnt/famfs/.meta/.log
    File:     /mnt/famfs/.meta/.log
	    size:   8388608
	    extents: 1
		    200000	8388608
    + NOTEXIST=/mnt/famfs/not_exist
    + sudo debug/famfs getmap
    famfs_cli map: no args
    
    Get the allocation map of a file:
        debug/famfs <filename>
    
    + sudo debug/famfs getmap no_leading_slash
    open failed: no_leading_slash rc 0 errno 2
    + F=bigtest
    + SIZE=0x4000000
    + for N in 10 11 12 13 14 15
    + FILE=bigtest10
    + sudo debug/famfs creat -r -S 10 -s 0x4000000 /mnt/famfs/bigtest10
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest10 relpath=bigtest10
    + sudo debug/famfs verify -S 10 -f /mnt/famfs/bigtest10
    filename: /mnt/famfs/bigtest10
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest10
    + for N in 10 11 12 13 14 15
    + FILE=bigtest11
    + sudo debug/famfs creat -r -S 11 -s 0x4000000 /mnt/famfs/bigtest11
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest11 relpath=bigtest11
    + sudo debug/famfs verify -S 11 -f /mnt/famfs/bigtest11
    filename: /mnt/famfs/bigtest11
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest11
    + for N in 10 11 12 13 14 15
    + FILE=bigtest12
    + sudo debug/famfs creat -r -S 12 -s 0x4000000 /mnt/famfs/bigtest12
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest12 relpath=bigtest12
    + sudo debug/famfs verify -S 12 -f /mnt/famfs/bigtest12
    filename: /mnt/famfs/bigtest12
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest12
    + for N in 10 11 12 13 14 15
    + FILE=bigtest13
    + sudo debug/famfs creat -r -S 13 -s 0x4000000 /mnt/famfs/bigtest13
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest13 relpath=bigtest13
    + sudo debug/famfs verify -S 13 -f /mnt/famfs/bigtest13
    filename: /mnt/famfs/bigtest13
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest13
    + for N in 10 11 12 13 14 15
    + FILE=bigtest14
    + sudo debug/famfs creat -r -S 14 -s 0x4000000 /mnt/famfs/bigtest14
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest14 relpath=bigtest14
    + sudo debug/famfs verify -S 14 -f /mnt/famfs/bigtest14
    filename: /mnt/famfs/bigtest14
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest14
    + for N in 10 11 12 13 14 15
    + FILE=bigtest15
    + sudo debug/famfs creat -r -S 15 -s 0x4000000 /mnt/famfs/bigtest15
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/bigtest15 relpath=bigtest15
    + sudo debug/famfs verify -S 15 -f /mnt/famfs/bigtest15
    filename: /mnt/famfs/bigtest15
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest15
    + for N in 10 11 12 13 14 15
    + FILE=bigtest10
    + sudo debug/famfs verify -S 10 -f /mnt/famfs/bigtest10
    filename: /mnt/famfs/bigtest10
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest10
    + for N in 10 11 12 13 14 15
    + FILE=bigtest11
    + sudo debug/famfs verify -S 11 -f /mnt/famfs/bigtest11
    filename: /mnt/famfs/bigtest11
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest11
    + for N in 10 11 12 13 14 15
    + FILE=bigtest12
    + sudo debug/famfs verify -S 12 -f /mnt/famfs/bigtest12
    filename: /mnt/famfs/bigtest12
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest12
    + for N in 10 11 12 13 14 15
    + FILE=bigtest13
    + sudo debug/famfs verify -S 13 -f /mnt/famfs/bigtest13
    filename: /mnt/famfs/bigtest13
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest13
    + for N in 10 11 12 13 14 15
    + FILE=bigtest14
    + sudo debug/famfs verify -S 14 -f /mnt/famfs/bigtest14
    filename: /mnt/famfs/bigtest14
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest14
    + for N in 10 11 12 13 14 15
    + FILE=bigtest15
    + sudo debug/famfs verify -S 15 -f /mnt/famfs/bigtest15
    filename: /mnt/famfs/bigtest15
    Success: verified 67108864 bytes in file /mnt/famfs/bigtest15
    + sudo umount /mnt/famfs
    + verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    0
    + grep -c /mnt/famfs /proc/mounts
    0
    + full_mount /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 23 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: creating file test1 mode 644
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: creating file test2 mode 644
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: creating file test3 mode 644
    famfs_logplay: 3 file=test10 size=8192
    famfs_logplay: creating file test10 mode 644
    famfs_logplay: 4 file=bigtest0 size=8388608
    famfs_logplay: creating file bigtest0 mode 644
    famfs_logplay: 5 file=bigtest0_cp size=8388608
    famfs_logplay: creating file bigtest0_cp mode 100644
    famfs_logplay: 6 mkdir=subdir
    famfs_logplay: creating directory subdir
    famfs_logplay: 7 file=subdir/bigtest0_cp0 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp0 mode 100644
    famfs_logplay: 8 file=subdir/bigtest0_cp1 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp1 mode 100644
    famfs_logplay: 9 file=subdir/bigtest0_cp2 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp2 mode 100644
    famfs_logplay: 10 file=subdir/bigtest0_cp3 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp3 mode 100644
    famfs_logplay: 11 file=subdir/bigtest0_cp4 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp4 mode 100644
    famfs_logplay: 12 file=subdir/bigtest0_cp5 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp5 mode 100644
    famfs_logplay: 13 file=subdir/bigtest0_cp6 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp6 mode 100644
    famfs_logplay: 14 file=subdir/bigtest0_cp7 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp7 mode 100644
    famfs_logplay: 15 file=subdir/bigtest0_cp8 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp8 mode 100644
    famfs_logplay: 16 file=subdir/bigtest0_cp9 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp9 mode 100644
    famfs_logplay: 17 file=bigtest10 size=67108864
    famfs_logplay: creating file bigtest10 mode 644
    famfs_logplay: 18 file=bigtest11 size=67108864
    famfs_logplay: creating file bigtest11 mode 644
    famfs_logplay: 19 file=bigtest12 size=67108864
    famfs_logplay: creating file bigtest12 mode 644
    famfs_logplay: 20 file=bigtest13 size=67108864
    famfs_logplay: creating file bigtest13 mode 644
    famfs_logplay: 21 file=bigtest14 size=67108864
    famfs_logplay: creating file bigtest14 mode 644
    famfs_logplay: 22 file=bigtest15 size=67108864
    famfs_logplay: creating file bigtest15 mode 644
    famfs_logplay: processed 23 log entries
    + verify_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + set +x
    *************************************************************************************
    Test2 completed successfully
    *************************************************************************************
    DEVTYPE=
    + verify_mounted /dev/pmem0 /mnt/famfs test2.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test2.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo cmp /mnt/famfs/bigtest0 /mnt/famfs/bigtest0_cp
    + sudo cmp /mnt/famfs/bigtest10 /mnt/famfs/bigtest11
    /mnt/famfs/bigtest10 /mnt/famfs/bigtest11 differ: byte 1, line 1
    + sudo debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/ddtest
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/ddtest relpath=ddtest
    + sudo debug/famfs verify -S 1 -f /mnt/famfs/test1
    filename: /mnt/famfs/test1
    Success: verified 4096 bytes in file /mnt/famfs/test1
    + sudo dd of=/dev/null if=/mnt/famfs/ddtest bs=4096
    1+0 records in
    1+0 records out
    4096 bytes (4.1 kB, 4.0 KiB) copied, 7.1268e-05 s, 57.5 MB/s
    + sudo umount /mnt/famfs
    + verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    0
    + grep -c /mnt/famfs /proc/mounts
    0
    + full_mount /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
    + sudo debug/famfs mkmeta /dev/pmem0
    mpt: /mnt/famfs
    + sudo debug/famfs logplay /mnt/famfs
    famfs_logplay: log contains 24 entries
    famfs_logplay: 0 file=test1 size=4096
    famfs_logplay: creating file test1 mode 644
    famfs_logplay: 1 file=test2 size=4096
    famfs_logplay: creating file test2 mode 644
    famfs_logplay: 2 file=test3 size=4096
    famfs_logplay: creating file test3 mode 644
    famfs_logplay: 3 file=test10 size=8192
    famfs_logplay: creating file test10 mode 644
    famfs_logplay: 4 file=bigtest0 size=8388608
    famfs_logplay: creating file bigtest0 mode 644
    famfs_logplay: 5 file=bigtest0_cp size=8388608
    famfs_logplay: creating file bigtest0_cp mode 100644
    famfs_logplay: 6 mkdir=subdir
    famfs_logplay: creating directory subdir
    famfs_logplay: 7 file=subdir/bigtest0_cp0 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp0 mode 100644
    famfs_logplay: 8 file=subdir/bigtest0_cp1 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp1 mode 100644
    famfs_logplay: 9 file=subdir/bigtest0_cp2 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp2 mode 100644
    famfs_logplay: 10 file=subdir/bigtest0_cp3 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp3 mode 100644
    famfs_logplay: 11 file=subdir/bigtest0_cp4 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp4 mode 100644
    famfs_logplay: 12 file=subdir/bigtest0_cp5 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp5 mode 100644
    famfs_logplay: 13 file=subdir/bigtest0_cp6 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp6 mode 100644
    famfs_logplay: 14 file=subdir/bigtest0_cp7 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp7 mode 100644
    famfs_logplay: 15 file=subdir/bigtest0_cp8 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp8 mode 100644
    famfs_logplay: 16 file=subdir/bigtest0_cp9 size=8388608
    famfs_logplay: creating file subdir/bigtest0_cp9 mode 100644
    famfs_logplay: 17 file=bigtest10 size=67108864
    famfs_logplay: creating file bigtest10 mode 644
    famfs_logplay: 18 file=bigtest11 size=67108864
    famfs_logplay: creating file bigtest11 mode 644
    famfs_logplay: 19 file=bigtest12 size=67108864
    famfs_logplay: creating file bigtest12 mode 644
    famfs_logplay: 20 file=bigtest13 size=67108864
    famfs_logplay: creating file bigtest13 mode 644
    famfs_logplay: 21 file=bigtest14 size=67108864
    famfs_logplay: creating file bigtest14 mode 644
    famfs_logplay: 22 file=bigtest15 size=67108864
    famfs_logplay: creating file bigtest15 mode 644
    famfs_logplay: 23 file=ddtest size=4096
    famfs_logplay: creating file ddtest mode 644
    famfs_logplay: processed 24 log entries
    + verify_mounted /dev/pmem0 /mnt/famfs test1.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test1.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo debug/famfs fsck /mnt/famfs
    Famfs Superblock:
      Filesystem UUID: 3696e255-512c-4d31-a5f9-b164c83e693f
      System UUID:     00000000-0000-0000-0000-0cc47aaaa734
      sizeof superblock: 136
      num_daxdevs:              1
      primary: /dev/pmem0   8589934592
    
    Log stats:
      # of log entriesi in use: 24 of 25575
      Log size in use:          7920
      No allocation errors found
    
    Capacity:
      Device capacity:         8589934592
      Bitmap capacity:         8579448832
      Sum of file sizes:       503341056
      Allocated bytes:         513802240
      Free space:              8065646592
      Space amplification:     1.02
      Percent used:            6.0%
    
    + set +x
    *************************************************************************************
    Test3 completed successfully
    *************************************************************************************
    DEVTYPE=
    + verify_mounted /dev/pmem0 /mnt/famfs test2.sh
    + DEV=/dev/pmem0
    + MPT=/mnt/famfs
    + MSG=test2.sh
    + grep -c /dev/pmem0 /proc/mounts
    1
    + grep -c /mnt/famfs /proc/mounts
    1
    + sudo debug/famfs creat -s 3g /mnt/famfs/memfile
    mode: 644
    famfs_relpath_from_fullpath: mpt=/mnt/famfs, fullpath=/mnt/famfs/memfile relpath=memfile
    + sudo /home/jmg/w/famfs/user/scripts/../../multichase/multichase -d /mnt/famfs/memfile -m 2900m
    Arena is not devdax, but a regular file
    cheap_create_dax: /mnt/famfs/memfile size is 3221225472
    Allocated cursor_heap size 3221225472
    87.366
    + set +x
    *************************************************************************************
    Test4 (multichase) completed successfully
    *************************************************************************************
```

