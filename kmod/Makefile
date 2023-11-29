# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

EXTRA_CFLAGS += -g -DDEBUG -O1
EXTRA_CFLAGS += -DCONFIG_FAMFS_CHAR_DAX=y

obj-m += famfs.o

famfs-objs += famfs_inode.o famfs_file.o

KHOME = ~/w/linux-debug

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# Put kmod in a kernel tree for gdb debugging
gdb:
	mkdir -p $(KHOME)/fs/famfs
	cp famfs.ko $(KHOME)/fs/famfs/

