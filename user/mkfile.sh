#!/usr/bin/bash
#
sudo debug/mkfile -F /dev/pmem0 -n 2 -o 0x600000 -l 0x200000 -o 0x200000 -l 0x200000  -f /mnt/tagfs/frog1
