# famfs-fuse Design Notes

The fuse (Filesystem in USEr space) maintainers have asked for famfs to be ported into fuse.
This requires that famfs-fuse files cache their entire file-to-dax metadata in their fuse/kernel
metadata. As of May 2025, famfs does this. This document is an overview of how famfs-fuse works.

As of May 2025, the famfs user space code works with both standalone and fuse-based famfs.
We will deprecate the standalone after we've proven there are no major performance (or other)
regressions.

## On-Media Format (omf)

The port of the famfs on-media format was simple: Basically nothing changed - or at least nothing
changed to accommodate fuse. (There have been some OMF changes during fuse development, but they are
not directly connected to famfs-fuse functionality.)

## Famfs Fuse Daemon User Space Server

The big new item in the fuse version of famfs is the famfs fuse daemon (```famfs_fused```). This is
a low-level libfuse server. It introduces two new messages and responses into the fuse protocol:
GET_FMAP and GET_DAXDEV. An fmap is the map of a file to dax device ranges, and a daxdev is (obviously(
a dax device that is or may be referenced by files.

Standalone famfs pushes all file metadata into the kernel when the log is played, but that's not how
fuse works. Fuse wants to discover the file system contents and structure through (primarily)
READDIR and LOOKUP commands - much like a conventional standalone file system.

The disconnect is that the famfs metadata exists in the famfs metadata log, which is sequential in
the chronological order in which files and directories were created.
To avoid an O(n^2) order when looking up files, we create the "shadow tree".
The famfs metadata log is played into the shadow tree, and READDIR and LOOKUP retrieve information
from the shadow tree.

The shadow tree is actually a tmpfs file system, and famfs leverages quite a bit from the example
passthrough_ll.c program from libfuse. The shadow tree is a "passed through" ... to a point. Directories
are passed through. With files, the name is passed through but the attributes are stored IN the file,
in YAML format, and decoded upon LOOKUP or OPEN etc. by famfs_fused. This approach probably seems a bit weird,
but it gives us an elegant tree-based structure to play the log into, while giving us efficient
READDIR and LOOKUP.

The shadow-file YAML contains the full "fmap" metadata for each file - placed there by logplay.

## Famfs User Space
This repo has the user space famfs code. This code creates famfs file systems and files, mounts famfs, plays the famfs metadata
log, etc. 

| **Operation** | **Description** |
|-----------|-------------|
| ```mkfs.famfs```| Initializes a superblock and empty log on raw a devdax device. This functions identically for standalone and fuse famfs. |
| ```famfs mount``` | Verifies a valid superblock and log, mounts famfs, creates the meta files (```mkmeta```), and plays the log. The superblock and log operations are identical for standalone and fuse famfs, but the other steps differ. |
| ```famfs logplay``` | Plays the log for a famfs file system. Log access is identical for standalone and fuse famfs, but the actions taken for each log entry are different.|

### ```famfs logplay```
We discuss ```famfs logplay``` first because it's a subset of ```famfs mount```.

```famfs logplay``` operates on a mounted famfs file system. In the standalone mode,
it pushes all files and their metadata into the kernel, such that metadata for the entire
mounted file system is cached in-kernel.

In famfs-fuse, the kernel calls READDIR and LOOKUP to discover what files and directories exist. As a result,
```famfs logplay``` plays the log into "shadow files" in the "shadow tree". As mentioned
above, directories are just passed through; the /existence/ of files is passed through, but the
metadata is decoded from YAML in each shadow file.

Note that once a famfs file system is mounted, its superblock and log are exposed as the files
```.meta/.superblock``` and ```.meta/.log```. So the log is played from a file - from the "front door"
of famfs. But the log is played to the specific shadow file system of the mount in question, which
is accessed by ```famfs_fused```.

We find the shadow file system for a given famfs mount by parsing the mount options in ```/proc/mounts```.
When we mount a famfs file system, we pass in the option ```-o shadow=<path>```, which is exposed in
```/proc/mounts```.

### ```famfs mount```

```famfs mount``` performs the following sequence when running via fuse:

1. Validate the superblock and log via raw mmap of the primary devdax device. Fail if not valid.
2. Create a temporary directory for the shadow path of the mount.
3. Create shadow metadata files for ```.meta/.superblock``` and ```.meta/.log```. Note that these are the only files that are not created BY log entries.
4. Mount the famfs instance by starting its fuse daemon ```famfs_fused``` - which performs the kernel mount.
5. Verify that the meta files are visible via the active mount (NOT the shadow path).
6. Play the famfs log (```famfs logplay```) via the mounted meta files into the shadow file system.

### ```famfs creat``` and ```famfs cp```

Famfs files must be strictly pre-allocated. The supported way of doing this is via ```famfs creat```
and ```famfs cp```.  Each method creates famfs-fuse files through this general sequence:

1. If we don't already have the allocation bitmap, read the log (through .meta/.log in the active mount) to generate the bitmap.
2. Allocate sufficient memory to back the file.
3. Append the log with an entry that "commits" the existence of the file.
4. Create the shadow file, the same as it would exist if it were created in response to a logplay.

## Kernel Space

- The kernel config parameter CONFIG_FUSE_FAMFS_DAX is added, controlling compilation of the ability to handle famfs files
- The new flag FUSE_DAX_FMAP advertises the kernel's famfs capability to user space / libfuse.
- If user space requests the FUSE_DAX_FMAP capability, active files will have their fmaps requested via the new GET_FMAP message / response.
- For any fmap that references a previously-unknown dax device, the new GET_DAXDEV message / response will be used to retrieve the daxdev.
