#!/bin/sh -ex

if [ $# -lt 1 ]
then
    echo "Usage: $0 <image name>"
    exit 1
fi

DEVENV_DIR=$(dirname "$0")
DISK_IMG=$1

if [ ! -f $DISK_IMG ]
then
    echo "No such file: $DISK_IMG"
    exit 1
fi

qemu-system-x86_64 \
    -m 1G \
    -drive if=pflash,format=raw,readonly=on,file=$DEVENV_DIR/build/OVMF_CODE.fd \
    -drive if=pflash,format=raw,file=$DEVENV_DIR/build/OVMF_VARS.fd \
    -drive if=ide,index=0,media=disk,format=raw,file=$DISK_IMG \
    -device nec-usb-xhci,id=xhci \
    -device usb-mouse -device usb-kbd \
    -monitor stdio \
    $QEMU_OPTS
    #-d int,guest_errors -no-reboot \
    #-gdb tcp::1234 \
    #-S \
