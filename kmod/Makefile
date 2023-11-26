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

# jg: the line commented out above puts famfs.ko in the kernel build tree
# so gdb can find it. This is only needed if you're running a VM under gdb,
# but you need to set the right KHOME


# jg: this is not working. there is some experimental code that tries
# (unsuccessfully) to use a /dex/daxm.n device rather than a /dev/pmem
# device, and enabling that code requires the defnition of the
# CONFIG_FAMFS_CHAR_DAX macro, plus a kernel patch.
#
# The attempt below to conditionally define that macro does not work.
# Leaving it in for the moment as a reminder.
#
chardax:	EXTRA_CFLAGS += -DCONFIG_FAMFS_CHAR_DAX=y
chardax:	all

