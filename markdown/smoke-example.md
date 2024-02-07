# Here is an example of a run_smoke.sh run

```
CWD:         /home/jmg/w/famfs/user
BIN:         /home/jmg/w/famfs/user/debug
SCRIPTS:     /home/jmg/w/famfs/user/scripts
TEST_ERRORS: 1
+ sudo mkdir -p /mnt/famfs
+ grep -c famfs /proc/mounts
0
+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs -h

Create a famfs file system:
    /home/jmg/w/famfs/user/debug/mkfs.famfs [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments
    -h|-?      - Print this message
    -f|--force - Will create the file system even if there is allready a superblock
    -k|--kill  - Will 'kill' the superblock (also requires -f)

+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs
mkfs.famfs: /home/jmg/w/famfs/user/mkfs.famfs.c:92: main: Assertion `optind < argc' failed.
./smoke/test0.sh: line 64:  6874 Aborted                 ${MKFS}
+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs /tmp/nonexistent
famfs_mmap_superblock_and_log_raw: open /tmp/nonexistent failed; rc 0 errno 2
famfs_get_device_size: failed to stat file /tmp/nonexistent (No such file or directory)
+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs -f -k /dev/pmem0
kill_super: 1
famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
famfs_get_device_size: size=8589934592
devsize: 8589934592
Famfs superblock killed
+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs /dev/pmem0
famfs_get_role: No valid superblock
famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
famfs_get_device_size: size=8589934592
devsize: 8589934592
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 0 of 25575
  Log size in use:          48
  No allocation errors found

Capacity:
  Device capacity:        8.00G
  Bitmap capacity:        7.99G
  Sum of file sizes:      0.00G
  Allocated space:        0.00G
  Free space:             7.99G
  Space amplification:     -nan
  Percent used:            0.0%

Famfs log:
  0 of 25575 entries used
  0 files
  0 directories

+ sudo /home/jmg/w/famfs/user/debug/mkfs.famfs /dev/pmem0
famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
famfs_get_device_size: size=8589934592
devsize: 8589934592
Device /dev/pmem0 already has a famfs superblock
+ sudo /home/jmg/w/famfs/user/debug/famfs -h
famfs: perform operations on a mounted famfs file system for specific files or devices
famfs [global_args] <command> [args]

Global args:
	--dryrun
Commands:
	mount
	fsck
	check
	mkdir
	cp
	creat
	verify
	mkmeta
	logplay
	getmap
	clone
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /dev/pmem0
famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
famfs_get_device_size: size=8589934592
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 0 of 25575
  Log size in use:          48
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      0
  Allocated bytes:        0
  Free space:             8579448832
  Space amplification:     -nan
  Percent used:            0.0%

Famfs log:
  0 of 25575 entries used
  0 files
  0 directories

+ sudo insmod ../kmod/famfs.ko
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ grep famfs /proc/mounts
/dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
+ grep /dev/pmem0 /proc/mounts
/dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
+ grep /mnt/famfs /proc/mounts
/dev/pmem0 /mnt/famfs famfs rw,noatime 0 0
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo test -f /mnt/famfs/.meta/.superblock
+ sudo test -f /mnt/famfs/.meta/.log
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -h

famfs creat: Create a file in a famfs file system

This testing tool allocates and creates a file of a specified size.

Create a file backed by free space:
    /home/jmg/w/famfs/user/debug/famfs creat -s <size> <filename>

Create a file containing randomized data from a specific seed:
    /home/jmg/w/famfs/user/debug/famfs creat -s size --randomize --seed <myseed> <filename>
Create a file backed by free space, with octal mode 0644:
    /home/jmg/w/famfs/user/debug/famfs creat -s <size> -m 0644 <filename>

Arguments:
    -?                       - Print this message
    -s|--size <size>[kKmMgG] - Required file size
    -S|--seed <random-seed>  - Optional seed for randomization
    -r|--randomize           - Optional - will randomize with provided seed
    -m|--mode <octal-mode>   - Default is 0644
                               Note: mode is ored with ~umask, so the actual mode
                               may be less permissive; see umask for more info
    -u|--uid <int uid>       - Default is caller's uid
    -g|--gid <int gid>       - Default is caller's gid
    -v|--verbose             - Print debugging output while executing the command

NOTE: the --randomize and --seed arguments are useful for testing; the file is
      randomized based on the seed, making it possible to use the 'famfs verify'
      command later to validate the contents of the file

+ sudo /home/jmg/w/famfs/user/debug/famfs creat
Must specify at least one dax device
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 1 /mnt/famfs/test1
Non-zero file size is required
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -h

famfs verify: Verify the contents of a file that was created with 'famfs creat':
    /home/jmg/w/famfs/user/debug/famfs verify -S <seed> -f <filename>

Arguments:
    -?                        - Print this message
    -f|--fillename <filename> - Required file path
    -S|--seed <random-seed>   - Required seed for data verification

+ '{sudo' '/home/jmg/w/famfs/user/debug/famfs}' verify
./smoke/test0.sh: line 94: {sudo: command not found
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -f /mnt/famfs/test1
Must specify random seed to verify file data
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 1 -f badfile
open badfile failed; rc 0 errno 2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 1 -f /mnt/famfs/test1
Success: verified 4096 bytes in file /mnt/famfs/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 99 -f /mnt/famfs/test1
Verify fail at offset 0 of 4096 bytes
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 2 /mnt/famfs/test2
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 3 /mnt/famfs/test3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 1 -f /mnt/famfs/test1
Success: verified 4096 bytes in file /mnt/famfs/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 2 -f /mnt/famfs/test2
Success: verified 4096 bytes in file /mnt/famfs/test2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 3 -f /mnt/famfs/test3
Success: verified 4096 bytes in file /mnt/famfs/test3
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/test1
famfs_file_create: file already exists: /mnt/famfs/test1
do_famfs_cli_creat: failed to create file /mnt/famfs/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 1 /tmp/test1
do_famfs_cli_creat: failed to create file /tmp/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 0 -S 1 /mnt/famfs/emptyfile
invalid file size 0
+ MODE=600
++ id -u
+ UID=1000
./smoke/test0.sh: line 122: UID: readonly variable
++ id -g
+ GID=1000
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -s 0x100000 -r -m 600 -u 1000 -g 1000 /mnt/famfs/testmode0
Randomizing buffer with random seed
++ stat --format=%a /mnt/famfs/testmode0
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ stat --format=%u /mnt/famfs/testmode0
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ stat --format=%g /mnt/famfs/testmode0
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir
Must specify at least one dax device
+ DIRPATH=/mnt/famfs/z/y/x/w
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -p -m 600 -u 1000 -g 1000 /mnt/famfs/z/y/x/w
++ sudo stat --format=%a /mnt/famfs/z/y/x/w
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ sudo stat --format=%u /mnt/famfs/z/y/x/w
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ sudo stat --format=%g /mnt/famfs/z/y/x/w
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -h

famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files

    /home/jmg/w/famfs/user/debug/famfs logplay [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories


+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -rc /mnt/famfs
famfs_logplay: read 8388608 bytes of log
famfs_logplay: processed 8 log entries; 0 new files; 0 new directories
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -rm /mnt/famfs
Error: The --mmap and --read arguments are mutually exclusive


famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files

    /home/jmg/w/famfs/user/debug/famfs logplay [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories


+ sudo /home/jmg/w/famfs/user/debug/famfs logplay
Must specify mount_point (actually any path within a famfs file system will work)

famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files

    /home/jmg/w/famfs/user/debug/famfs logplay [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories


+ sudo umount /mnt/famfs
+ grep -c famfs /proc/mounts
0
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ grep -c famfs /proc/mounts
1
+ echo 'this logplay should fail because we haven'\''t done mkmeta yet'
this logplay should fail because we haven't done mkmeta yet
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -vvv /mnt/famfs
famfs_logplay: failed to open log file for filesystem /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay
Must specify mount_point (actually any path within a famfs file system will work)

famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files

    /home/jmg/w/famfs/user/debug/famfs logplay [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories


+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo test -f /mnt/famfs/.meta/.superblock
+ sudo test -f /mnt/famfs/.meta/.log
+ sudo ls -lR /mnt/famfs
/mnt/famfs:
total 0
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -vvv /mnt/famfs
famfs logplay: log contains 8 entries
__famfs_logplay: 0 file=test1 size=4096
famfs logplay: creating file test1 mode 644
__famfs_logplay: 1 file=test2 size=4096
famfs logplay: creating file test2 mode 644
__famfs_logplay: 2 file=test3 size=4096
famfs logplay: creating file test3 mode 644
__famfs_logplay: 3 file=testmode0 size=1048576
famfs logplay: creating file testmode0 mode 600
famfs logplay: creating directory z
famfs logplay: creating directory z/y
famfs logplay: creating directory z/y/x
famfs logplay: creating directory z/y/x/w
famfs_logplay: processed 8 log entries; 4 new files; 4 new directories
	Created:  4 files, 4 directories
	Existed: 0 files, 0 directories
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta
Must specify at least one dax device

famfs mkmeta:

The famfs file system exposes its superblock and log to its userspace components
as files. After telling the linux kernel to mount a famfs file system, you need
to run 'famfs mkmeta' in order to expose the critical metadata, and then run
'famfs logplay' to play the log. Files will not be visible until these steps
have been performed.

    /home/jmg/w/famfs/user/debug/famfs mkmeta <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments:
    -?               - Print this message
    -v|--verbose     - Print verbose output

+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta -h

famfs mkmeta:

The famfs file system exposes its superblock and log to its userspace components
as files. After telling the linux kernel to mount a famfs file system, you need
to run 'famfs mkmeta' in order to expose the critical metadata, and then run
'famfs logplay' to play the log. Files will not be visible until these steps
have been performed.

    /home/jmg/w/famfs/user/debug/famfs mkmeta <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments:
    -?               - Print this message
    -v|--verbose     - Print verbose output

+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /tmp/nonexistent
do_famfs_cli_mkmeta: unable to rationalize daxdev path from (/tmp/nonexistent) rc 2
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_file_map_create: failed MAP_CREATE for file /mnt/famfs/.meta/.superblock (errno 17)
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -vr /mnt/famfs
famfs_logplay: read 8388608 bytes of log
famfs logplay: log contains 8 entries
famfs_logplay: processed 8 log entries; 0 new files; 0 new directories
	Created:  0 files, 0 directories
	Existed: 4 files, 0 directories
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -m /mnt/famfs
famfs_logplay: processed 8 log entries; 0 new files; 0 new directories
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 1 -f /mnt/famfs/test1
Success: verified 4096 bytes in file /mnt/famfs/test1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 2 -f /mnt/famfs/test2
Success: verified 4096 bytes in file /mnt/famfs/test2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 3 -f /mnt/famfs/test3
Success: verified 4096 bytes in file /mnt/famfs/test3
++ stat --format=%a /mnt/famfs/testmode0
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ stat --format=%u /mnt/famfs/testmode0
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ stat --format=%g /mnt/famfs/testmode0
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ echo 're-checking mkdir -mug after remout'
re-checking mkdir -mug after remout
++ sudo sudo stat --format=%a /mnt/famfs/z/y/x/w
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ sudo stat --format=%u /mnt/famfs/z/y/x/w
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ sudo stat --format=%g /mnt/famfs/z/y/x/w
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ sudo /home/jmg/w/famfs/user/debug/famfs check
famfs_check: Must specify filename

famfs check: check the contents of a famfs file system.

Unlike fsck, which validates the log and that there are no cross-linked files,
this command examines every file in a mounted famfs instance and checks that
the allocation metadata is valid. To get the full picture you need both
'famfs fsck' and 'famfs check'.

This is imporant for a couple of reasons. Although creating a valid famfs file
requires use of the famfs cli or api, it is possible to create invalid files with
the standard system tools (cp, etc.). It is also conceivable that a bug in the
famfs api and/or cli would leave an improperly configured file in place after
unsuccessful error recovery. This commmand will find those invalid
files (if any) and report them.

    /home/jmg/w/famfs/user/debug/famfs check [args] <mount point>

Arguments:
    -?           - Print this message
    -v|--verbose - Print debugging output while executing the command
                   (the verbose arg can be repeated for more verbose output)

Exit codes:
   0    - All files properlly mapped
When non-zero, the exit code is the bitwise or of the following values:
   1    - At least one unmapped file found
   2    - Superblock file missing or corrupt
   4    - Log file missing or corrupt

In the future we may support checking whether each file is in the log, and that
the file properties and map match the log, but the files found in the mounted
file system are not currently compared to the log

TODO: add an option to remove bad files
TODO: add an option to check that all files match the log (and fix problems)

+ sudo /home/jmg/w/famfs/user/debug/famfs check '-?'

famfs check: check the contents of a famfs file system.

Unlike fsck, which validates the log and that there are no cross-linked files,
this command examines every file in a mounted famfs instance and checks that
the allocation metadata is valid. To get the full picture you need both
'famfs fsck' and 'famfs check'.

This is imporant for a couple of reasons. Although creating a valid famfs file
requires use of the famfs cli or api, it is possible to create invalid files with
the standard system tools (cp, etc.). It is also conceivable that a bug in the
famfs api and/or cli would leave an improperly configured file in place after
unsuccessful error recovery. This commmand will find those invalid
files (if any) and report them.

    /home/jmg/w/famfs/user/debug/famfs check [args] <mount point>

Arguments:
    -?           - Print this message
    -v|--verbose - Print debugging output while executing the command
                   (the verbose arg can be repeated for more verbose output)

Exit codes:
   0    - All files properlly mapped
When non-zero, the exit code is the bitwise or of the following values:
   1    - At least one unmapped file found
   2    - Superblock file missing or corrupt
   4    - Log file missing or corrupt

In the future we may support checking whether each file is in the log, and that
the file properties and map match the log, but the files found in the mounted
file system are not currently compared to the log

TODO: add an option to remove bad files
TODO: add an option to check that all files match the log (and fix problems)

+ /home/jmg/w/famfs/user/debug/famfs check /mnt/famfs
famfs_check: superblock file not found for file system /mnt/famfs
famfs_check: log file not found for file system /mnt/famfs
famfs_recursive_check: failed to stat source path (/mnt/famfs/z/y)
famfs_recursive_check: failed to open src dir (/mnt/famfs/.meta)
famfs_check: 4 files, 2 directories, 3 errors
+ sudo /home/jmg/w/famfs/user/debug/famfs check /mnt/famfs
famfs_check: 6 files, 5 directories, 0 errors
+ sudo /home/jmg/w/famfs/user/debug/famfs check relpath
famfs_check: must use absolute path of mount point
+ sudo /home/jmg/w/famfs/user/debug/famfs check /badpath
famfs_check: path (/badpath) is not a famfs mount point
+ sudo touch /mnt/famfs/unmapped_file
+ sudo /home/jmg/w/famfs/user/debug/famfs check -vvv /mnt/famfs
famfs_recursive_check:  /mnt/famfs/unmapped_file
famfs_recursive_check: Error file not mapped: /mnt/famfs/unmapped_file
famfs_recursive_check:  /mnt/famfs/z
famfs_recursive_check:  /mnt/famfs/z/y
famfs_recursive_check:  /mnt/famfs/z/y/x
famfs_recursive_check:  /mnt/famfs/z/y/x/w
famfs_recursive_check:  /mnt/famfs/testmode0
famfs_recursive_check:  /mnt/famfs/test3
famfs_recursive_check:  /mnt/famfs/test2
famfs_recursive_check:  /mnt/famfs/test1
famfs_recursive_check:  /mnt/famfs/.meta
famfs_recursive_check:  /mnt/famfs/.meta/.log
famfs_recursive_check:  /mnt/famfs/.meta/.superblock
famfs_check: 7 files, 5 directories, 1 errors
+ sudo rm /mnt/famfs/unmapped_file
+ sudo /home/jmg/w/famfs/user/debug/famfs check -v /mnt/famfs
famfs_recursive_check:  /mnt/famfs/z
famfs_recursive_check:  /mnt/famfs/z/y
famfs_recursive_check:  /mnt/famfs/z/y/x
famfs_recursive_check:  /mnt/famfs/z/y/x/w
famfs_recursive_check:  /mnt/famfs/testmode0
famfs_recursive_check:  /mnt/famfs/test3
famfs_recursive_check:  /mnt/famfs/test2
famfs_recursive_check:  /mnt/famfs/test1
famfs_recursive_check:  /mnt/famfs/.meta
famfs_recursive_check:  /mnt/famfs/.meta/.log
famfs_recursive_check:  /mnt/famfs/.meta/.superblock
famfs_check: 6 files, 5 directories, 0 errors
+ /home/jmg/w/famfs/user/debug/famfs fsck -hv /mnt/famfs
famfs_fsck: failed to open superblock file
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck
Must specify at least one dax device

famfs fsck: check a famfs file system

This command checks the validity of the superblock and log, and scans the
superblock for cross-linked files.

Check an unmounted famfs file system
    /home/jmg/w/famfs/user/debug/famfs fsck [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0
Check a mounted famfs file system:
    /home/jmg/w/famfs/user/debug/famfs [args] <mount point>

Arguments:
    -?           - Print this message
    -m|--mmap    - Access the superblock and log via mmap
    -h|--human   - Print sizes in a human-friendly form
    -v|--verbose - Print debugging output while executing the command

Exit codes:
  0  - No errors were found
 !=0 - Errors were found

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck '-?'

famfs fsck: check a famfs file system

This command checks the validity of the superblock and log, and scans the
superblock for cross-linked files.

Check an unmounted famfs file system
    /home/jmg/w/famfs/user/debug/famfs fsck [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0
Check a mounted famfs file system:
    /home/jmg/w/famfs/user/debug/famfs [args] <mount point>

Arguments:
    -?           - Print this message
    -m|--mmap    - Access the superblock and log via mmap
    -h|--human   - Print sizes in a human-friendly form
    -v|--verbose - Print debugging output while executing the command

Exit codes:
  0  - No errors were found
 !=0 - Errors were found

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 8 of 25575
  Log size in use:          2672
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      1060864
  Allocated bytes:        8388608
  Free space:             8571060224
  Space amplification:     7.91
  Percent used:            0.1%

Famfs log:
  8 of 25575 entries used
  4 files
  4 directories

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck --human /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 8 of 25575
  Log size in use:          2672
  No allocation errors found

Capacity:
  Device capacity:        8.00G
  Bitmap capacity:        7.99G
  Sum of file sizes:      0.00G
  Allocated space:        0.01G
  Free space:             7.98G
  Space amplification:     7.91
  Percent used:            0.1%

Famfs log:
  8 of 25575 entries used
  4 files
  4 directories

+ set +x
*************************************************************************************
Test0 completed successfully
*************************************************************************************
DEVTYPE=
CLI_FULLLPATH: /home/jmg/w/famfs/user/debug/famfs
CLI: sudo  /home/jmg/w/famfs/user/debug/famfs
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
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
famfs_fsck: failed to open superblock file
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /dev/pmem0
famfs_get_device_size: getting daxdev size from file /sys/class/block/pmem0/size
famfs_get_device_size: size=8589934592
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 8 of 25575
  Log size in use:          2672
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      1060864
  Allocated bytes:        8388608
  Free space:             8571060224
  Space amplification:     7.91
  Percent used:            0.1%

Famfs log:
  8 of 25575 entries used
  4 files
  4 directories

+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG=test1.sh
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 8 log entries; 4 new files; 4 new directories
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 8 of 25575
  Log size in use:          2672
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      1060864
  Allocated bytes:        8388608
  Free space:             8571060224
  Space amplification:     7.91
  Percent used:            0.1%

Famfs log:
  8 of 25575 entries used
  4 files
  4 directories

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /dev/pmem0
famfs_fsck: error - cannot fsck by device (/dev/pmem0) when mounted
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /boguspath
famfs_fsck: failed to stat path /boguspath (No such file or directory)
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck bogusrelpath
famfs_fsck: failed to stat path bogusrelpath (No such file or directory)
+ verify_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
+ F=test10
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 8192 -S 10 /mnt/famfs/test10
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 10 -f /mnt/famfs/test10
Success: verified 8192 bytes in file /mnt/famfs/test10
+ F=bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -v -r -S 42 -s 0x800000 /mnt/famfs/bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/bigtest0
Success: verified 8388608 bytes in file /mnt/famfs/bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -h

famfs cp: Copy one or more files and directories into a famfs file system

Copy a file into a famfs file system
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <destfile> # destfile must not already exist

Copy a file into a directory of a famfs file system with the same basename
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <dirpath>

Copy a wildcard set of files to a directory
    /home/jmg/w/famfs/user/debug/famfs cp [args]/path/to/* <dirpath>

Arguments
    -h|-?            - Print this message
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|verbose       - print debugging output while executing the command

NOTE 1: 'famfs cp' will never overwite an existing file, which is a side-effect
        of the facts that famfs never does delete, truncate or allocate-onn-write
NOTE 2: you need this tool to copy a file into a famfs file system,
        but the standard 'cp' can be used to copy FROM a famfs file system.
        If you inadvertently copy files into famfs using the standard 'cp' (or
        other non-famfs tools), the files created will be invalid. Any such files
        can be found using 'famfs check'.

+ sudo /home/jmg/w/famfs/user/debug/famfs cp -vvv /mnt/famfs/bigtest0 /mnt/famfs/bigtest0_cp
famfs_cp_multi:  /mnt/famfs/bigtest0
famfs_cp: (/mnt/famfs/bigtest0) -> (/mnt/famfs/bigtest0_cp)
famfs_build_bitmap: dev_size 8589934592 nbits 4091 bitmap_nbytes 512
famfs_build_bitmap: superblock and log in bitmap:
   0: 1111100000000000000000000000000000000000000000000000000000000000
famfs_build_bitmap: file=test1 size=4096
famfs_build_bitmap: file=test2 size=4096
famfs_build_bitmap: file=test3 size=4096
famfs_build_bitmap: file=testmode0 size=1048576
famfs_build_bitmap: file=test10 size=8192
famfs_build_bitmap: file=bigtest0 size=8388608
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/bigtest0_cp
Success: verified 8388608 bytes in file /mnt/famfs/bigtest0_cp
+ sudo /home/jmg/w/famfs/user/debug/famfs cp --gid=-1
famfs cp error: source and destination args are required

famfs cp: Copy one or more files and directories into a famfs file system

Copy a file into a famfs file system
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <destfile> # destfile must not already exist

Copy a file into a directory of a famfs file system with the same basename
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <dirpath>

Copy a wildcard set of files to a directory
    /home/jmg/w/famfs/user/debug/famfs cp [args]/path/to/* <dirpath>

Arguments
    -h|-?            - Print this message
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|verbose       - print debugging output while executing the command

NOTE 1: 'famfs cp' will never overwite an existing file, which is a side-effect
        of the facts that famfs never does delete, truncate or allocate-onn-write
NOTE 2: you need this tool to copy a file into a famfs file system,
        but the standard 'cp' can be used to copy FROM a famfs file system.
        If you inadvertently copy files into famfs using the standard 'cp' (or
        other non-famfs tools), the files created will be invalid. Any such files
        can be found using 'famfs check'.

+ sudo /home/jmg/w/famfs/user/debug/famfs cp --uid=-1
famfs cp error: source and destination args are required

famfs cp: Copy one or more files and directories into a famfs file system

Copy a file into a famfs file system
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <destfile> # destfile must not already exist

Copy a file into a directory of a famfs file system with the same basename
    /home/jmg/w/famfs/user/debug/famfs cp [args] <srcfile> <dirpath>

Copy a wildcard set of files to a directory
    /home/jmg/w/famfs/user/debug/famfs cp [args]/path/to/* <dirpath>

Arguments
    -h|-?            - Print this message
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|verbose       - print debugging output while executing the command

NOTE 1: 'famfs cp' will never overwite an existing file, which is a side-effect
        of the facts that famfs never does delete, truncate or allocate-onn-write
NOTE 2: you need this tool to copy a file into a famfs file system,
        but the standard 'cp' can be used to copy FROM a famfs file system.
        If you inadvertently copy files into famfs using the standard 'cp' (or
        other non-famfs tools), the files created will be invalid. Any such files
        can be found using 'famfs check'.

+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -h

famfs mkdir: Create a directory in a famfs file system:

    /home/jmg/w/famfs/user/debug/famfs mkdir [args] <dirname>


Arguments:
    -?               - Print this message
    -p|--parents     - No error if existing, make parent directories as needed,
                       the -m option only applies to dirs actually created
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|--verbose     - Print debugging output while executing the command
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/subdir
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/subdir
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/bigtest0
+ cd /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir foo
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir ./foo/foo
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir foo/foo/./bar
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir ./foo/foo//bar/baz
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir ./foo/./foo//bar/baz
+ cd -
/home/jmg/w/famfs/user
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -p /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -p /mnt/famfs/A/B/C
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -pv /mnt/famfs/AAAA/BBBB/CCC
famfs_mkdir_parents: cwd /home/jmg/w/famfs/user abspath /mnt/famfs/AAAA/BBBB/CCC
famfs mkdir: created directory '/mnt/famfs/AAAA'
famfs mkdir: created directory '/mnt/famfs/AAAA/BBBB'
famfs mkdir: created directory '/mnt/famfs/AAAA/BBBB/CCC'
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -pv /mnt/famfs/A/B/C/w/x/y/z
famfs_mkdir_parents: cwd /home/jmg/w/famfs/user abspath /mnt/famfs/A/B/C/w/x/y/z
famfs mkdir: created directory '/mnt/famfs/A/B/C/w'
famfs mkdir: created directory '/mnt/famfs/A/B/C/w/x'
famfs mkdir: created directory '/mnt/famfs/A/B/C/w/x/y'
famfs mkdir: created directory '/mnt/famfs/A/B/C/w/x/y/z'
+ sudo chmod 0666 /mnt/famfs/A
+ cd /mnt/famfs
+ pwd
/mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -p A/x/y/z
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -p ./A/x/y/z
+ cd -
/home/jmg/w/famfs/user
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -pv /mnt/famfs/bigtest0/foo/bar/baz/bing
famfs_mkdir_parents: cwd /home/jmg/w/famfs/user abspath /mnt/famfs/bigtest0/foo/bar/baz/bing
famfs_make_parent_dir: path /mnt/famfs/bigtest0 is not a directory
famfs_make_parent_dir: bad path component above (/mnt/famfs/bigtest0/foo)
famfs_make_parent_dir: bad path component above (/mnt/famfs/bigtest0/foo/bar)
famfs_make_parent_dir: bad path component above (/mnt/famfs/bigtest0/foo/bar/baz)
famfs_make_parent_dir: bad path component above (/mnt/famfs/bigtest0/foo/bar/baz/bing)
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir -pvvv /mnt/famfs/a/y/../../../..
famfs_mkdir_parents: cwd /home/jmg/w/famfs/user abspath /mnt/famfs/a/y/../../../..
famfs_make_parent_dir: dir /mnt/famfs/a depth 5
famfs mkdir: created directory '/mnt/famfs/a'
famfs_make_parent_dir: dir /mnt/famfs/a/y depth 4
famfs mkdir: created directory '/mnt/famfs/a/y'
famfs_make_parent_dir: dir /mnt/famfs/a/y/.. depth 3
famfs_make_parent_dir: bad path component above (/mnt/famfs/a/y/../..)
famfs_make_parent_dir: bad path component above (/mnt/famfs/a/y/../../..)
famfs_make_parent_dir: bad path component above (/mnt/famfs/a/y/../../../..)
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -v /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp8
famfs_cp_multi:  /mnt/famfs/bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -v /mnt/famfs/bigtest0 /mnt/famfs/subdir/bigtest0_cp9
famfs_cp_multi:  /mnt/famfs/bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /tmp/nonexistent_file /mnt/famfs/
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /dev/zero /mnt/famfs/
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/dirtarg
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/bigtest0 /mnt/famfs/dirtarg
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay -n /mnt/famfs
Logplay: dry_run selected
famfs_logplay: processed 43 log entries; 0 new files; 0 new directories
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp0
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp1
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp2
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp3
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp4
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp5
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp6
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp7
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp8
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp9
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp9
+ MODE=600
++ id -u
+ UID=1000
./smoke/test1.sh: line 184: UID: readonly variable
++ id -g
+ GID=1000
+ cd /mnt/famfs
+ DEST=A/B/C/w/x/y/z
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -m 600 -u 1000 -g 1000 /mnt/famfs/subdir/bigtest0_cp0 /mnt/famfs/subdir/bigtest0_cp1 /mnt/famfs/subdir/bigtest0_cp2 /mnt/famfs/subdir/bigtest0_cp3 /mnt/famfs/subdir/bigtest0_cp4 /mnt/famfs/subdir/bigtest0_cp5 /mnt/famfs/subdir/bigtest0_cp6 /mnt/famfs/subdir/bigtest0_cp7 /mnt/famfs/subdir/bigtest0_cp8 /mnt/famfs/subdir/bigtest0_cp9 /mnt/famfs/A/B/C/w/x/y/z
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp0
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp1
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp2
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp3
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp4
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp5
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp6
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp7
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp8
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp9
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp9
+ cd -
/home/jmg/w/famfs/user
+ FILE=/mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
++ sudo stat --format=%a /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ sudo stat --format=%u /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ sudo stat --format=%g /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ sudo umount /mnt/famfs
+ verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
0
+ grep -c /mnt/famfs /proc/mounts
0
+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG=test1.sh
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 53 log entries; 28 new files; 25 new directories
+ verify_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
++ sudo stat --format=%a /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ MODE_OUT=600
+ [[ 600 != 600 ]]
++ sudo stat --format=%u /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ UID_OUT=1000
+ [[ 1000 != 1000 ]]
++ sudo stat --format=%g /mnt/famfs/A/B/C/w/x/y/z/bigtest0_cp0
+ GID_OUT=1000
+ [[ 1000 != 1000 ]]
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/bigtest0_cp
Success: verified 8388608 bytes in file /mnt/famfs/bigtest0_cp
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp0
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp1
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp2
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp3
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp4
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp5
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp6
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp7
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp8
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/subdir/bigtest0_cp9
Success: verified 8388608 bytes in file /mnt/famfs/subdir/bigtest0_cp9
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/subdir/bigtest0_cp0 /mnt/famfs/subdir/bigtest0_cp1 /mnt/famfs/subdir/bigtest0_cp2 /mnt/famfs/subdir/bigtest0_cp3 /mnt/famfs/subdir/bigtest0_cp4 /mnt/famfs/subdir/bigtest0_cp5 /mnt/famfs/subdir/bigtest0_cp6 /mnt/famfs/subdir/bigtest0_cp7 /mnt/famfs/subdir/bigtest0_cp8 /mnt/famfs/subdir/bigtest0_cp9 /mnt/famfs/dirtarg
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp0
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp1
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp2
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp3
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp4
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp5
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp6
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp7
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp8
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg/bigtest0_cp9
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg/bigtest0_cp9
+ cd /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp0
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp1
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp2
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp3
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp4
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp5
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp6
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp7
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp8
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f A/B/C/w/x/y/z/bigtest0_cp9
Success: verified 8388608 bytes in file A/B/C/w/x/y/z/bigtest0_cp9
+ cd -
/home/jmg/w/famfs/user
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/dirtarg2
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/dirtarg/foo
+ cd /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs cp dirtarg/bigtest0 dirtarg/bigtest0_cp0 dirtarg/bigtest0_cp1 dirtarg/bigtest0_cp2 dirtarg/bigtest0_cp3 dirtarg/bigtest0_cp4 dirtarg/bigtest0_cp5 dirtarg/bigtest0_cp6 dirtarg/bigtest0_cp7 dirtarg/bigtest0_cp8 dirtarg/bigtest0_cp9 dirtarg/foo dirtarg2
famfs_cp_multi: -r not specified; omitting directory 'dirtarg/foo'
+ cd -
/home/jmg/w/famfs/user
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp0
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp0
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp1
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp1
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp2
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp2
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp3
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp3
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp4
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp4
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp5
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp5
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp6
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp6
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp7
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp7
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp8
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp8
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 42 -f /mnt/famfs/dirtarg2/bigtest0_cp9
Success: verified 8388608 bytes in file /mnt/famfs/dirtarg2/bigtest0_cp9
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/smalldir
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/dirtarg/bigtest0_cp0 /mnt/famfs/smalldir
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /mnt/famfs/dirtarg/bigtest0_cp1 /mnt/famfs/smalldir
+ sudo /home/jmg/w/famfs/user/debug/famfs mkdir /mnt/famfs/smalldir2
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -rv /mnt/famfs/smalldir/bigtest0_cp0 /mnt/famfs/smalldir/bigtest0_cp1 /mnt/famfs/smalldir2
famfs_cp_multi:  /mnt/famfs/smalldir/bigtest0_cp0
famfs_cp_multi:  /mnt/famfs/smalldir/bigtest0_cp1
+ sudo diff -r /mnt/famfs/smalldir /mnt/famfs/smalldir2
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -r /mnt/famfs/A /mnt/famfs/A-prime
+ sudo diff -r /mnt/famfs/A /mnt/famfs/A-prime
+ cd /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -r A A-double-prime
+ sudo diff -r A A-double-prime
+ cd -
/home/jmg/w/famfs/user
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -r /mnt/famfs/A /mnt/famfs/bar/foo
famfs_cp_multi: unable to get realpath for (/mnt/famfs/bar/foo)
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -r /mnt/famfs/A /mnt/famfs/bigtest0
famfs_cp_multi: Error: destination (/mnt/famfs) exists and is not a directory
+ sudo /home/jmg/w/famfs/user/debug/famfs cp -r /mnt/famfs/A /mnt/famfs/bigtest0/foo
famfs_cp_multi: Error: dest parent (/mnt/famfs/bigtest0) exists and is not a directory
+ sudo touch /tmp/emptyfile
+ sudo /home/jmg/w/famfs/user/debug/famfs cp /tmp/emptyfile /mnt/famfs/emptyfile2
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 122 of 25575
  Log size in use:          40064
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      571494400
  Allocated bytes:        580911104
  Free space:             7998537728
  Space amplification:     1.02
  Percent used:            6.8%

Famfs log:
  122 of 25575 entries used
  73 files
  49 directories

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck -m /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 122 of 25575
  Log size in use:          40064
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      571494400
  Allocated bytes:        580911104
  Free space:             7998537728
  Space amplification:     1.02
  Percent used:            6.8%

Famfs log:
  122 of 25575 entries used
  73 files
  49 directories

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck -vv /mnt/famfs
famfs_fsck: read 8388608 bytes of log
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 122 of 25575
  Log size in use:          40064
famfs_build_bitmap: dev_size 8589934592 nbits 4091 bitmap_nbytes 512
famfs_build_bitmap: superblock and log in bitmap:
   0: 1111100000000000000000000000000000000000000000000000000000000000
famfs_build_bitmap: file=test1 size=4096
famfs_build_bitmap: file=test2 size=4096
famfs_build_bitmap: file=test3 size=4096
famfs_build_bitmap: file=testmode0 size=1048576
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
famfs_build_bitmap: file=dirtarg/bigtest0 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs_build_bitmap: file=A/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp2 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp3 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp4 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp5 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp6 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp7 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp8 size=8388608
famfs_build_bitmap: file=dirtarg/bigtest0_cp9 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp2 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp3 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp4 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp5 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp6 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp7 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp8 size=8388608
famfs_build_bitmap: file=dirtarg2/bigtest0_cp9 size=8388608
famfs_build_bitmap: file=smalldir/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=smalldir/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=smalldir2/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=smalldir2/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=A-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs_build_bitmap: file=A-double-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      571494400
  Allocated bytes:        580911104
  Free space:             7998537728
  Space amplification:     1.02
  Percent used:            6.8%

Famfs log:
  122 of 25575 entries used
  73 files
  49 directories

Verbose:
  log_offset:        2097152
  log_len:           8388608
  sizeof(log header) 48
  sizeof(log_entry)  328
  last_log_index:    25574
  usable log size:   8388320
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
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 122 of 25575
  Log size in use:          40064
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      571494400
  Allocated bytes:        580911104
  Free space:             7998537728
  Space amplification:     1.02
  Percent used:            6.8%

Famfs log:
  122 of 25575 entries used
  73 files
  49 directories

+ NOT_IN_FAMFS=no_leading_slash
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -s 0x400000 no_leading_slash
find_real_parent_path: path no_leading_slash appears not to be in a famfs mount
do_famfs_cli_creat: failed to create file no_leading_slash
+ LOG=/mnt/famfs/.meta/.log
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap -h

famfs getmap: check the validity ofa famfs file, and optionally get the
mapping info for the file

This command is primarily for testing and validation of a famfs file system

    /home/jmg/w/famfs/user/debug/famfs getmap [args] <filename>

Arguments:
    -q|--quiet - Quiet print output, but exit code confirms whether the
                 file is famfs
    -?         - Print this message

Exit codes:
   0    - The file is a fully-mapped famfs file
   1    - The file is not in a famfs file system
   2    - The file is in a famfs file system, but is not mapped
 EBADF  - invalid input
 ENOENT - file not found
 EISDIR - File is not a regular file

This is similar to the xfs_bmap command and is only used for testing

+ sudo /home/jmg/w/famfs/user/debug/famfs getmap
famfs_getmap: Must specify filename

famfs getmap: check the validity ofa famfs file, and optionally get the
mapping info for the file

This command is primarily for testing and validation of a famfs file system

    /home/jmg/w/famfs/user/debug/famfs getmap [args] <filename>

Arguments:
    -q|--quiet - Quiet print output, but exit code confirms whether the
                 file is famfs
    -?         - Print this message

Exit codes:
   0    - The file is a fully-mapped famfs file
   1    - The file is not in a famfs file system
   2    - The file is in a famfs file system, but is not mapped
 EBADF  - invalid input
 ENOENT - file not found
 EISDIR - File is not a regular file

This is similar to the xfs_bmap command and is only used for testing

+ sudo /home/jmg/w/famfs/user/debug/famfs getmap badfile
famfs_getmap: file not found (badfile)
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap -c badfile
famfs_getmap: file not found (badfile)
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap /etc/passwd
famfs_getmap: file (/etc/passwd) not in a famfs file system
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap /mnt/famfs/.meta/.log
File:     /mnt/famfs/.meta/.log
	size:   8388608
	extents: 1
		200000	8388608
famfs_getmap: good file /mnt/famfs/.meta/.log
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap -q /mnt/famfs/.meta/.log
famfs_getmap: good file /mnt/famfs/.meta/.log
+ NOTEXIST=/mnt/famfs/not_exist
+ sudo /home/jmg/w/famfs/user/debug/famfs getmap
famfs_getmap: Must specify filename

famfs getmap: check the validity ofa famfs file, and optionally get the
mapping info for the file

This command is primarily for testing and validation of a famfs file system

    /home/jmg/w/famfs/user/debug/famfs getmap [args] <filename>

Arguments:
    -q|--quiet - Quiet print output, but exit code confirms whether the
                 file is famfs
    -?         - Print this message

Exit codes:
   0    - The file is a fully-mapped famfs file
   1    - The file is not in a famfs file system
   2    - The file is in a famfs file system, but is not mapped
 EBADF  - invalid input
 ENOENT - file not found
 EISDIR - File is not a regular file

This is similar to the xfs_bmap command and is only used for testing

+ sudo /home/jmg/w/famfs/user/debug/famfs getmap no_leading_slash
famfs_getmap: file not found (no_leading_slash)
+ F=bigtest
+ SIZE=0x4000000
+ for N in 10 11 12 13 14 15
+ FILE=bigtest10
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 10 -s 0x4000000 /mnt/famfs/bigtest10
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 10 -f /mnt/famfs/bigtest10
Success: verified 67108864 bytes in file /mnt/famfs/bigtest10
+ for N in 10 11 12 13 14 15
+ FILE=bigtest11
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 11 -s 0x4000000 /mnt/famfs/bigtest11
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 11 -f /mnt/famfs/bigtest11
Success: verified 67108864 bytes in file /mnt/famfs/bigtest11
+ for N in 10 11 12 13 14 15
+ FILE=bigtest12
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 12 -s 0x4000000 /mnt/famfs/bigtest12
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 12 -f /mnt/famfs/bigtest12
Success: verified 67108864 bytes in file /mnt/famfs/bigtest12
+ for N in 10 11 12 13 14 15
+ FILE=bigtest13
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 13 -s 0x4000000 /mnt/famfs/bigtest13
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 13 -f /mnt/famfs/bigtest13
Success: verified 67108864 bytes in file /mnt/famfs/bigtest13
+ for N in 10 11 12 13 14 15
+ FILE=bigtest14
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 14 -s 0x4000000 /mnt/famfs/bigtest14
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 14 -f /mnt/famfs/bigtest14
Success: verified 67108864 bytes in file /mnt/famfs/bigtest14
+ for N in 10 11 12 13 14 15
+ FILE=bigtest15
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -S 15 -s 0x4000000 /mnt/famfs/bigtest15
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 15 -f /mnt/famfs/bigtest15
Success: verified 67108864 bytes in file /mnt/famfs/bigtest15
+ for N in 10 11 12 13 14 15
+ FILE=bigtest10
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 10 -f /mnt/famfs/bigtest10
Success: verified 67108864 bytes in file /mnt/famfs/bigtest10
+ for N in 10 11 12 13 14 15
+ FILE=bigtest11
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 11 -f /mnt/famfs/bigtest11
Success: verified 67108864 bytes in file /mnt/famfs/bigtest11
+ for N in 10 11 12 13 14 15
+ FILE=bigtest12
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 12 -f /mnt/famfs/bigtest12
Success: verified 67108864 bytes in file /mnt/famfs/bigtest12
+ for N in 10 11 12 13 14 15
+ FILE=bigtest13
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 13 -f /mnt/famfs/bigtest13
Success: verified 67108864 bytes in file /mnt/famfs/bigtest13
+ for N in 10 11 12 13 14 15
+ FILE=bigtest14
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 14 -f /mnt/famfs/bigtest14
Success: verified 67108864 bytes in file /mnt/famfs/bigtest14
+ for N in 10 11 12 13 14 15
+ FILE=bigtest15
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 15 -f /mnt/famfs/bigtest15
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
+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG=test1.sh
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 128 log entries; 79 new files; 49 new directories
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
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -r -s 4096 -S 1 /mnt/famfs/ddtest
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 1 -f /mnt/famfs/test1
Success: verified 4096 bytes in file /mnt/famfs/test1
+ sudo dd of=/dev/null if=/mnt/famfs/ddtest bs=4096
1+0 records in
1+0 records out
4096 bytes (4.1 kB, 4.0 KiB) copied, 7.167e-05 s, 57.2 MB/s
+ sudo umount /mnt/famfs
+ verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
0
+ grep -c /mnt/famfs /proc/mounts
0
+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' full_mount
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG=full_mount
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 129 log entries; 80 new files; 49 new directories
+ verify_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 129 of 25575
  Log size in use:          42360
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      974151680
  Allocated bytes:        985661440
  Free space:             7593787392
  Space amplification:     1.01
  Percent used:            11.5%

Famfs log:
  129 of 25575 entries used
  80 files
  49 directories

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
+ sudo /home/jmg/w/famfs/user/debug/famfs badarg
famfs cli: Unrecognized command badarg
famfs: perform operations on a mounted famfs file system for specific files or devices
famfs [global_args] <command> [args]

Global args:
	--dryrun
Commands:
	mount
	fsck
	check
	mkdir
	cp
	creat
	verify
	mkmeta
	logplay
	getmap
	clone
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -h

famfs creat: Create a file in a famfs file system

This testing tool allocates and creates a file of a specified size.

Create a file backed by free space:
    /home/jmg/w/famfs/user/debug/famfs creat -s <size> <filename>

Create a file containing randomized data from a specific seed:
    /home/jmg/w/famfs/user/debug/famfs creat -s size --randomize --seed <myseed> <filename>
Create a file backed by free space, with octal mode 0644:
    /home/jmg/w/famfs/user/debug/famfs creat -s <size> -m 0644 <filename>

Arguments:
    -?                       - Print this message
    -s|--size <size>[kKmMgG] - Required file size
    -S|--seed <random-seed>  - Optional seed for randomization
    -r|--randomize           - Optional - will randomize with provided seed
    -m|--mode <octal-mode>   - Default is 0644
                               Note: mode is ored with ~umask, so the actual mode
                               may be less permissive; see umask for more info
    -u|--uid <int uid>       - Default is caller's uid
    -g|--gid <int gid>       - Default is caller's gid
    -v|--verbose             - Print debugging output while executing the command

NOTE: the --randomize and --seed arguments are useful for testing; the file is
      randomized based on the seed, making it possible to use the 'famfs verify'
      command later to validate the contents of the file

+ sudo /home/jmg/w/famfs/user/debug/famfs creat -s 3g /mnt/famfs/memfile
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -s 100m /mnt/famfs/memfile1
+ sudo /home/jmg/w/famfs/user/debug/famfs creat -s 10000k /mnt/famfs/memfile2
+ sudo sh -c 'echo 1 > /sys/fs/famfs/fault_count_enable'
+ sudo /home/jmg/w/famfs/user/debug/src/multichase/multichase -d /mnt/famfs/memfile -m 2900m
Arena is not devdax, but a regular file
cheap_create_dax: /mnt/famfs/memfile size is 3221225472
Allocated cursor_heap size 3221225472
 131.0
+ set +x
pte faults:0
pmd faults: 1483
pud faults: 3
+ verify_mounted /dev/pmem0 /mnt/famfs 'test4.sh mounted'
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG='test4.sh mounted'
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
+ sudo umount /mnt/famfs
+ verify_not_mounted /dev/pmem0 /mnt/famfs test4.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test4.sh
+ grep -c /dev/pmem0 /proc/mounts
0
+ grep -c /mnt/famfs /proc/mounts
0
+ sudo /home/jmg/w/famfs/user/debug/famfs mount -vvv /dev/pmem0 /mnt/famfs
famfs_module_loaded: YES
famfs_mkmeta: Meta files successfullly created
famfs logplay: log contains 132 entries
__famfs_logplay: 0 file=test1 size=4096
famfs logplay: creating file test1 mode 644
__famfs_logplay: 1 file=test2 size=4096
famfs logplay: creating file test2 mode 644
__famfs_logplay: 2 file=test3 size=4096
famfs logplay: creating file test3 mode 644
__famfs_logplay: 3 file=testmode0 size=1048576
famfs logplay: creating file testmode0 mode 600
famfs logplay: creating directory z
famfs logplay: creating directory z/y
famfs logplay: creating directory z/y/x
famfs logplay: creating directory z/y/x/w
__famfs_logplay: 8 file=test10 size=8192
famfs logplay: creating file test10 mode 644
__famfs_logplay: 9 file=bigtest0 size=8388608
famfs logplay: creating file bigtest0 mode 644
__famfs_logplay: 10 file=bigtest0_cp size=8388608
famfs logplay: creating file bigtest0_cp mode 100644
famfs logplay: creating directory subdir
famfs logplay: creating directory foo
famfs logplay: creating directory foo/foo
famfs logplay: creating directory foo/foo/bar
famfs logplay: creating directory foo/foo/bar/baz
famfs logplay: creating directory A
famfs logplay: creating directory A/B
famfs logplay: creating directory A/B/C
famfs logplay: creating directory AAAA
famfs logplay: creating directory AAAA/BBBB
famfs logplay: creating directory AAAA/BBBB/CCC
famfs logplay: creating directory A/B/C/w
famfs logplay: creating directory A/B/C/w/x
famfs logplay: creating directory A/B/C/w/x/y
famfs logplay: creating directory A/B/C/w/x/y/z
famfs logplay: creating directory A/x
famfs logplay: creating directory A/x/y
famfs logplay: creating directory A/x/y/z
famfs logplay: creating directory a
famfs logplay: creating directory a/y
__famfs_logplay: 31 file=subdir/bigtest0_cp0 size=8388608
famfs logplay: creating file subdir/bigtest0_cp0 mode 100644
__famfs_logplay: 32 file=subdir/bigtest0_cp1 size=8388608
famfs logplay: creating file subdir/bigtest0_cp1 mode 100644
__famfs_logplay: 33 file=subdir/bigtest0_cp2 size=8388608
famfs logplay: creating file subdir/bigtest0_cp2 mode 100644
__famfs_logplay: 34 file=subdir/bigtest0_cp3 size=8388608
famfs logplay: creating file subdir/bigtest0_cp3 mode 100644
__famfs_logplay: 35 file=subdir/bigtest0_cp4 size=8388608
famfs logplay: creating file subdir/bigtest0_cp4 mode 100644
__famfs_logplay: 36 file=subdir/bigtest0_cp5 size=8388608
famfs logplay: creating file subdir/bigtest0_cp5 mode 100644
__famfs_logplay: 37 file=subdir/bigtest0_cp6 size=8388608
famfs logplay: creating file subdir/bigtest0_cp6 mode 100644
__famfs_logplay: 38 file=subdir/bigtest0_cp7 size=8388608
famfs logplay: creating file subdir/bigtest0_cp7 mode 100644
__famfs_logplay: 39 file=subdir/bigtest0_cp8 size=8388608
famfs logplay: creating file subdir/bigtest0_cp8 mode 100644
__famfs_logplay: 40 file=subdir/bigtest0_cp9 size=8388608
famfs logplay: creating file subdir/bigtest0_cp9 mode 100644
famfs logplay: creating directory dirtarg
__famfs_logplay: 42 file=dirtarg/bigtest0 size=8388608
famfs logplay: creating file dirtarg/bigtest0 mode 100644
__famfs_logplay: 43 file=A/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp0 mode 600
__famfs_logplay: 44 file=A/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp1 mode 600
__famfs_logplay: 45 file=A/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp2 mode 600
__famfs_logplay: 46 file=A/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp3 mode 600
__famfs_logplay: 47 file=A/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp4 mode 600
__famfs_logplay: 48 file=A/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp5 mode 600
__famfs_logplay: 49 file=A/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp6 mode 600
__famfs_logplay: 50 file=A/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp7 mode 600
__famfs_logplay: 51 file=A/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp8 mode 600
__famfs_logplay: 52 file=A/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp9 mode 600
__famfs_logplay: 53 file=dirtarg/bigtest0_cp0 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp0 mode 100644
__famfs_logplay: 54 file=dirtarg/bigtest0_cp1 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp1 mode 100644
__famfs_logplay: 55 file=dirtarg/bigtest0_cp2 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp2 mode 100644
__famfs_logplay: 56 file=dirtarg/bigtest0_cp3 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp3 mode 100644
__famfs_logplay: 57 file=dirtarg/bigtest0_cp4 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp4 mode 100644
__famfs_logplay: 58 file=dirtarg/bigtest0_cp5 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp5 mode 100644
__famfs_logplay: 59 file=dirtarg/bigtest0_cp6 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp6 mode 100644
__famfs_logplay: 60 file=dirtarg/bigtest0_cp7 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp7 mode 100644
__famfs_logplay: 61 file=dirtarg/bigtest0_cp8 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp8 mode 100644
__famfs_logplay: 62 file=dirtarg/bigtest0_cp9 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp9 mode 100644
famfs logplay: creating directory dirtarg2
famfs logplay: creating directory dirtarg/foo
__famfs_logplay: 65 file=dirtarg2/bigtest0 size=8388608
famfs logplay: creating file dirtarg2/bigtest0 mode 100644
__famfs_logplay: 66 file=dirtarg2/bigtest0_cp0 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp0 mode 100644
__famfs_logplay: 67 file=dirtarg2/bigtest0_cp1 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp1 mode 100644
__famfs_logplay: 68 file=dirtarg2/bigtest0_cp2 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp2 mode 100644
__famfs_logplay: 69 file=dirtarg2/bigtest0_cp3 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp3 mode 100644
__famfs_logplay: 70 file=dirtarg2/bigtest0_cp4 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp4 mode 100644
__famfs_logplay: 71 file=dirtarg2/bigtest0_cp5 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp5 mode 100644
__famfs_logplay: 72 file=dirtarg2/bigtest0_cp6 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp6 mode 100644
__famfs_logplay: 73 file=dirtarg2/bigtest0_cp7 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp7 mode 100644
__famfs_logplay: 74 file=dirtarg2/bigtest0_cp8 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp8 mode 100644
__famfs_logplay: 75 file=dirtarg2/bigtest0_cp9 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp9 mode 100644
famfs logplay: creating directory smalldir
__famfs_logplay: 77 file=smalldir/bigtest0_cp0 size=8388608
famfs logplay: creating file smalldir/bigtest0_cp0 mode 100644
__famfs_logplay: 78 file=smalldir/bigtest0_cp1 size=8388608
famfs logplay: creating file smalldir/bigtest0_cp1 mode 100644
famfs logplay: creating directory smalldir2
__famfs_logplay: 80 file=smalldir2/bigtest0_cp0 size=8388608
famfs logplay: creating file smalldir2/bigtest0_cp0 mode 100644
__famfs_logplay: 81 file=smalldir2/bigtest0_cp1 size=8388608
famfs logplay: creating file smalldir2/bigtest0_cp1 mode 100644
famfs logplay: creating directory A-prime
famfs logplay: creating directory A-prime/x
famfs logplay: creating directory A-prime/x/y
famfs logplay: creating directory A-prime/x/y/z
famfs logplay: creating directory A-prime/B
famfs logplay: creating directory A-prime/B/C
famfs logplay: creating directory A-prime/B/C/w
famfs logplay: creating directory A-prime/B/C/w/x
famfs logplay: creating directory A-prime/B/C/w/x/y
famfs logplay: creating directory A-prime/B/C/w/x/y/z
__famfs_logplay: 92 file=A-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp9 mode 100600
__famfs_logplay: 93 file=A-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp8 mode 100600
__famfs_logplay: 94 file=A-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp7 mode 100600
__famfs_logplay: 95 file=A-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp6 mode 100600
__famfs_logplay: 96 file=A-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp5 mode 100600
__famfs_logplay: 97 file=A-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp4 mode 100600
__famfs_logplay: 98 file=A-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp3 mode 100600
__famfs_logplay: 99 file=A-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp2 mode 100600
__famfs_logplay: 100 file=A-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp1 mode 100600
__famfs_logplay: 101 file=A-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp0 mode 100600
famfs logplay: creating directory A-double-prime
famfs logplay: creating directory A-double-prime/x
famfs logplay: creating directory A-double-prime/x/y
famfs logplay: creating directory A-double-prime/x/y/z
famfs logplay: creating directory A-double-prime/B
famfs logplay: creating directory A-double-prime/B/C
famfs logplay: creating directory A-double-prime/B/C/w
famfs logplay: creating directory A-double-prime/B/C/w/x
famfs logplay: creating directory A-double-prime/B/C/w/x/y
famfs logplay: creating directory A-double-prime/B/C/w/x/y/z
__famfs_logplay: 112 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp9 mode 100600
__famfs_logplay: 113 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp8 mode 100600
__famfs_logplay: 114 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp7 mode 100600
__famfs_logplay: 115 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp6 mode 100600
__famfs_logplay: 116 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp5 mode 100600
__famfs_logplay: 117 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp4 mode 100600
__famfs_logplay: 118 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp3 mode 100600
__famfs_logplay: 119 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp2 mode 100600
__famfs_logplay: 120 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp1 mode 100600
__famfs_logplay: 121 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp0 mode 100600
__famfs_logplay: 122 file=bigtest10 size=67108864
famfs logplay: creating file bigtest10 mode 644
__famfs_logplay: 123 file=bigtest11 size=67108864
famfs logplay: creating file bigtest11 mode 644
__famfs_logplay: 124 file=bigtest12 size=67108864
famfs logplay: creating file bigtest12 mode 644
__famfs_logplay: 125 file=bigtest13 size=67108864
famfs logplay: creating file bigtest13 mode 644
__famfs_logplay: 126 file=bigtest14 size=67108864
famfs logplay: creating file bigtest14 mode 644
__famfs_logplay: 127 file=bigtest15 size=67108864
famfs logplay: creating file bigtest15 mode 644
__famfs_logplay: 128 file=ddtest size=4096
famfs logplay: creating file ddtest mode 644
__famfs_logplay: 129 file=memfile size=3221225472
famfs logplay: creating file memfile mode 644
__famfs_logplay: 130 file=memfile1 size=104857600
famfs logplay: creating file memfile1 mode 644
__famfs_logplay: 131 file=memfile2 size=10240000
famfs logplay: creating file memfile2 mode 644
famfs_logplay: processed 132 log entries; 83 new files; 49 new directories
	Created:  83 files, 49 directories
	Existed: 0 files, 0 directories
+ sudo /home/jmg/w/famfs/user/debug/famfs mount -vvv /dev/pmem0 /mnt/famfs
famfs_module_loaded: YES
+ F=/mnt/famfs/test1
+ sudo rm /mnt/famfs/test1
+ sudo umount /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mount '-?'

famfs mount: mount a famfs file system and make it ready to use

We recommend using the 'famfs mount' command rather than the native system mount
command, because there are additional steps necessary to make a famfs file system
ready to use after the system mount (see mkmeta and logplay). This command takes
care of the whole job.

    /home/jmg/w/famfs/user/debug/famfs mount <memdevice> <mountpoint>

Arguments:
    -?             - Print this message
    -r             - Re-mount
    -v|--verbose   - Print verbose output

+ sudo /home/jmg/w/famfs/user/debug/famfs mount
famfs mount error: <daxdev> and <mountpoint> args are required

famfs mount: mount a famfs file system and make it ready to use

We recommend using the 'famfs mount' command rather than the native system mount
command, because there are additional steps necessary to make a famfs file system
ready to use after the system mount (see mkmeta and logplay). This command takes
care of the whole job.

    /home/jmg/w/famfs/user/debug/famfs mount <memdevice> <mountpoint>

Arguments:
    -?             - Print this message
    -r             - Re-mount
    -v|--verbose   - Print verbose output

+ sudo /home/jmg/w/famfs/user/debug/famfs mount a b c
famfs mount error: <daxdev> and <mountpoint> args are required

famfs mount: mount a famfs file system and make it ready to use

We recommend using the 'famfs mount' command rather than the native system mount
command, because there are additional steps necessary to make a famfs file system
ready to use after the system mount (see mkmeta and logplay). This command takes
care of the whole job.

    /home/jmg/w/famfs/user/debug/famfs mount <memdevice> <mountpoint>

Arguments:
    -?             - Print this message
    -r             - Re-mount
    -v|--verbose   - Print verbose output

+ sudo /home/jmg/w/famfs/user/debug/famfs mount baddev /mnt/famfs
famfs mount: daxdev (baddev) not found
+ sudo /home/jmg/w/famfs/user/debug/famfs mount /dev/pmem0 badmpt
famfs mount: mount pt (badmpt) not found
+ sudo /home/jmg/w/famfs/user/debug/famfs mount -vvv /dev/pmem0 /mnt/famfs
famfs_module_loaded: YES
famfs_mkmeta: Meta files successfullly created
famfs logplay: log contains 132 entries
__famfs_logplay: 0 file=test1 size=4096
famfs logplay: creating file test1 mode 644
__famfs_logplay: 1 file=test2 size=4096
famfs logplay: creating file test2 mode 644
__famfs_logplay: 2 file=test3 size=4096
famfs logplay: creating file test3 mode 644
__famfs_logplay: 3 file=testmode0 size=1048576
famfs logplay: creating file testmode0 mode 600
famfs logplay: creating directory z
famfs logplay: creating directory z/y
famfs logplay: creating directory z/y/x
famfs logplay: creating directory z/y/x/w
__famfs_logplay: 8 file=test10 size=8192
famfs logplay: creating file test10 mode 644
__famfs_logplay: 9 file=bigtest0 size=8388608
famfs logplay: creating file bigtest0 mode 644
__famfs_logplay: 10 file=bigtest0_cp size=8388608
famfs logplay: creating file bigtest0_cp mode 100644
famfs logplay: creating directory subdir
famfs logplay: creating directory foo
famfs logplay: creating directory foo/foo
famfs logplay: creating directory foo/foo/bar
famfs logplay: creating directory foo/foo/bar/baz
famfs logplay: creating directory A
famfs logplay: creating directory A/B
famfs logplay: creating directory A/B/C
famfs logplay: creating directory AAAA
famfs logplay: creating directory AAAA/BBBB
famfs logplay: creating directory AAAA/BBBB/CCC
famfs logplay: creating directory A/B/C/w
famfs logplay: creating directory A/B/C/w/x
famfs logplay: creating directory A/B/C/w/x/y
famfs logplay: creating directory A/B/C/w/x/y/z
famfs logplay: creating directory A/x
famfs logplay: creating directory A/x/y
famfs logplay: creating directory A/x/y/z
famfs logplay: creating directory a
famfs logplay: creating directory a/y
__famfs_logplay: 31 file=subdir/bigtest0_cp0 size=8388608
famfs logplay: creating file subdir/bigtest0_cp0 mode 100644
__famfs_logplay: 32 file=subdir/bigtest0_cp1 size=8388608
famfs logplay: creating file subdir/bigtest0_cp1 mode 100644
__famfs_logplay: 33 file=subdir/bigtest0_cp2 size=8388608
famfs logplay: creating file subdir/bigtest0_cp2 mode 100644
__famfs_logplay: 34 file=subdir/bigtest0_cp3 size=8388608
famfs logplay: creating file subdir/bigtest0_cp3 mode 100644
__famfs_logplay: 35 file=subdir/bigtest0_cp4 size=8388608
famfs logplay: creating file subdir/bigtest0_cp4 mode 100644
__famfs_logplay: 36 file=subdir/bigtest0_cp5 size=8388608
famfs logplay: creating file subdir/bigtest0_cp5 mode 100644
__famfs_logplay: 37 file=subdir/bigtest0_cp6 size=8388608
famfs logplay: creating file subdir/bigtest0_cp6 mode 100644
__famfs_logplay: 38 file=subdir/bigtest0_cp7 size=8388608
famfs logplay: creating file subdir/bigtest0_cp7 mode 100644
__famfs_logplay: 39 file=subdir/bigtest0_cp8 size=8388608
famfs logplay: creating file subdir/bigtest0_cp8 mode 100644
__famfs_logplay: 40 file=subdir/bigtest0_cp9 size=8388608
famfs logplay: creating file subdir/bigtest0_cp9 mode 100644
famfs logplay: creating directory dirtarg
__famfs_logplay: 42 file=dirtarg/bigtest0 size=8388608
famfs logplay: creating file dirtarg/bigtest0 mode 100644
__famfs_logplay: 43 file=A/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp0 mode 600
__famfs_logplay: 44 file=A/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp1 mode 600
__famfs_logplay: 45 file=A/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp2 mode 600
__famfs_logplay: 46 file=A/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp3 mode 600
__famfs_logplay: 47 file=A/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp4 mode 600
__famfs_logplay: 48 file=A/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp5 mode 600
__famfs_logplay: 49 file=A/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp6 mode 600
__famfs_logplay: 50 file=A/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp7 mode 600
__famfs_logplay: 51 file=A/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp8 mode 600
__famfs_logplay: 52 file=A/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A/B/C/w/x/y/z/bigtest0_cp9 mode 600
__famfs_logplay: 53 file=dirtarg/bigtest0_cp0 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp0 mode 100644
__famfs_logplay: 54 file=dirtarg/bigtest0_cp1 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp1 mode 100644
__famfs_logplay: 55 file=dirtarg/bigtest0_cp2 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp2 mode 100644
__famfs_logplay: 56 file=dirtarg/bigtest0_cp3 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp3 mode 100644
__famfs_logplay: 57 file=dirtarg/bigtest0_cp4 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp4 mode 100644
__famfs_logplay: 58 file=dirtarg/bigtest0_cp5 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp5 mode 100644
__famfs_logplay: 59 file=dirtarg/bigtest0_cp6 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp6 mode 100644
__famfs_logplay: 60 file=dirtarg/bigtest0_cp7 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp7 mode 100644
__famfs_logplay: 61 file=dirtarg/bigtest0_cp8 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp8 mode 100644
__famfs_logplay: 62 file=dirtarg/bigtest0_cp9 size=8388608
famfs logplay: creating file dirtarg/bigtest0_cp9 mode 100644
famfs logplay: creating directory dirtarg2
famfs logplay: creating directory dirtarg/foo
__famfs_logplay: 65 file=dirtarg2/bigtest0 size=8388608
famfs logplay: creating file dirtarg2/bigtest0 mode 100644
__famfs_logplay: 66 file=dirtarg2/bigtest0_cp0 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp0 mode 100644
__famfs_logplay: 67 file=dirtarg2/bigtest0_cp1 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp1 mode 100644
__famfs_logplay: 68 file=dirtarg2/bigtest0_cp2 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp2 mode 100644
__famfs_logplay: 69 file=dirtarg2/bigtest0_cp3 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp3 mode 100644
__famfs_logplay: 70 file=dirtarg2/bigtest0_cp4 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp4 mode 100644
__famfs_logplay: 71 file=dirtarg2/bigtest0_cp5 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp5 mode 100644
__famfs_logplay: 72 file=dirtarg2/bigtest0_cp6 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp6 mode 100644
__famfs_logplay: 73 file=dirtarg2/bigtest0_cp7 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp7 mode 100644
__famfs_logplay: 74 file=dirtarg2/bigtest0_cp8 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp8 mode 100644
__famfs_logplay: 75 file=dirtarg2/bigtest0_cp9 size=8388608
famfs logplay: creating file dirtarg2/bigtest0_cp9 mode 100644
famfs logplay: creating directory smalldir
__famfs_logplay: 77 file=smalldir/bigtest0_cp0 size=8388608
famfs logplay: creating file smalldir/bigtest0_cp0 mode 100644
__famfs_logplay: 78 file=smalldir/bigtest0_cp1 size=8388608
famfs logplay: creating file smalldir/bigtest0_cp1 mode 100644
famfs logplay: creating directory smalldir2
__famfs_logplay: 80 file=smalldir2/bigtest0_cp0 size=8388608
famfs logplay: creating file smalldir2/bigtest0_cp0 mode 100644
__famfs_logplay: 81 file=smalldir2/bigtest0_cp1 size=8388608
famfs logplay: creating file smalldir2/bigtest0_cp1 mode 100644
famfs logplay: creating directory A-prime
famfs logplay: creating directory A-prime/x
famfs logplay: creating directory A-prime/x/y
famfs logplay: creating directory A-prime/x/y/z
famfs logplay: creating directory A-prime/B
famfs logplay: creating directory A-prime/B/C
famfs logplay: creating directory A-prime/B/C/w
famfs logplay: creating directory A-prime/B/C/w/x
famfs logplay: creating directory A-prime/B/C/w/x/y
famfs logplay: creating directory A-prime/B/C/w/x/y/z
__famfs_logplay: 92 file=A-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp9 mode 100600
__famfs_logplay: 93 file=A-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp8 mode 100600
__famfs_logplay: 94 file=A-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp7 mode 100600
__famfs_logplay: 95 file=A-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp6 mode 100600
__famfs_logplay: 96 file=A-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp5 mode 100600
__famfs_logplay: 97 file=A-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp4 mode 100600
__famfs_logplay: 98 file=A-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp3 mode 100600
__famfs_logplay: 99 file=A-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp2 mode 100600
__famfs_logplay: 100 file=A-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp1 mode 100600
__famfs_logplay: 101 file=A-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A-prime/B/C/w/x/y/z/bigtest0_cp0 mode 100600
famfs logplay: creating directory A-double-prime
famfs logplay: creating directory A-double-prime/x
famfs logplay: creating directory A-double-prime/x/y
famfs logplay: creating directory A-double-prime/x/y/z
famfs logplay: creating directory A-double-prime/B
famfs logplay: creating directory A-double-prime/B/C
famfs logplay: creating directory A-double-prime/B/C/w
famfs logplay: creating directory A-double-prime/B/C/w/x
famfs logplay: creating directory A-double-prime/B/C/w/x/y
famfs logplay: creating directory A-double-prime/B/C/w/x/y/z
__famfs_logplay: 112 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp9 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp9 mode 100600
__famfs_logplay: 113 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp8 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp8 mode 100600
__famfs_logplay: 114 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp7 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp7 mode 100600
__famfs_logplay: 115 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp6 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp6 mode 100600
__famfs_logplay: 116 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp5 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp5 mode 100600
__famfs_logplay: 117 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp4 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp4 mode 100600
__famfs_logplay: 118 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp3 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp3 mode 100600
__famfs_logplay: 119 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp2 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp2 mode 100600
__famfs_logplay: 120 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp1 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp1 mode 100600
__famfs_logplay: 121 file=A-double-prime/B/C/w/x/y/z/bigtest0_cp0 size=8388608
famfs logplay: creating file A-double-prime/B/C/w/x/y/z/bigtest0_cp0 mode 100600
__famfs_logplay: 122 file=bigtest10 size=67108864
famfs logplay: creating file bigtest10 mode 644
__famfs_logplay: 123 file=bigtest11 size=67108864
famfs logplay: creating file bigtest11 mode 644
__famfs_logplay: 124 file=bigtest12 size=67108864
famfs logplay: creating file bigtest12 mode 644
__famfs_logplay: 125 file=bigtest13 size=67108864
famfs logplay: creating file bigtest13 mode 644
__famfs_logplay: 126 file=bigtest14 size=67108864
famfs logplay: creating file bigtest14 mode 644
__famfs_logplay: 127 file=bigtest15 size=67108864
famfs logplay: creating file bigtest15 mode 644
__famfs_logplay: 128 file=ddtest size=4096
famfs logplay: creating file ddtest mode 644
__famfs_logplay: 129 file=memfile size=3221225472
famfs logplay: creating file memfile mode 644
__famfs_logplay: 130 file=memfile1 size=104857600
famfs logplay: creating file memfile1 mode 644
__famfs_logplay: 131 file=memfile2 size=10240000
famfs logplay: creating file memfile2 mode 644
famfs_logplay: processed 132 log entries; 83 new files; 49 new directories
	Created:  83 files, 49 directories
	Existed: 0 files, 0 directories
+ sudo test -f /mnt/famfs/test1
+ sudo umount /mnt/famfs
+ sudo rmmod famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mount -vvv /dev/pmem0 /mnt/famfs
famfs_module_loaded: NO
famfs mount: famfs kernel module is not loaded!
+ sudo insmod ../kmod/famfs.ko
+ sudo /home/jmg/w/famfs/user/debug/famfs mount /dev/pmem0 /mnt/famfs
famfs_module_loaded: YES
famfs_mkmeta: Meta files successfullly created
famfs_logplay: processed 132 log entries; 83 new files; 49 new directories
+ sudo /home/jmg/w/famfs/user/debug/famfs mount -r /dev/pmem0 /mnt/famfs
famfs_module_loaded: YES
famfs_file_map_create: failed MAP_CREATE for file /mnt/famfs/.meta/.superblock (errno 17)
famfs mount: ignoring err -1 from mkmeta
famfs_logplay: processed 132 log entries; 0 new files; 0 new directories
+ sudo umount /mnt/famfs
+ set +x
*************************************************************************************
Test4 (multichase) completed successfully
*************************************************************************************
DEVTYPE=
SCRIPTS=/home/jmg/w/famfs/user/scripts
+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' 'test_errors full_mount'
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG='test_errors full_mount'
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 132 log entries; 83 new files; 49 new directories
+ verify_mounted /dev/pmem0 /mnt/famfs test2.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test2.sh
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 132 of 25575
  Log size in use:          43344
  No allocation errors found

Capacity:
  Device capacity:        8589934592
  Bitmap capacity:        8579448832
  Sum of file sizes:      4310474752
  Allocated bytes:        4322230272
  Free space:             4257218560
  Space amplification:     1.00
  Percent used:            50.4%

Famfs log:
  132 of 25575 entries used
  83 files
  49 directories

+ N=10
+ FILE=bigtest10
+ sudo /home/jmg/w/famfs/user/debug/famfs clone -h

famfs clone: Clone a file within a famfs file system

This administrative command is only useful in testing, and leaves the
file system in cross-linked state. Don't use it unless yu want to generate
errors for testing!

Clone a file, creating a second file with the same extent list:
    /home/jmg/w/famfs/user/debug/famfs clone <src_file> <dest_file>

Arguments:
    -?           - Print this message

NOTE: this creates a file system error and is for testing only!!

+ sudo /home/jmg/w/famfs/user/debug/famfs clone /mnt/famfs/bigtest10 /mnt/famfs/bigtest10_clone
+ sudo /home/jmg/w/famfs/user/debug/famfs clone -v /mnt/famfs/bigtest10 /mnt/famfs/bigtest10_clone1
+ sudo /home/jmg/w/famfs/user/debug/famfs clone -v /mnt/famfs/bogusfile /mnt/famfs/bogusfile.cllone
do_famfs_cli_clone: bad source path /mnt/famfs/bogusfile
+ sudo /home/jmg/w/famfs/user/debug/famfs clone -v /etc/passwd /mnt/famfs/passwd
famfs_clone: source path (/etc/passwd) not in a famfs file system
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 134 of 25575
  Log size in use:          44000
ERROR: 64 ALLOCATION COLLISIONS FOUND
Famfs log:
  134 of 25575 entries used
  85 files
  49 directories

+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 10 -f /mnt/famfs/bigtest10_clone
Success: verified 67108864 bytes in file /mnt/famfs/bigtest10_clone
+ sudo /home/jmg/w/famfs/user/debug/famfs verify -S 10 -f /mnt/famfs/bigtest10_clone1
Success: verified 67108864 bytes in file /mnt/famfs/bigtest10_clone1
+ sudo umount /mnt/famfs
+ verify_not_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
0
+ grep -c /mnt/famfs /proc/mounts
0
+ full_mount /dev/pmem0 /mnt/famfs '-t famfs -o noatime -o dax=always ' test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MO='-t famfs -o noatime -o dax=always '
+ MSG=test1.sh
+ sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 /mnt/famfs
+ sudo /home/jmg/w/famfs/user/debug/famfs mkmeta /dev/pmem0
famfs_mkmeta: Meta files successfullly created
+ sudo /home/jmg/w/famfs/user/debug/famfs logplay /mnt/famfs
famfs_logplay: processed 134 log entries; 85 new files; 49 new directories
+ verify_mounted /dev/pmem0 /mnt/famfs test1.sh
+ DEV=/dev/pmem0
+ MPT=/mnt/famfs
+ MSG=test1.sh
+ grep -c /dev/pmem0 /proc/mounts
1
+ grep -c /mnt/famfs /proc/mounts
1
+ sudo /home/jmg/w/famfs/user/debug/famfs fsck -v /mnt/famfs
famfs_fsck: read 8388608 bytes of log
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 134 of 25575
  Log size in use:          44000
ERROR: 64 ALLOCATION COLLISIONS FOUND
Famfs log:
  134 of 25575 entries used
  85 files
  49 directories

Verbose:
  log_offset:        2097152
  log_len:           8388608
  sizeof(log header) 48
  sizeof(log_entry)  328
  last_log_index:    25574
  usable log size:   8388320
  sizeof(struct famfs_file_creation): 304
  sizeof(struct famfs_file_access):   44

+ sudo /home/jmg/w/famfs/user/debug/famfs fsck /mnt/famfs
Famfs Superblock:
  Filesystem UUID: 7ee5acd8-b245-472b-b3da-28bc43a0edb4
  System UUID:     bb39a95b-121c-4226-b742-87d2625d610c
  sizeof superblock: 136
  num_daxdevs:              1
  primary: /dev/pmem0   8589934592

Log stats:
  # of log entriesi in use: 134 of 25575
  Log size in use:          44000
ERROR: 64 ALLOCATION COLLISIONS FOUND
Famfs log:
  134 of 25575 entries used
  85 files
  49 directories

+ sudo umount /mnt/famfs
+ set +x
*************************************************************************************
 Important note: This test (at least the first run) will generate a stack dump
 in the kernel log (a WARN_ONCE) due to cross-linked pages (specifically DAX noticing
 that a page was mapped to more than one file. This is normal, as this test intentionally
 does bogus cross-linked mappings
*************************************************************************************
Test_errors completed successfully
*************************************************************************************
famfs_module_loaded: YES
famfs_mkmeta: Meta files successfullly created
famfs_logplay: processed 134 log entries; 85 new files; 49 new directories

```
