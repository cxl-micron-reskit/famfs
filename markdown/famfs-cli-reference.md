
# Famfs mkfs

```
Create a famfs file system:
    debug/mkfs.famfs [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments
    -h|-?      - Print this message
    -f|--force - Will create the file system even if there is allready a superblock
    -k|--kill  - Will 'kill' the superblock (also requires -f)
```
# Famfs cli
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
	verify
	mkmeta
	logplay
	getmap
	clone
```
## Famfs mount
```
famfs mount: mount a famfs file system and make it ready to use

We recommend using the 'famfs mount' command rather than the native system mount
command, because there are additional steps necessary to make a famfs file system
ready to use after the system mount (see mkmeta and logplay). This command takes
care of the whole job.

 mount <memdevice> <mountpoint>

Arguments:
    -?             - Print this message
    -r             - Re-mount
    -v|--verbose   - Print verbose output

```
## Famfs fsck
```

famfs fsck: check a famfs file system

This command checks the validity of the superblock and log, and scans the
superblock for cross-linked files.

Check an unmounted famfs file system
    famfs fsck [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0
Check a mounted famfs file system:
    famfs [args] <mount point>

Arguments:
    -?           - Print this message
    -m|--mmap    - Access the superblock and log via mmap
    -h|--human   - Print sizes in a human-friendly form
    -v|--verbose - Print debugging output while executing the command

Exit codes:
  0  - No errors were found
 !=0 - Errors were found

```
## famfs check
```

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

    famfs check [args] <mount point>

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

```
## Famfs mkdir
```
famfs mkdir: Create a directory in a famfs file system:

    famfs mkdir [args] <dirname>


Arguments:
    -?               - Print this message
    -p|--parents     - No error if existing, make parent directories as needed,
                       the -m option only applies to dirs actually created
    -m|--mode=<mode> - Set mode (as in chmod) to octal value
    -u|--uid=<uid>   - Specify uid (default is current user's uid)
    -g|--gid=<gid>   - Specify uid (default is current user's gid)
    -v|--verbose     - Print debugging output while executing the command

```

## Famfs cp
```
famfs cp: Copy one or more files and directories into a famfs file system

Copy a file into a famfs file system
    famfs cp [args] <srcfile> <destfile> # destfile must not already exist

Copy a file into a directory of a famfs file system with the same basename
    famfs cp [args] <srcfile> <dirpath>

Copy a wildcard set of files to a directory
    famfs cp [args]/path/to/* <dirpath>

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

```
## Famfs creat
```
famfs creat: Create a file in a famfs file system

This testing tool allocates and creates a file of a specified size.

Create a file backed by free space:
    famfs creat -s <size> <filename>

Create a file containing randomized data from a specific seed:
    famfs creat -s size --randomize --seed <myseed> <filename>
Create a file backed by free space, with octal mode 0644:
    famfs creat -s <size> -m 0644 <filename>

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

```
## Famfs verify
```
famfs verify: Verify the contents of a file that was created with 'famfs creat':
    famfs verify -S <seed> -f <filename>

Arguments:
    -?                        - Print this message
    -f|--fillename <filename> - Required file path
    -S|--seed <random-seed>   - Required seed for data verification

```
## Famfs mkmeta
```
famfs mkmeta:

The famfs file system exposes its superblock and log to its userspace components
as files. After telling the linux kernel to mount a famfs file system, you need
to run 'famfs mkmeta' in order to expose the critical metadata, and then run
'famfs logplay' to play the log. Files will not be visible until these steps
have been performed.

    famfs mkmeta <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments:
    -?           - Print this message

```
## Famfs logplay
```
famfs logplay: Play the log of a mounted famfs file system

This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files

    famfs logplay [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories

```
## Famfs getmap
```
+ famfs getmap '-?'

famfs getmap: check the validity ofa famfs file, and optionally get the
mapping info for the file

This command is primarily for testing and validation of a famfs file system

    famfs getmap [args] <filename>

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

```
## Famfs clone
```
famfs clone: Clone a file within a famfs file system

This administrative command is only useful in testing, and leaves the
file system in cross-linked state. Don't use it unless yu want to generate
errors for testing!

Clone a file, creating a second file with the same extent list:
    famfs clone <src_file> <dest_file>

Arguments:
    -?           - Print this message

NOTE: this creates a file system error and is for testing only!!

```
