# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

EXTRA_CFLAGS += -g -DDEBUG -O1

obj-m += tagfs.o

tagfs-objs += tagfs_inode.o tagfs_file.o

KHOME = ~/w/linux-debug

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	cp tagfs.ko $(KHOME)/fs/tagfs/

#
# This is a little hinky, but it works for now. The chardax build
# requires some kernel patches that the "normal" build can do without
# But if you've built one way, the objects will still be used unless
# you do a 'make clean' before doing the other build type (all/chardax)
#
chardax: EXTRA_CFLAGS += -DCONFIG_TAGFS_CHAR_DAX=y
chardax:
	make all
