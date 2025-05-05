#!/bin/sh -ex

if [ $# -lt 1 ]
then
    echo "Usage: $0 <image name> [debug]"
    exit 1
fi

DEVENV_DIR=$(dirname "$0")
DISK_IMG=$1
DEBUG_MODE=$2

if [ ! -f "$DISK_IMG" ]
then
    echo "No such file: $DISK_IMG"
    exit 1
fi

QEMU_ARGS="
    -m 1G
    -drive if=pflash,format=raw,readonly=on,file=$DEVENV_DIR/build/OVMF_CODE.fd
    -drive if=pflash,format=raw,file=$DEVENV_DIR/build/OVMF_VARS.fd
    -drive if=ide,index=0,media=disk,format=raw,file=$DISK_IMG
    -device nec-usb-xhci,id=xhci
    -device usb-mouse -device usb-kbd
    -monitor stdio
"

if [ "$DEBUG_MODE" = "debug" ]; then
    # 백그라운드로 QEMU 실행 + GDB 포트 열기
    qemu-system-x86_64 $QEMU_ARGS -d int,guest_errors -no-reboot -gdb tcp::1234 -S > /dev/null 2>&1 &
    QEMU_PID=$!

    gdb ./build/kernel.elf \
        -ex "target remote :1234" \
        -ex "break KernelMain" \
        -ex "continue"

    kill $QEMU_PID
else
    # 일반 실행 (포그라운드)
    qemu-system-x86_64 $QEMU_ARGS
fi
