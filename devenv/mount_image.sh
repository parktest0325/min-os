#!/bin/sh -ex

if [ $# -lt 2 ]
then
    echo "Usage: $0 <image name> <mount point>"
    exit 1
fi

DEVENV_DIR=$(dirname "$0")
DISK_IMG=$1
MOUNT_POINT=$2

if [ ! -f $DISK_IMG ]
then
    echo "No such file: $DISK_IMG"
    exit 1
fi

mkdir -p $MOUNT_POINT

OS_TYPE=$(uname)

if [ "$OS_TYPE" = "Darwin" ]; then
    DEVICE=$(hdiutil attach -nomount "$DISK_IMG" | tail -n1 | awk '{print $1}')
    if [ -z "$DEVICE" ]; then
        echo "Failed to attach disk image via hdiutil."
        exit 1
    fi
    sudo mount -t msdos "$DEVICE" "$MOUNT_POINT"
    echo "$DEVICE" > "$DEVENV_DIR/.device_name.tmp"
    # diskutil list 로 잘 마운트됐는지 확인가능 
else
    sudo mount -o loop "$DISK_IMG" "$MOUNT_POINT"
fi
