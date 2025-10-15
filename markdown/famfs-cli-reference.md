# mkfs.famfs
```

Create a famfs file system:
    mkfs.famfs [args] <memdevice>  # Example memdevice: /dev/dax0.0

Create a famfs file system with a 256MiB log
    mkfs.famfs --loglen 256m /dev/dax0.0

Arguments:
    -h|-?      - Print this message
    -f|--force - Will create the file system even if there is already a valid superblock
    -k|--kill  - Will 'kill' existing superblock (also requires -f)
    -l|--loglen <loglen> - Default loglen: 8 MiB
                           Valid range: >= 8 MiB

```
# The famfs CLI
The famfs CLI enables most of the normal maintenance operations with famfs.

```
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
	flush
	verify
	mkmeta
	logplay
	getmap
	clone
	chkread
```

## famfs mount
```

famfs mount: mount a famfs file system and make it ready to use

    famfs mount [args] <memdevice> <mountpoint>

Arguments:
    -h|-?              - Print this message
    -f|--fuse          - Use famfs via fuse. If specified, the mount will
                         fail if fuse support for famfs is not available.
    -F|--nofuse        - Use the standalone famfs v1 kernel module. If
                         specified, the mount will fail if the famfs v1
                         kernel module is not available
    -t|--timeout       - Fuse metadata timeout in seconds
    -d|--debug         - In fuse mode, the debug option runs the fuse
                         daemon single-threaded, and may enable more
                         verbose logging
    -v|--verbose       - Print verbose output
    -u|--nouseraccess  - Allow non-root access
    -p|--nodefaultperm - Do not apply normal posix permissions
                         (don'd use default_permissions mount opt
    -S|--shadow=path - Path to root of shadow filesystem

```
## famfs fsck
```

famfs fsck: check a famfs file system

This command checks the validity of the superblock and log, and scans the
log for cross-linked files.

Check an unmounted famfs file system
    famfs fsck [args] <memdevice>  # Example memdevice: /dev/dax0.0

Check a mounted famfs file system:
    famfs [args] <mount point>

Arguments:
    -?           - Print this message
    -v|--verbose - Print debugging output while executing the command

Exit codes:
  0  - No errors were found
 !=0 - Errors were found

```
## famfs check
```

famfs check: check the contents of a famfs file system.

NOTE: 'famfs check' is only useful for standalone famfs. For fuse-based
      famfs, a new 'famfs logplay --check' option will be added to run
      appropriate checks for famfs-fuse

Unlike fsck, which validates the log and that there are no cross-linked files,
this command examines every file in a mounted famfs instance and checks that
the allocation metadata is valid. To get the full picture you need both
'famfs fsck' and 'famfs check'.

This is imporant for a couple of reasons. Although creating a valid famfs file
requires use of the famfs cli or api, it is possible to create invalid files with
the standard system tools (cp, etc.). It is also conceivable that a bug in the
famfs api and/or cli would leave an improperly configured file in place after
unsuccessful error recovery. This command will find those invalid
files (if any) and report them.

    famfs check [args] <mount point>

Arguments:
    -h|-?        - Print this message
    -v|--verbose - Print debugging output while executing the command
                   (the verbose arg can be repeated for more verbose output)

Exit codes:
   0    - All files properly mapped
When non-zero, the exit code is the bitwise or of the following values:
   1    - At least one unmapped file found
   2    - Superblock file missing or corrupt
   4    - Log file missing or corrupt

In the future we may support checking whether each file is in the log, and that
the file properties and map match the log, but the files found in the mounted
file system are not currently compared to the log

TODO: add an option to remove bad files
TODO: add an option to check that all files match the log (and fix problems)

```
## famfs mkdir
```

famfs mkdir: Create a directory in a famfs file system:

    famfs mkdir [args] <dirname>


Arguments:
    -h|-?            - Print this message
    -p|--parents     - No error if existing, make parent directories as needed,
                       the -m option only applies to dirs actually created
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|--verbose     - Print debugging output while executing the command
```
## famfs cp
```

famfs cp: Copy one or more files and directories into a famfs file system

Copy a file into a famfs file system
    famfs cp [args] <srcfile> <destfile> # destfile must not already exist

Copy a file into a directory of a famfs file system with the same basename
    famfs cp [args] <srcfile> <dirpath>

Copy a wildcard set of files to a directory
    famfs cp [args]/path/to/* <dirpath>

Arguments:
    -h|-?                         - Print this message
    -r                            - Recursive
    -t|--threadct <nthreads>      - Number of copy threads
    -m|--mode <mode>              - Set mode (as in chmod) to octal value
    -u|--uid <uid>                - Specify uid (default is current user's uid)
    -g|--gid <gid>                - Specify uid (default is current user's gid)
    -v|--verbose                  - print debugging output while executing the command
Interleaving Arguments:
    -N|--nstrips <n>              - Number of strips to use in interleaved allocations.
    -B|--nbuckets <n>             - Number of buckets to divide the device into
                                    (nstrips && nbuckets) causes strided
                                    allocation within a single device.
    -C|--chunksize <size>[kKmMgG] - Size of chunks for interleaved allocation
                        (default=2M)

NOTE 1: 'famfs cp' will only overwrite an existing file if it the correct size.
        This makes 'famfs cp' restartable if necessary.
NOTE 2: you need this tool to copy a file into a famfs file system,
        but the standard 'cp' can be used to copy FROM a famfs file system.

```
## famfs creat
```

famfs creat: Create a file in a famfs file system

This tool allocates and creates files.

Create a file backed by free space:
    famfs creat -s <size> <filename>

Create a file containing randomized data from a specific seed:
    famfs creat -s size --randomize --seed <myseed> <filename>

Create a file backed by free space, with octal mode 0644:
    famfs creat -s <size> -m 0644 <filename>

Create two files randomized with separte seeds:
    famfs creat --multi file1,256M,42 --multi file2,256M,43

Create two non-randomized files:
    famfs creat --multi file1,256M --multi file2,256M

Arguments:
    -h|-?                    - Print this message
    -m|--mode <octal-mode>   - Default is 0644
                               Note: mode is ored with ~umask, so the actual mode
                               may be less permissive; see umask for more info
    -u|--uid <int uid>       - Default is caller's uid
    -g|--gid <int gid>       - Default is caller's gid
    -v|--verbose             - Print debugging output while executing the command

Single-file create: (cannot mix with multi-create)
    -s|--size <size>[kKmMgG] - Required file size
    -S|--seed <random-seed>  - Optional seed for randomization
    -r|--randomize           - Optional - will randomize with provided seed

Multi-file create: (cannot mix with single-create)
    -t|--threadct <nthreads> - Thread count in --multi mode
    -M|--multi <fname>,<size>[,<seed>]
                             - This arg can repeat; will create each fiel
                               if non-zero seed specified, will randomize

Interleave arguments:
    -N|--nstrips <n>              - Number of strips to use in interleaved allocations.
    -B|--nbuckets <n>             - Number of buckets to divide the device into
                                    (nstrips && nbuckets) causes strided
                                    allocation within a single device.
    -C|--chunksize <size>[kKmMgG] - Size of chunks for interleaved allocation
                                    (default=256M)

NOTE: the --randomize and --seed arguments are useful for testing; the file is
      randomized based on the seed, making it possible to use the 'famfs verify'
      command later to validate the contents of the file

```
## famfs verify
```

famfs verify: Verify the contents of a file that was created with 'famfs creat':
    famfs verify -S <seed> -f <filename>

Arguments:
    -h|-?                        - Print this message
    -f|--filename <filename>     - Required file path
    -S|--seed <random-seed>      - Required seed for data verification
    -m|--multi <filename>,<seed> - Verify multiple files in parallel
                                   (specify with multiple instances of this arg)
                                   (cannot combine with separate args)
    -t|--threadct <nthreads>     - Thread count in --multi mode

```
## famfs flush
```

famfs flush: Flush or invalidate the processor cache for an entire file

This command is useful for shared memory that is not cache coherent. It should
be called after mutating a file whose mutations need to be visible on other hosts,
and before accessing any file that may have been mutated on other hosts. Note that
logplay also takes care of this, but if the log has not been played since the file
was mutated, this operation may be needed.

    famfs flush [args] <file> [<file> ...]

Arguments:
    -v           - Verbose output
    -h|-?        - Print this message

NOTE: this creates a file system error and is for testing only!!

```
## famfs logplay
```

famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary if files have been added by another node
since the file system was mounted (or since the last logplay)

    famfs logplay [args] <mount_point>

Arguments:
    -n|--dryrun  - Process the log but don't instantiate the files & directories
    -v|--verbose - Verbose output


```
## famfs getmap
```

famfs getmap: check the validity of a famfs file, and optionally get the
mapping info for the file

This command is primarily for testing and validation of a famfs file system

    famfs getmap [args] <filename>

Arguments:
    -q|--quiet - Quiet print output, but exit code confirms whether the
                 file is famfs
    -h|-?      - Print this message

Exit codes:
   0    - The file is a fully-mapped famfs file
   1    - The file is not in a famfs file system
   2    - The file is in a famfs file system, but is not mapped
 EBADF  - invalid input
 ENOENT - file not found
 EISDIR - File is not a regular file

This is similar to the xfs_bmap command and is only used for testing

```
## famfs clone
```

famfs clone: Clone a file within a famfs file system

This administrative command is only useful in testing, and leaves the
file system in cross-linked state. Don't use it unless you want to generate
errors for testing!

Clone a file, creating a second file with the same extent list:
    famfs clone <src_file> <dest_file>

Arguments:
    -h|-?        - Print this message

NOTE: this creates a file system error and is for testing only!!

```
## famfs chkread
```

famfs chkread: verify that the contents of a file match via read and mmap

    famfs chkread <famfs-file>

Arguments:
    -h|-?  - Print this message
    -s     - File is famfs superblock
    -l     - File is famfs log

```
