
INC="/usr/include/linux/famfs_ioctl.h"
if [ ! -f $INC ]; then
    echo "$INC not found"
    echo "You need to install a famfs kernel"
    exit -1
fi
exit 0
