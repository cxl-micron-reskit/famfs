#!/usr/bin/env python3
"""
mmap_shared_private.py - verify famfs mmap write-through vs copy-on-write.

Given an existing famfs file, prove the two mmap semantics that matter for a
shared-memory filesystem:

  * a MAP_SHARED  store (mmap ACCESS_WRITE) DOES modify the file's FAM
    contents (write-through), and
  * a MAP_PRIVATE store (mmap ACCESS_COPY)  does NOT -- it is copy-on-write:
    the store is visible only in the writer's own mapping and never reaches
    fabric-attached memory.

The authoritative "file contents" here is a pread(2) syscall. On a famfs DAX
file, read() goes through dax_iomap_rw() and reads straight from FAM, bypassing
any mapping -- so it is an independent ground truth for what actually landed in
the device memory.

Requirements:
  - The file must be an already-mapped famfs file (created with e.g.
    `famfs creat`), at least offset+length bytes long.
  - The mount must permit writes (FAMFS_OPT_WRITE), or the MAP_SHARED store
    will be refused -- that failure is reported distinctly.
  - Run with enough privilege to open the file O_RDWR (usually root/sudo,
    since famfs files are typically root-owned).

Exit status: 0 = PASS, non-zero = FAIL.

Usage: mmap_shared_private.py <famfs-file> [offset length]
  offset and length default to 0 and 0x1000 (a PTE-sized region). Pass a
  2 MiB-aligned offset with length 0x200000 to exercise the PMD path. Both
  accept decimal or 0x-hex; offset must be a multiple of the page size.
"""

import mmap
import os
import sys

DEFAULT_OFF = 0        # page-aligned offset into the file to exercise
DEFAULT_LEN = 0x1000   # bytes to stamp / compare (one page; 0x200000 = PMD)


def die(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def file_bytes(fd, off, n):
    """Authoritative file contents: a syscall read, not through any mapping."""
    buf = os.pread(fd, n, off)
    if len(buf) != n:
        die(f"short pread ({len(buf)}/{n} bytes) at offset {off:#x}; "
            f"is the file at least {off + n:#x} bytes?")
    return buf


def main():
    argv = sys.argv
    if len(argv) not in (2, 4):
        sys.exit(f"usage: {argv[0]} <famfs-file> [offset length]")
    path = argv[1]
    try:
        off = int(argv[2], 0) if len(argv) == 4 else DEFAULT_OFF
        length = int(argv[3], 0) if len(argv) == 4 else DEFAULT_LEN
    except ValueError:
        sys.exit("offset and length must be integers (decimal or 0xhex)")
    if length <= 0:
        sys.exit("length must be > 0")
    if off % mmap.ALLOCATIONGRANULARITY:
        sys.exit(f"offset {off:#x} must be a multiple of the page size "
                 f"({mmap.ALLOCATIONGRANULARITY:#x})")

    print(f"# {path}: offset={off:#x} length={length:#x}")
    fd = os.open(path, os.O_RDWR)
    try:
        orig = file_bytes(fd, off, length)
        # Two distinct patterns, both guaranteed != orig and != each other.
        shared_pat = bytes(b ^ 0xAA for b in orig)
        priv_pat = bytes(b ^ 0x55 for b in orig)

        # ---- MAP_SHARED: store must reach FAM (write-through) ----------------
        try:
            m = mmap.mmap(fd, length, access=mmap.ACCESS_WRITE, offset=off)
        except PermissionError:
            die("MAP_SHARED write mapping refused (EPERM) -- is FAMFS_OPT_WRITE "
                "enabled on this mount?")
        try:
            m[:] = shared_pat
            m.flush()                       # msync(MS_SYNC)
        finally:
            m.close()

        if file_bytes(fd, off, length) != shared_pat:
            die("MAP_SHARED store did not reach the file (no write-through)")
        print("PASS: MAP_SHARED store modified the file")

        # ---- MAP_PRIVATE: store must be copy-on-write (file unchanged) -------
        m = mmap.mmap(fd, length, access=mmap.ACCESS_COPY, offset=off)
        try:
            if m[:] != shared_pat:
                die("MAP_PRIVATE mapping did not read current FAM contents "
                    "(private read should still see the file's data)")
            m[:] = priv_pat
            if m[:] != priv_pat:
                die("MAP_PRIVATE store not visible in its own mapping")
        finally:
            m.close()

        after = file_bytes(fd, off, length)
        if after == priv_pat:
            die("MAP_PRIVATE store LEAKED into the file (write-through bug!)")
        if after != shared_pat:
            die("file contents changed unexpectedly across the private mapping")
        print("PASS: MAP_PRIVATE store did not modify the file")

    finally:
        os.close(fd)

    print("PASS: famfs mmap shared/private semantics are correct")


if __name__ == "__main__":
    main()
