#!/bin/sh -ex

if [ $# -lt 3 ]
then
    echo "Usage: $0 <image name> <mount point> <.efi file> [another file]"
    exit 1
fi

DEVENV_DIR=$(dirname "$0")
DISK_IMG=$1
MOUNT_POINT=$2
EFI_FILE=$3
APPS_DIR=$4
APPS_DEST_DIR=$5
RESOURCE_DIR=$6
ANOTHER_FILE=$7
KERNEL_FILE=${DEVENV_DIR}/build/kernel.elf


if [ ! -f $EFI_FILE ]
then
    echo "No such file: $EFI_FILE"
    exit 1
fi

rm -f $DISK_IMG

OS_TYPE=$(uname)

qemu-img create -f raw $DISK_IMG 200M
mkfs.fat -n 'MIN OS' -s 2 -f 2 -R 32 -F 32 $DISK_IMG

$DEVENV_DIR/mount_image.sh $DISK_IMG $MOUNT_POINT
sudo mkdir -p $MOUNT_POINT/EFI/BOOT
sudo mkdir -p $MOUNT_POINT/$APPS_DEST_DIR
sudo mkdir -p $DEVENV_DIR/build/apps/
sudo cp $EFI_FILE $MOUNT_POINT/EFI/BOOT/BOOTX64.EFI
sudo cp $KERNEL_FILE $MOUNT_POINT/kernel.elf

# 모든 앱 빌드 및 볼륨이미지에 복사
for app_dir in $APPS_DIR/*; do
    if [ -d "$app_dir" ]; then
        make -C "$app_dir"
        app_name=$(basename "$app_dir")
        binary_path="$app_dir/$app_name"
        if [ -f "$binary_path" ]; then
            sudo cp "$binary_path" "$MOUNT_POINT/$APPS_DEST_DIR/$app_name"
            sudo cp "$binary_path" "$DEVENV_DIR/build/apps/$app_name"
        fi
    fi
done

if [ "$RESOURCE_DIR" != "" ]
then
    sudo cp -r $RESOURCE_DIR $MOUNT_POINT/
fi

if [ "$ANOTHER_FILE" != "" ]
then
    sudo cp $ANOTHER_FILE $MOUNT_POINT/
fi
sleep 0.5

sudo umount $MOUNT_POINT

if [ -f "$DEVENV_DIR/.device_name.tmp" ]; then
    DEVICE=$(cat "$DEVENV_DIR/.device_name.tmp")
    sudo hdiutil detach "$DEVICE"
    rm -f "$DEVENV_DIR/.device_name.tmp"
fi
