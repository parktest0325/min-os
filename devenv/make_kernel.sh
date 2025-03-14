#!/bin/sh -ex

DEVENV_DIR=$(dirname "$0")
KERNEL_DIR=$DEVENV_DIR/../kernel/
BUILD_DIR=$DEVENV_DIR/build/
TARGET=kernel.elf

make -C $KERNEL_DIR re
mkdir -p $BUILD_DIR
mv "${KERNEL_DIR}${TARGET}" "${BUILD_DIR}${TARGET}"
make -C $KERNEL_DIR fclean
