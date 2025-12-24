#!/bin/sh -ex

. ./buildenv.sh 

DEVENV_DIR=$(dirname "$0")

# 기본값
DEBUG_MODE=0
EFI_FILE=""
ANOTHER_FILE=""

# 옵션/인자 파싱
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--debug)
      DEBUG_MODE="debug"
      shift
      ;;
    -*)
      echo "Unknown option: $1"
      exit 1
      ;;
    *)
      if [ -z "$EFI_FILE" ]; then
        EFI_FILE="$1"
      elif [ -z "$ANOTHER_FILE" ]; then
        ANOTHER_FILE="$1"
      else
        echo "Unexpected argument: $1"
        exit 1
      fi
      shift
      ;;
  esac
done

# 기본값 설정
[ -z "$EFI_FILE" ] && EFI_FILE=./build/Loader.efi
DISK_IMG=./build/disk.img
MOUNT_POINT=./mnt
APPS_DIR=../apps
APPS_DEST_DIR=apps

# 실행
$DEVENV_DIR/build-kernel.sh "$DEBUG_MODE"
$DEVENV_DIR/make_image.sh "$DISK_IMG" "$MOUNT_POINT" "$EFI_FILE" "$APPS_DIR" "$APPS_DEST_DIR" "$ANOTHER_FILE"
$DEVENV_DIR/start_qemu.sh "$DISK_IMG" "$DEBUG_MODE"
