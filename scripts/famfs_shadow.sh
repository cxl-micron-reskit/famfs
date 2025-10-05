#!/usr/bin/env bash
# Usage: get_famfs_shadow.sh /mnt/famfs

mp="$1"
if [[ -z "$mp" ]]; then
  echo "Usage: $0 <mountpoint>" >&2
  exit 1
fi

awk -v mp="$mp" '
BEGIN {
  # /proc/mounts escapes spaces/tabs/newlines/backslashes; mirror that for comparison
  gsub(/\\/,"\\\\", mp); gsub(/ /,"\\040", mp); gsub(/\t/,"\\011", mp); gsub(/\n/,"\\012", mp)
}
$2 == mp {
  n = split($4, opt, ",")
  for (i = 1; i <= n; i++) {
    if (opt[i] ~ /^shadow=/) { sub(/^shadow=/, "", opt[i]); print opt[i]; exit 0 }
  }
  exit 1  # mount found but no shadow=
}
END { exit 1 }  # mount not found
' /proc/mounts


