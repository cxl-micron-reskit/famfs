# famfs-fuse Design Notes

The fuse (Filesystem in USEr space) maintainers have expressed support for putting sufficient
capabilities into fuse to suppoer famfs as a fuse file system. This document is starting out
as a list of famfs adaptations that will support an attempt to port famfs to fuse.

The idea is as follows:

- Adapt fuse to support a file type that sets the S_DAX flag, caches the famfs dax extent
  list in the kernel, and uses the dev_dax_iomap capabilities to allow famfs files to be
  proper fs-dax files for which read/write/fault handling takes place without upcalls.
- Adapt the famfs user space to do the following:
  - Play the log into a "shadow file system" as described below
  - Provide a famfs-fused daemon, based on a modified version of a fuse pass-through server,
    that uses the fuse_dax_iomap capabilities to provide famfs capabilities via fuse, using the 
    shadow file system as the source metadata.
  - The shadow file system may be periodically update via ```famfs logplay```, which will
    update the contents of the famfs-fuse file system.

## Pass-through Fuse Filesystem

Fuse supports pass-through file systems, and has an example pass-through file system.
We need to bring up the pass-through file system - initially unmodified.

## Super-High-Level Architecture

Add a ```famfs logplay``` option taht plays a famfs log into a regular file system, with
the following qualifications:

- Directories are created normally
- Files are created at the appropriate relative paths, but the contents of each file are YAML
  that describes the metadata of the file (dax extent list, owner, group, permissions, etc.).
  That's basically a YAML version of a famfs log entry.

Step 0 is to expose this file system via fuse pass-through. So at step 0, the YAML would be
exposed via a fuse mount.

Step N is to expose this file system via fuse pass-through, but instead of exposing the YAML
file contents, we will expose fs-dax files that /apply/ the metadata such that accessing a file
references the backing memory for the file.

See Patching Fuse below

## famfs logplay

```famfs logplay``` needs a new option (```--shadow <path>```) that plays a famfs log into a regular 
filesystem (which might be any filesystem other than famfs). Directories are created normally,
But files contain a YAML-translation of the log's description of the file, such as this:

```
file:
  path: /path/to/file
  owner: <uid>
  group: <gid>
  permissions: '0755'
  extents:
    - device_uuid: '123e4567-e89b-12d3-a456-426614174000'
      offset: 0
      length: 2097152
    - device_uuid: '123e4567-e89b-12d3-a456-426614174000'
      offset: 4194304
      length: 8388608
    - device_uuid: '123e4567-e89b-12d3-a456-426614174001'
      offset: 16777216
      length: 33554432
```

Note that devices don't have UUIDs yet. This is an OMF change (on-media-format)
and thus needs to be pushed cautiously. Also: CXL DCDs provide a UUID for each 
allocation, meaning there will be a DAX device that can be found via its UUID.

A reasonable approach would involve the following (none of which has to be done 
immediately):

- Add a device uuid to the superblock
- If the the dax device is a DCD allocation (i.e. tagged capacity), the tag
  will be used as the device uuid. Otherwise generate a UUID.
- In the future, when we support famfs instances that span multiple DAX devices,
  each will have a superblock that includes the file system UUID and the specific
  device UUID, but only one of the superblocks will be the /primary/ superblock.
- Additional device UUIDs will be added to a file system via a new type of log
  entry that indicates that a new device UUID is now part of the file system.

## Patching Fuse

The patching of fuse to support famfs fs-dax files is TBD...
