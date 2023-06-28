# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

EXTRA_CFLAGS += -g -DDEBUG -O1

obj-m += tagfs.o

tagfs-objs += tagfs_inode.o tagfs_file.o

KHOME = ~/w/linux-debug

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	cp tagfs.ko $(KHOME)/fs/tagfs/

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
