# min-os

## Dev Setting for Mac
https://dongkim.dev/posts/project/archive/system/min_os/1_build_setting/#%ed%8c%a8%ed%82%a4%ec%a7%80-%eb%b9%8c%eb%93%9c
```sh
git clone git@github.com:parktest0325/min-os.git
git clone https://github.com/uchan-nos/mikanos.git

cd min-os/setup/
./setup_for_mac.sh
```

#### build UEFI and run_qemu
```sh
cd ../devenv/edk2 
source ./edksetup.sh
ln -s ~/Project/mikanos/MikanLoaderPkg ./
build -p MikanLoaderPkg/MikanLoaderPkg.dsc -a X64 -t CLANGDWARF -b RELEASE
cp Build/MikanLoaderX64/RELEASE_CLANGDWARF/X64/Loader.efi ~/Project/

build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -t XCODE5 -b RELEASE
cp Build/OvmfX64/RELEASE_XCODE5/FV/OVMF_* ../

cd ~/Project/edk2
./run_qemu.sh ../../Loader.efi
```

#### If RegisterFilterLib is not found occurs when building the MikanLoaderPkg
```sh
(...): error 4000: Instance of library class [RegisterFilterLib] is not found

$ find . -name "RegisterFilterLib*.inf"
./MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf

$ vi MikanLoaderPkg/MikanLoaderPkg.dsc
[LibraryClasses]
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
```

#### If error occurs when building OVMF
```sh
which mtoc
/opt/homebrew/bin/mtoc

vi Conf/tools_def.txt
*_XCODE5_*_MTOC_PATH      = /opt/homebrew/bin/mtoc
```

```sh
vi Conf/tools_def.txt
RELEASE_XCODE5_X64_CC_FLAGS   = ... -Wno-unused-but-set-variable -Wno-unused-but-set-parameter

vi CryptoPkg/Library/OpensslLib/OpensslLibCrypto.inf
XCODE:*_*_X64_CC_FLAGS    = ... -DOPENSSL_NO_APPLE_CRYPTO_RANDOM
```

