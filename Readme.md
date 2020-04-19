# DtbLoader

DtbLoader is an EFI "driver" intended to support booting a [devicetree](https://www.devicetree.org/)
based kernel on ACPI based hardware, in particular windows ARM based laptops.
It provides a way to determine *which* dtb tables to load, and apply appropriate
dtb overlays based on the hw configuration, as determined by SMBIOS table
values, etc.

The motivation is to support linux support for Snapdragon(TM) ARM laptops,
although it's utility is not limited to a particular architecture or OS.
DtbLoader runs before the OS starts, in order to support any
[EBBR](https://github.com/ARM-software/ebbr) compatible OS or distro.

## Why not ACPI

ACPI provides a lot of advantages from the perspective on the operating
system provider (OSV), in particular it provides enough of a description of
the hardware (in tables and methods) to boot an operating system which was
released before the hardware existed.  That may not provide full functionality
of all the various accelerators (GPUs, etc), but it gets the user far enough
along that they can install, boot, and then install OS updates to get support
for the newest, shiniest, features in their hardware.

But, the windows snapdragon laptops rely on
[Platform Extension Plugins (PEPs)](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/platform-extension-plug-ins--peps-)
for power control for more advanced platform hardware.  In particular,
device power state transitions which previously would have been handled
entirely via ACPI methods, are instead handled by a PEP driver which can
control the necessary clocks, power domains, interconnect/memory scaling
required for a given device power state, for more precise power management.
Linux currently has no equivalent mechanism.  So while ACPI boot is sufficient
to get basic functionality and run an an OS installer, it is not sufficient
for more advanced support (display support beyond efifb, gpu, video
acceleration, audio, etc).

## How it works

Once installed, *DtbLoader.efi* runs before the OS bootloader.  It looks
for a matching dtb file installed in the [ESP](https://en.wikipedia.org/wiki/EFI_system_partition),
based on the machines CHID (*$ESP\dtb\\$CHID.dtb*), and if found, it will
load and install the devicetree table as a EFI config table.  If necessary,
it will apply any needed dtb overlays first.  If no matching dtb is found,
DtbLoader will not change/adjust anything.

In particular, it is common for a given laptop SKU to use one of several
possible different LCD panels.  (They may have the same resolution and
otherwise appear identical to the user, but could have different panel
timings, etc.)  For the snapdragon arm laptops, DtbLoader reads the
`UEFIDisplayInfo` EFI variable to determine the panel-id, and applies the
appropriate dt overlay.  The dtb overlay is loaded from *$ESP\dtb\qcom-panels\\$panel-id.dtb*.

## CHIDs?

ComputerHardwareIds are a way to generate a UUID from fields in the SMBIOS
tables.  This gives a convenient way to generate a file path without having
to worry about escaping any random characters a hw vendor might stuff into
SMBIOS, or having to worry about file path length.  There are actually
multiple CHIDs for a given machine, and DtbLoader will search from most-
specific to least, to provide a way to overload dtb table matching in case
some particular variant of a given device needs specific workarounds.

For more details, see:

 * https://blogs.gnome.org/hughsie/2017/04/25/reverse-engineering-computerhardwareids-exe-with-winedbg/
 * https://github.com/fwupd/fwupd/blob/master/libfwupdplugin/fu-hwids.c#L121

In particular, DtbLoader looks for the following sequence of CHIDs, in
priority order:

 * `CHID_3` -  Manufacturer + Family + ProductName + ProductSku + BaseboardManufacturer + BaseboardProduct
 * `CHID_4` -  Manufacturer + Family + ProductName + ProductSku
 * `CHID_5` -  Manufacturer + Family + ProductName
 * `CHID_6` -  Manufacturer + ProductSku + BaseboardManufacturer + BaseboardProduct
 * `CHID_7` -  Manufacturer + ProductSku
 * `CHID_8` -  Manufacturer + ProductName + BaseboardManufacturer + BaseboardProduct
 * `CHID_9` -  Manufacturer + ProductName

When adding support for a new device, pick the least specific possible CHID,
and leave the more specific options for overriding the less specific options.
`CHID_9` should be sufficient in most/all cases.

For example, the Lenovo C630 laptop has Manufacturer "LENOVO" and ProductName
"81JL", which can be seen in the linux kernel bootlog:

```
[    0.057874] DMI: LENOVO 81JL/LNVNB161216, BIOS 9UCN23WW(V1.06) 10/25/2018
```

Which translates to CHID_9 value of `30b031c0-9de7-5d31-a61c-dee772871b7d`,
therefore DtbLoader will load the dtb file from *$ESP\dtb\30b031c0-9de7-5d31-a61c-dee772871b7d.dtb*.

## Build Instructions

```
export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
source edksetup.sh
build -b DEBUG -a AARCH64 -t GCC5 -p EmbeddedPkg/EmbeddedPkg.dsc -m EmbeddedPkg/Application/ConfigTableLoader/DtbLoader.inf
build -b DEBUG -a AARCH64 -t GCC5 -p ShellPkg/ShellPkg.dsc -m ShellPkg/Application/Shell/Shell.inf
```

(the last step is option but useful if you need a *Shell.efi* for installation)

The result will be *./Build/Embedded/DEBUG_GCC5/AARCH64/DtbLoader.efi*

## Installation

Copy *DtbLoader.efi* to ESP, and boot to *Shell.efi*, and:

```
Shell> fs2:   <== or whichever partition your ESP is on
FS:\> bcfg driver add 1 DtbLoader.efi "dtb loader"
FS:\> reset
```

As the upstream kernel is not using a dtb naming scheme conducive to automated
choosing of appropriate dtb, *EmbeddedPkg/Application/ConfigTableLoader/install-dtbs.py*
provides a way to copy dtb files from a linux kernel build, translating
the linux kernel dtb name into an appropriate CHID based name.  To add
support for new laptops, add additional entries to the `dtbs` table.

## TODO

* smoother installation process.. In particular, `bcfg` will use the full
  EFI device-path to *DtbLoader.efi`, meaning that if it is an external usb
  drive, and the user plugs it in to a different USB port (or hub topology
  changes, etc) on the next boot, *DtbLoader.efi* won't be found and loaded.

* update process.. rough idea is to write a fwupd plugin and use the LVFS
  mechanism to distribute updated and new dtb files to users.  Fwupd already
  has the logic to find and mount the ESP partition in order to install
  new/updated dtb files.  By the time the devicetree files land in upstream
  kernel, they should always be backwards compatible, so booting an older.
  distro kernel with newer dtb files should be a thing that always works.

  Note than this could also be accomplished via normal distro update
  mechanisms, but that is likely to cause problems if a user is booting
  multiple different distros/OSs from different partitions.  It is better
  to have a distro-agnosting way to deliver updated dtb to users.

* find a better home than my personal github for DtbLoader

