#!/bin/bash


# as for magic runes
# if cross compiling, export GCC5_AARCH64_PREFIX=<whatever your toolchain>
# then, in edk2 dir, source edksetup.sh (--reconfig if you rerun at some later point, and might be using a different toolchain)
# then build -a AARCH64 -t GCC5 -p EmbeddedPkg/EmbeddedPkg.dsc -m EmbeddedPkg/Application/ConfigTableLoader/DtbLoader.inf
# use -b DEBUG or -b RELEASE to build with/without debug info and extra printouts

# https://eciton.net/~leif/DtbLoader.efi now works as expected - installing persistently is still manual from UEFI Shell
# source pushed to http://git.linaro.org/people/leif.lindholm/edk2.git/log/?h=dtbloader
# installation instructions - 1) copy driver to somewhere on the same filesystem the windows loader resides (this is FS3: when I boot with just the Shell USB key inserted), and the .dtb to the same directory, called my.dtb   2) bcfg driver add 1 <driver image> "<descriptive name>"

set -ex

export PYTHON3_ENABLE=1
export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
source edksetup.sh

build -b DEBUG -a AARCH64 -t GCC5 -p EmbeddedPkg/EmbeddedPkg.dsc -m EmbeddedPkg/Application/ConfigTableLoader/DtbLoader.inf
#build -b DEBUG -a AARCH64 -t GCC5 -p EmbeddedPkg/EmbeddedPkg.dsc -m EmbeddedPkg/Application/ConfigTableLoader/DtbLoaderUpdate.inf
#build -b DEBUG -a AARCH64 -t GCC5 -p EmbeddedPkg/EmbeddedPkg.dsc -m EmbeddedPkg/Application/ConfigTableLoader/FmpDeviceLib.inf


#build -b DEBUG -a AARCH64 -t GCC5 -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Application/CapsuleApp/CapsuleApp.inf
#build -b DEBUG -a AARCH64 -t GCC5 -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Application/HelloWorld/HelloWorld.inf
#build -b DEBUG -a AARCH64 -t GCC5 -p ShellPkg/ShellPkg.dsc -m ShellPkg/Application/Shell/Shell.inf
