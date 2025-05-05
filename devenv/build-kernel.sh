#!/bin/sh -ex

DEVENV_DIR=$(dirname "$0")
KERNEL_DIR=$DEVENV_DIR/../kernel/
BUILD_DIR=$DEVENV_DIR/build/
TARGET=kernel.elf

if [ "$1" = "debug" ]; then
  make -C $KERNEL_DIR DEBUG=1 re
else
  make -C $KERNEL_DIR re
fi
mkdir -p $BUILD_DIR
mv "${KERNEL_DIR}${TARGET}" "${BUILD_DIR}${TARGET}"
make -C $KERNEL_DIR fclean
