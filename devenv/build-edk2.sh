#!/bin/bash -ex

if [ ! -d edk2 ]
then
  git clone https://github.com/tianocore/edk2.git
fi

cd edk2

make -C ./BaseTools/Source/C

# Setting Build target
TARGET_ARCH=X64
TARGET=DEBUG
BUILD_FOLDER=../build

if [ "$1" == "ovmf" ]; then
  PLATFORM=Ovmf
  CP_TARGET="FV/OVMF_*.fd"
  ACTIVE_PLATFORM="${PLATFORM}Pkg/${PLATFORM}Pkg${TARGET_ARCH}.dsc"
else
  PLATFORM=MinLoader
  CP_TARGET="${TARGET_ARCH}/Loader.*"

  if [ ! -d "./${PLATFORM}Pkg" ]; then
    ln -s "../../bootloader/${PLATFORM}Pkg" ./
  fi
  ACTIVE_PLATFORM="${PLATFORM}Pkg/${PLATFORM}Pkg.dsc"
fi

OS_TYPE=$(uname)
if [ "$1" = "ovmf" ] && [ "$OS_TYPE" = "Darwin" ]; then
  TOOL_CHAIN_TAG=XCODE5
elif [ "$OS_TYPE" = "Darwin" ]; then
  TOOL_CHAIN_TAG=CLANGDWARF
else
  TOOL_CHAIN_TAG=CLANG38
fi


set --
source ./edksetup.sh

if sed --version >/dev/null 2>&1; then
  # GNU sed (Linux)
  BAK_FILE=""
else
  # BSD sed (macOS)
  BAK_FILE="''"
fi

# Configure target.txt
sed -i ${BAK_FILE} "/ACTIVE_PLATFORM/ s:= .*$:= ${ACTIVE_PLATFORM}:" Conf/target.txt
sed -i ${BAK_FILE} "/TARGET_ARCH / s:= .*$:= ${TARGET_ARCH}:" Conf/target.txt
sed -i ${BAK_FILE} "/TARGET / s:= .*$:= ${TARGET}:" Conf/target.txt
sed -i ${BAK_FILE} "/TOOL_CHAIN_TAG/ s:= .*$:= ${TOOL_CHAIN_TAG}:" Conf/target.txt

sed -i ${BAK_FILE} '/CLANG38/ s/-flto//' Conf/tools_def.txt
sed -i ${BAK_FILE} '/MTOC_PATH/ s:= .*$:= /opt/homebrew/bin/mtoc:' Conf/tools_def.txt

rm -rf Build/${PLATFORM}${TARGET_ARCH}/${TARGET}_${TOOL_CHAIN_TAG}/
build



# Copy build file
mkdir -p ${BUILD_FOLDER}
cp Build/${PLATFORM}${TARGET_ARCH}/${TARGET}_${TOOL_CHAIN_TAG}/${CP_TARGET} ${BUILD_FOLDER}
