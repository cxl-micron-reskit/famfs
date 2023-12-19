
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
famfs [global_args] <command> [args]

Global args:
	--dryrun
Commands:
	fsck
	mkdir
	cp
	creat
	verify
	mkmeta
	logplay
	getmap
	clone
```
## Famfs fsck
```
Check an unmounted famfs file system
    famfs [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0
Check a mounted famfs file system:
    famfs [args] <mount point>

Arguments:
    -?           - Print this message
    -m|--mmap    - Access the superblock and log via mmap
    -h|--human   - Print sizes in a human-friendly form
    -v|--verbose - Print debugging output while executing the command
```
## Famfs mkdir
```
Create a directory in a famfs file system:
    famfs <dirname>


(the mkdir will be logged
Wishlist: 'mkdir -p' is not implemented yet
```

## Famfs cp
```
Copy a file into a famfs file system
    famfs cp [args] <srcfile> <destfile>
Copy a file into a directory of a famfs file system with the same basename
    famfs cp [args] <srcfile> <famfs_dir>
Copy a wildcard set of files to a directory
    famfs cp [args]/path/to/* <dirpath>

Arguments
    -h|-?      - Print this message
    -v|verbose - print debugging output while executing the command

NOTE: you need this tool to copy a file into a famfs file system,
but the standard 'cp' can be used to copy FROM a famfs file system.
```
## Famfs creat
```
This testing tool allocates a file and optionally fills it with seeded data
that can be verified later

Create a file backed by free space:
    famfs -s <size> <filename>


Create a file containing randomized data from a specific seed:
    famfs -s size --randomize --seed <myseed> <filename>Create a file backed by free space, with octal mode 0644:
    famfs -s <size> -m 0644 <filename>

Arguments:
    -?                       - Print this message
    -s|--size <size>[kKmMgG] - Required file size
    -S|--seed <random-seed>  - Optional seed for randomization
    -r|--randomize           - Optional - will randomize with provided seed
    -m|--mode <octal-mode>   - Default is 0644
    -u|--uid <int uid>       - Default is caller's uid
    -g|--gid <int gid>       - Default is caller's gid
    -v|--verbose             - Print debugging output while executing the command
```
## Famfs verify
```
Verify the contents of a file that was created with 'famfs creat':
    famfs -S <seed> -f <filename>

Arguments:
    -?                        - Print this message
    -f|--fillename <filename> - Required file path
    -S|--seed <random-seed>   - Required seed for data verification
```
## Famfs mkmeta
```
Expose the meta files of a famfs file system
This administrative command is necessary after performing a mount
    famfs <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0

Arguments:
    -?           - Print this message
```
## Famfs logplay
```

Play the log into a famfs file system
This administrative command is necessary after mounting a famfs file system
and performing a 'famfs mkmeta' to instantiate all logged files
    famfs [args] <mount_point>

Arguments:
    -r|--read   - Get the superblock and log via posix read
    -m--mmap    - Get the log via mmap
    -c|--client - force "client mode" (all files read-only)
    -n|--dryrun - Process the log but don't instantiate the files & directories
```
## Famfs getmap
```

This administrative command gets the allocation map of a file:
    famfs [args] <filename>

Arguments:
    -?           - Print this message

This is similar to the xfs_bmap command and is only used for testing
```
## Famfs clone
```

This administrative command is only useful in testing, and leaves the
file system in cross-linked state. Don't use it!

Clone a file, creating a second file with the same extent list:
    famfs <src_file> <dest_file>

Arguments:
    -?           - Print this message

NOTE: this creates a file system error and is for testing only!!

```
