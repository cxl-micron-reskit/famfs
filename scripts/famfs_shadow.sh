#!/usr/bin/env bash
# Usage: famfs_shadow.sh /mnt/famfs
#
# Returns the shadow path for a famfs mount point via xattr.

if ! command -v getfattr &>/dev/null; then
  echo "Error: getfattr not found. Please install attr package:" >&2
  echo "  Fedora/RHEL: sudo dnf install attr" >&2
  echo "  Ubuntu/Debian: sudo apt install attr" >&2
  exit 1
fi

mp="$1"
if [[ -z "$mp" ]]; then
  echo "Usage: $0 <mountpoint>" >&2
  exit 1
fi

getfattr -n user.famfs.shadow --only-values "$mp" 2>/dev/null
