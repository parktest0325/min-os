# min-os
mini OS from MikanOS (M1 Mac, WSL Ubuntu)

<br>

## Dev Setting
```sh
# path: ~
git clone git@github.com:parktest0325/min-os.git
git clone https://github.com/uchan-nos/mikanos.git

cd ~/mikanos
git checkout osbook_day02a

# path: ~/min-os/setup/
./ubuntu_setup.sh
./mac_setup.sh
```

#### WSL Qemu X-server
install `https://sourceforge.net/projects/vcxsrv/` 


<br>

## build and run_qemu.sh
```sh
# path: min-os/devenv/edk2
ln -s ../../mikanos/MikanLoaderPkg ./

## Ubuntu (WSL)
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -t CLANG38 -b RELEASE
build -p MikanLoaderPkg/MikanLoaderPkg.dsc -a X64 -t CLANG38 -b RELEASE
cp Build/OvmfX64/RELEASE_CLANG38/FV/OVMF_* ../
cp Build/MikanLoaderX64/RELEASE_CLANG38/X64/Loader.efi ../
./run_qemu.sh ../Loader.efi

## MacOS
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -t XCODE5 -b RELEASE
build -p MikanLoaderPkg/MikanLoaderPkg.dsc -a X64 -t CLANGDWARF -b RELEASE
cp Build/OvmfX64/RELEASE_XCODE5/FV/OVMF_* ../
cp Build/MikanLoaderX64/RELEASE_CLANGDWARF/X64/Loader.efi ../
./run_qemu.sh ../Loader.efi
```

<br>

## Oh.. Error occured..
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

