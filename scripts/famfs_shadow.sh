#!/usr/bin/env bash
# Usage: famfs_shadow.sh /mnt/famfs
#
# Returns the shadow path for a famfs mount point via xattr.

mp="$1"
if [[ -z "$mp" ]]; then
  echo "Usage: $0 <mountpoint>" >&2
  exit 1
fi

getfattr -n user.famfs.shadow --only-values "$mp" 2>/dev/null
