#!/usr/bin/env bash

verbose=0
while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
	(-v|--verbose)
	    verbose=1
	    ;;
	*)
	    echo "Unrecognized argument $flag"
	    ;;

    esac
done
errs=0

DISTRO=
# Check if /etc/os-release exists
if [ -f /etc/os-release ]; then
    source /etc/os-release

    # Check distribution and output result
    case $ID in
        ubuntu)
	    DISTRO="ubuntu"
            echo "This system is Ubuntu."
            ;;
        fedora)
	    DISTRO="fedora"
            echo "This system is Fedora."
            ;;
        *)
            echo "This system is neither Ubuntu nor Fedora. It is identified as $ID."
            ;;
    esac
else
    echo "/etc/os-release file not found. Unable to determine the distribution."
fi


D=/sys/firmware/efi
if [ ! -d "$D" ]; then
    (( errs++ ))
    echo "$D not found; probably not efi"
fi
if (( verbose > 0)); then
    echo "$D:"
    ls -al $D
fi
D="$D/efivars"
if [ ! -d "$D" ]; then
    (( errs++ ))
    echo "$F not found; probably nof efi"
fi
if (( verbose > 0)); then
    echo "$D:"
    ls -al $D
fi

D=/boot/efi/EFI
if [ ! -d "$D" ]; then
    (( errs++ ))
    echo "$D not found; probably not efi"
fi
if (( verbose > 0)); then
    echo "$D:"
    ls -al $D
fi
D=/boot/efi/EFI/BOOT
if [ ! -d "$D" ]; then
    (( errs++ ))
    echo "$D not found; probably not efi"
fi
if (( verbose > 0)); then
    echo "$D:"
    ls -al $D
fi

#
# Fedora specific:
#
D=/boot/efi/EFI/$DISTRO
if [ ! -d "$D" ]; then
    (( errs++ ))
    echo "$D not found; probably not efi"
fi
if (( verbose > 0)); then
    echo "$D:"
    ls -al $D
fi
F="$D/grub.cfg"
if [ ! -f "$F" ]; then
    (( errs++ ))
    echo "$F not found; probably nof efi"
fi

if ((errs == 0)); then
    echo "looks like efi to me"
    exit;
fi

echo "Probably not efi; errs=$errs"

