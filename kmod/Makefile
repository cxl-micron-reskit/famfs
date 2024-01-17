# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

# Get the directory of the Makefile
MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

EXTRA_CFLAGS += -g -DDEBUG -O1 -DK67
#EXTRA_CFLAGS += -g -DDEBUG -O1
EXTRA_CFLAGS += -DFAMFS_TRACE_PATH=$(MAKEFILE_DIR) # required for famfs_trace.h

obj-m += famfs.o

famfs-objs += famfs_inode.o famfs_file.o famfs_trace.o

KHOME := /lib/modules/$(shell uname -r)/build
#KHOME := ~/w/linux-6.6

clean:
	make -k -C $(KHOME) M=$(PWD) clean

all:
	make -k -C $(KHOME) M=$(PWD) modules

# Put kmod in a kernel tree for gdb debugging
gdb:
	mkdir -p $(KHOME)/fs/famfs
	cp famfs.ko $(KHOME)/fs/famfs/

