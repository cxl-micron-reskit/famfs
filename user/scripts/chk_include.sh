
#!/usr/bin/env bash

kpath=/lib/modules/$(uname -r)
#instpath=$kpath/extra
#famfs=../kmod/famfs.ko

incpath=/usr/include/linux/


#
# If there's an in-tree kmod, make sure /usr/include/linux/famfs_ioctl.h matches it
#
if [ -f "$kpath/kernel/fs/famfs/famfs.ko" ]; then
    echo "famfs is part of the installed kernel; installing kernel famfs_ioctl.h"
    sudo cp $kpath/include/uapi/linux/famfs_ioctl.h $incpath
    exit 0
fi

#
# Otherwise copy famfs_ioctl.h from local kmod directory
#
echo "famfs not part of installed kernel; installing local famfs_ioctl.h"
sudo cp ../kmod/famfs_ioctl.h $incpath
exit 0
