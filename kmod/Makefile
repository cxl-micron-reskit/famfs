# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux ramfs routines.
#

EXTRA_CFLAGS += -g -DDEBUG -O1
#EXTRA_CFLAGS += -DCONFIG_TAGFS_CHAR_DAX=y

obj-m += tagfs.o

tagfs-objs += tagfs_inode.o tagfs_file.o

KHOME = ~/w/linux-debug

clean:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

all:
	make -k -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	cp tagfs.ko $(KHOME)/fs/tagfs/

# jg: the line commented out above puts tagfs.ko in the kernel build tree
# so gdb can find it. This is only needed if you're running a VM under gdb,
# but you need to set the right KHOME


# jg: this is not working. there is some experimental code that tries
# (unsuccessfully) to use a /dex/daxm.n device rather than a /dev/pmem
# device, and enabling that code requires the defnition of the
# CONFIG_TAGFS_CHAR_DAX macro, plus a kernel patch.
#
# The attempt below to conditionally define that macro does not work.
# Leaving it in for the moment as a reminder.
#
chardax:	EXTRA_CFLAGS += -DCONFIG_TAGFS_CHAR_DAX=y
chardax:	all

