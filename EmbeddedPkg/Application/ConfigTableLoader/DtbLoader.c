/** @file
  Application to load and register a .dtb file.

  Replaces any existing registration.

  Copyright (c) 2019, Linaro. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <libfdt.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Guid/SmBios.h>
#include <IndustryStandard/SmBios.h>

#include <Guid/Fdt.h>

#include "CHID.h"
#include "Common.h"
#include "Qcom.h"

STATIC struct {
  UINT32 Crc32;
  UINT32 TotalSize;
  VOID   *Data;
} mBlobInfo;

#define FDT_ADDITIONAL_SIZE 0x400

/* Increase the size of the FDT blob so that we can patch in new nodes */
STATIC
EFI_STATUS
ResizeBlob (
  IN OUT VOID **Blob
  )
{
  VOID  *NewBlob;
  UINTN NewSize;
  INTN  Err;

  NewSize = fdt_totalsize (*Blob) + FDT_ADDITIONAL_SIZE;
  NewBlob = AllocatePool (NewSize);
  if (!NewBlob) {
    Print (L"%a:%d: allocation failed\n", __func__, __LINE__);
    return EFI_OUT_OF_RESOURCES;
  }

  Err = fdt_open_into (*Blob, NewBlob, NewSize);
  if (Err) {
    Print (L"Could not expand fdt: %a\n", fdt_strerror (Err));
    FreePool (NewBlob);
    return EFI_OUT_OF_RESOURCES;
  }

  /* Successfully Resized: */
  mBlobInfo.Data = NewBlob;

  FreePool (*Blob);
  *Blob = NewBlob;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
RegisterDtBlob (
  IN VOID *Blob
  )
{
  EFI_STATUS Status;

  /* Calculate CRC to detect changes.  The linux kernel's efi libstub
   * will insert the kernel commandline into the chosen node before
   * calling ExitBootServices, and we can use this to differentiate
   * between ACPI boot (ie. windows) and DT boot.
   */
  mBlobInfo.TotalSize = fdt_totalsize (mBlobInfo.Data);
  gBS->CalculateCrc32 (mBlobInfo.Data, mBlobInfo.TotalSize, &mBlobInfo.Crc32);

  Dbg (L"DT CRC32: %08x\n", mBlobInfo.Crc32);
  Dbg (L"DT TotalSize: %d bytes\n", mBlobInfo.TotalSize);

  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, Blob);
  if (!EFI_ERROR (Status)) {
    Dbg (L"DTB installed successfully!\n");
  }

  return Status;
}


STATIC
VOID
EFIAPI
ExitBootServicesHook (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  VOID *Data;
  UINT32 Crc32;

#if !defined(MDEPKG_NDEBUG)
  gST->ConOut->OutputString (gST->ConOut,
		 L"Checking DT CRC...\r\n"
		 );
#endif

  // If the table we registered isn't there, abort.
  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gFdtTableGuid, &Data))) {
    return;
  }

  gBS->CalculateCrc32 (Data, fdt_totalsize (Data), &Crc32);
  if (Crc32 == mBlobInfo.Crc32) {
    // If Crc32 unchanged, ACPI is in use, so don't delete it.
    return;
  }

#if !defined(MDEPKG_NDEBUG)
  gST->ConOut->OutputString (gST->ConOut,
		 L"DT in use - unregistering ACPI tables\r\n"
		 );
#endif

  // DT appears to be used, so deregister ACPI tables
  gBS->InstallConfigurationTable (&gEfiAcpiTableGuid, NULL);
  gBS->InstallConfigurationTable (&gEfiAcpi20TableGuid, NULL);
}

/////////////////////////////////////////////////////////////////
STATIC
VOID
PrintChid (VOID)
{
#if !defined(MDEPKG_NDEBUG)
  EFI_STATUS Status;
  EFI_GUID   CHID;

#define DumpCHID(__chid) do {                              \
    Status = GetComputerHardwareId(&CHID, __chid);         \
    if (!EFI_ERROR (Status)) {                             \
      Print (L"{%g}   <- %a\n", &CHID, #__chid);           \
    }                                                      \
  } while (0)

  DumpCHID(CHID_3);
  DumpCHID(CHID_4);
  DumpCHID(CHID_5);
  DumpCHID(CHID_6);
  DumpCHID(CHID_7);
  DumpCHID(CHID_8);
  DumpCHID(CHID_9);
  DumpCHID(CHID_10);
  DumpCHID(CHID_11);
  DumpCHID(CHID_13);
  DumpCHID(CHID_14);
#endif
}
/////////////////////////////////////////////////////////////////

/**
  Rough attempt to sort in order from most specfic to least, omitting
  ones that are too generic to be plausible, or are not supported yet
 */
STATIC CHID PrioritizedCHIDs[] = {
    CHID_3,      // Manufacturer + Family + ProductName + ProductSku + BaseboardManufacturer + BaseboardProduct
    CHID_6,      // Manufacturer + ProductSku + BaseboardManufacturer + BaseboardProduct
    CHID_8,      // Manufacturer + ProductName + BaseboardManufacturer + BaseboardProduct
    CHID_10,     // Manufacturer + Family + BaseboardManufacturer + BaseboardProduct
    CHID_4,      // Manufacturer + Family + ProductName + ProductSku
    CHID_5,      // Manufacturer + Family + ProductName
    CHID_7,      // Manufacturer + ProductSku
    CHID_9,      // Manufacturer + ProductName
    CHID_11,     // Manufacturer + Family
};

STATIC
EFI_STATUS
LoadAndRegisterDtb (VOID)
{
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_STATUS                      Status;
  VOID                            *Blob;
  UINT32                          Index;
  CHID                            CurrentCHID;

  Dbg (L"LoadAndRegisterDtb\n");

  Status = GetLoadedImageProtocol (&LoadedImage);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetLoadedImageFileSystem (LoadedImage, &FileSystem);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadSmbiosInfo();
  if (EFI_ERROR (Status)) {
    Print (L"Failed to read SMBIOS info: Status = %x\n", Status);
    goto Cleanup;
  }

  PrintChid();

  Status = FileSystem->OpenVolume (FileSystem, &Root);
  if (EFI_ERROR (Status)) {
    Print (L"OpenVolume call failed!\n");
    goto Cleanup;
  }

  /* Try finding a matching .dtb based on prioritized list of hw-id's: */
  for (Index = 0; Index < ARRAY_SIZE(PrioritizedCHIDs); Index++) {
    EFI_GUID CHID;

    CurrentCHID = PrioritizedCHIDs[Index];

    Status = GetComputerHardwareId(&CHID, CurrentCHID);
    if (EFI_ERROR (Status))
      continue;

    Status = ReadFdt (&Blob, Root, L"\\dtb\\%g.dtb", &CHID);
    if (!EFI_ERROR (Status))
      break;
  }

  if (EFI_ERROR (Status)) {
    /* finally fallback to trying \MY.DTB: */
    // TODO should we try this first, as a convenient way to override default dtb for devel??
    Status = ReadFdt (&Blob, Root, L"\\MY.dtb");
  }

  if (!EFI_ERROR (Status)) {
    EFI_EVENT ExitBootServicesEvent;

    ResizeBlob (&Blob);
    QcomDetectPanel (Root, Blob, CurrentCHID);
    RegisterDtBlob (Blob);

    Status = gBS->CreateEvent (
		    EVT_SIGNAL_EXIT_BOOT_SERVICES,
		    TPL_CALLBACK,
		    ExitBootServicesHook,
		    NULL,
		    &ExitBootServicesEvent
		    );
    if (EFI_ERROR (Status)) {
      Print (L"Failed to install ExitBootServices hook!");
    }
  }

  Status = Root->Close (Root);
  if (EFI_ERROR (Status)) {
    Print (L"Root->Close failed: %llx\n", Status);
    goto Cleanup;
  }

 Cleanup:
  return Status;
}

#define NEXT_STAGE L"grubaa64.efi"

STATIC
CHAR16 *
GetNextStagePath (
  IN EFI_LOADED_IMAGE_PROTOCOL *LoadedImage
  )
{
  EFI_DEVICE_PATH_PROTOCOL        *DevPathNode;
  CHAR16                          *DevPathString;
  CHAR16                          *NextStagePath;
  CHAR16                          *Pos;
  UINTN                           Len;

  DevPathNode = LoadedImage->FilePath;
  if (!DevPathNode) {
    Dbg (L"No FilePath!\n");
    return NULL;
  }

  /*
   * We are looking for a file in the same directory as ourselves, with
   * the name NEXT_STAGE (ie. "grubaa64.efi", etc)...
   *
   * TODO is LoadedImage->FilePath ever *not* a filepath node?  If not we
   * can skip the loop nonsense.
   */
  while (!IsDevicePathEnd (DevPathNode)) {
    DevPathString = ConvertDevicePathToText (DevPathNode, TRUE, FALSE);

    Dbg (L"DevPathString=%s\n", DevPathString);
    FreePool (DevPathString);

    if ((DevPathNode->Type == MEDIA_DEVICE_PATH) && (DevPathNode->SubType == MEDIA_FILEPATH_DP)) {
      break;
    }

    DevPathNode = NextDevicePathNode (DevPathNode);
  }

  if (IsDevicePathEnd (DevPathNode)) {
    Dbg (L"No FilePath (reached the end)!\n");
    return NULL;
  }

  DevPathString = ConvertDevicePathToText (DevPathNode, TRUE, FALSE);
  Len = 1 + StrLen (DevPathString) + StrLen (NEXT_STAGE);
  NextStagePath = AllocatePool (Len * sizeof (CHAR16));
  StrCpyS (NextStagePath, Len, DevPathString);
  FreePool (DevPathString);

  Pos = StrRChr (NextStagePath, L'\\');
  if (!Pos) {
    Pos = NextStagePath;
  } else {
    ++Pos;
  }

  StrCpyS (Pos, 1 + StrLen (NEXT_STAGE), NEXT_STAGE);

  return NextStagePath;
}

STATIC
EFI_STATUS
LoadNextStage (
  IN EFI_HANDLE ImageHandle
  )
{
  EFI_DEVICE_PATH_PROTOCOL        *NewImagePath;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_HANDLE                      NewImageHandle;
  CHAR16                          *NextStagePath;
  EFI_STATUS                      Status;

  Dbg (L"LoadNextStage\n");

  Status = GetLoadedImageProtocol (&LoadedImage);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetLoadedImageFileSystem (LoadedImage, &FileSystem);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NextStagePath = GetNextStagePath (LoadedImage);
  Dbg (L"Got NextStagePath=%s\n", NextStagePath);
  if (!NextStagePath) {
    Status = EFI_NO_MEDIA;
    goto Cleanup;
  }

  NewImagePath = FileDevicePath (LoadedImage->DeviceHandle, NextStagePath);

  Status = gBS->LoadImage (
      FALSE,
      ImageHandle,
      NewImagePath,
      NULL,
      0,
      &NewImageHandle);
  if (EFI_ERROR (Status)) {
    Print (L"Failed to load %s\n", NextStagePath);
    goto Cleanup;
  }

  Status = gBS->StartImage (
      NewImageHandle,
      0,
      NULL);
  if (EFI_ERROR (Status)) {
    Print (L"Failed to start %s\n", NextStagePath);
    goto Cleanup;
  }

 Cleanup:
  // TODO
  return Status;
}

typedef struct {
  UINT16  Version;
  UINT16  Length;
  UINT32  RuntimeServicesSupported;
} EFI_RT_PROPERTIES_TABLE;

STATIC
VOID
InstallRtPropertiesTable (
  VOID
  )
{
  EFI_RT_PROPERTIES_TABLE *Table;
  EFI_STATUS              Status;

  Table = AllocateRuntimePool (sizeof *Table);
  ASSERT (Table != NULL);

  Table->Version                  = 0x1;
  Table->Length                   = sizeof *Table;
  Table->RuntimeServicesSupported = 0;  // all unsupported

  Status = gBS->InstallConfigurationTable (&gEfiRtPropertiesTableGuid, Table);
  ASSERT_EFI_ERROR (Status);
}

/**
  The user Entry Point for Application. The user code starts with this function
  as the real entry point for the application.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status = EFI_LOAD_ERROR;

  InstallRtPropertiesTable ();

  if (GetSmbiosTable()) {
    /* already got SMBIOS tables configured, so just go: */
    Status = LoadAndRegisterDtb();
    if (EFI_ERROR (Status)) {
      Dbg (L"Could not load DTB! (%x)\n", Status);
      return Status;
    }
  }

  return LoadNextStage (ImageHandle);
}

