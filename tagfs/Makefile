# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#



obj-m += tagfs.o

tagfs-objs += tagfs_inode.o tagfs_file.o

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
