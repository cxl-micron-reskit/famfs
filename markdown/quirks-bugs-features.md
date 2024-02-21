# Famfs Quirks, Bugs and "Features"

Famfs has been developed in 2023-2024, and first made public in early 2024. As of the release,
there are a number of known issues that we plan to address. We try to record them here.

| **Issue**                    | **Notes** |
|------------------------------|-----------|
| Must be root to create files | As of this writing, root permission is necessary to create files (even on a Master). This is easily addressed by two related fixes. The first is to drop dependency on getting the system UUID from dmidecode, which requires root. The second is to make the log writable by non-root users. This should be fixed by May 2024          |
| WARN_ON_ONCE() in fs/dax.c in ```insert_dax_entry()``` | When this occurs with /dev/dax, it is a symptom of a bug in devdax - Pages that are accessed via raw devdax are marked as if they were inserted into its ```inode->i_mapping```, but they were not actually inserted - meaning they can't be cleaned up correctly. But ```insert_dax_entry()``` notices when the famfs superblock is accessed first via raw mmap and then soon afterward via mmap of the fsdax superblock file. The bug here is not in famfs; the underlying dax layer needs fixes. |
| ```df``` does not show famfs | We're working on it...|
| Cache coherency untested | Famfs does not manage cache coherency for apps that share data, but it is intended to manage coherency of its own data structures. This means that the processor cache must be written-back for the superblock and log during ```mkfs.famfs```, and for the log any time the log is appended. In addition, during ```famfs logplay```, the processor cache must be invalidated if necessary to avoid reading stale data from the cache. The superblock and all log related structures are checksummed, so famfs is already equipped to avoid using bogus structures and log entries. When running with VMs sharing memory, these issues are moot because the VMs share the same processor (and therefore the same processor cache). But these issues will be important with actual disaggregated memory. We will update as things develop. |
| Not processor arch independent | The intent is that famfs will manage its metadata in a way that is processor architecture independent, by using XDR transformations when storing and retrieving structures (e.g. the superblock and log). But this is not implemented yet. So it probably only works if all of the systems are the same cpu architecture. (also, we've only tested on x86 so far) |
| Logplay is not automatic | This may be an "actual" feature. If you want a client to notice new files, you need to run a ```famfs logplay``` on that client. This can be turned into an automated feature if there is demand for it. |
| If you handle famfs files incorrectly, accessing those files will fail | This is definitely a "feature", although we will be exploring ways to prevent as many modes of horking famfs files as we can prevent. We're not sure if we can prevent a rogue truncate, or a rogue cp into famfs, but we do the right thing and prevent those invalid files from silently performing I/O. Tell us about your requirements and we'll try to work them into the plan. |



