# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#



obj-m += tagfs.o

tagfs-objs += inode.o file-mmu.o

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
