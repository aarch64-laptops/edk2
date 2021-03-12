/** @file

  Copyright (c) 2019, Linaro. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <libfdt.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/LoadedImage.h>

#include "Common.h"

/**
  Acquires a LOADED_IMAGE_PROTOCOL structure that points to the instance
  for the currently executing image.

  @param[in][out] LoadedImage  Pointer to EFI_LOADED_IMAGE_PROTOCOL pointer.

  @retval EFI_SUCCESS          The structure was located successfully.
  @retval other                The return value from gBS->OpenProtocol call.

**/
EFI_STATUS
EFIAPI
GetLoadedImageProtocol (
  IN OUT EFI_LOADED_IMAGE_PROTOCOL **LoadedImage
  )
{
  EFI_STATUS Status;

  Status = gBS->OpenProtocol (gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **) LoadedImage,
                  gImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    Print (L"Failed to open EFI_LOADED_IMAGE_PROTOCOL! %llx\n", Status);
  }

  return Status;
}

/**
  Acquires a SIMPLE_FILE_SYSTEM_PROTOCOL instance for the device handle of
  the supplied   EFI_LOADED_IMAGE_PROTOCOL instance.

  @param[in]      LoadedImage  Pointer to EFI_LOADED_IMAGE_PROTOCOL structure.
  @param[in][out] FileSystem   Pointer to EFI_SIMPLE_FILE_SYSTEM_PROTOCOL pointer.

  @retval EFI_SUCCESS          The structure was located successfully.
  @retval other                The return value from gBS->OpenProtocol call.

**/
EFI_STATUS
EFIAPI
GetLoadedImageFileSystem (
  IN     EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
  IN OUT EFI_SIMPLE_FILE_SYSTEM_PROTOCOL **FileSystem
  )
{
  EFI_STATUS Status;

  Status = gBS->OpenProtocol (LoadedImage->DeviceHandle,
			      &gEfiSimpleFileSystemProtocolGuid,
			      (VOID **)FileSystem,
			      gImageHandle,
			      NULL,
			      EFI_OPEN_PROTOCOL_GET_PROTOCOL
			      );
  if (EFI_ERROR (Status)) {
    Print (L"Failed to open SIMPLE_FILE_SYSTEM_PROTOCOL! %llx\n", Status);
  }

  return Status;
}

CHAR16 *
EFIAPI
StrRChr (
  IN CHAR16 *String,
  IN CHAR16 Character
  )
{
  CHAR16 *Pos;

  Pos = String + StrLen (String) - 1;

  while (Pos-- > String) {
    if (*Pos == Character) {
      return Pos;
    }
  }

  return NULL;
}


STATIC
EFI_STATUS
ReadFdtImpl (
  IN     EFI_FILE_PROTOCOL *Root,
  IN     CHAR16            *Path,
  IN OUT VOID              **Blob
  )
{
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS        Status;
  UINTN             BufferSize;
  EFI_FILE_INFO     *FileInfo;

  Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Dbg (L"Failed to open '%s'\n", Path);
    return Status;
  }

  Dbg (L"File '%s' opened successfully!\n", Path);
  BufferSize = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &BufferSize, NULL);

  FileInfo = AllocatePool (BufferSize);
  if (!FileInfo) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &BufferSize, FileInfo);
  Dbg (L"File size: %d bytes\n", FileInfo->FileSize);

  // Don't bother loading the file if it's smaller than the DT header
  if (FileInfo->FileSize < sizeof (struct fdt_header)) {
    Print (L"'%s' is not a valid .dtb (too small) - not using!\n", Path);
    Status = EFI_INVALID_PARAMETER;
    goto Cleanup;
  }

  *Blob = AllocatePool (FileInfo->FileSize);
  if (!*Blob) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Status = File->Read (File, &FileInfo->FileSize, *Blob);
  if (EFI_ERROR (Status)) {
    FreePool (*Blob);
    goto Cleanup;
  }

  if (fdt_check_header (*Blob)) {
    Print (L"'%s' does not have a valid fdt header!\n", Path);
    FreePool (*Blob);
    Status = EFI_NOT_FOUND;
    goto Cleanup;
  }

 Cleanup:
  FreePool (FileInfo);
  Root->Close (File);
  return Status;
}

STATIC
EFI_STATUS
VReadFdt (
  OUT      VOID              **Blob,
  IN       EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16            *Format,
  IN       VA_LIST           Marker
  )
{
  CHAR16                          *Path;
  UINTN                           PathLength;
  UINTN                           Return;
  EFI_STATUS                      Status;

  PathLength = 512;
  Path = (CHAR16 *) AllocatePool (PathLength * sizeof (CHAR16));
  ASSERT (Path != NULL);

  Return = UnicodeVSPrint (Path, PathLength * sizeof (CHAR16), Format, Marker);

  if (Return >= (PathLength - 1)) {
    Print (L"Path too long!\n");
    return EFI_OUT_OF_RESOURCES;
  }

  Dbg (L"Try to read: %s\n", Path);

  Status = ReadFdtImpl (Root, Path, Blob);

  FreePool (Path);

  return Status;
}

/**
  Attempt to read a .dtb at path construction from format string and var-args.

  @param[out]     Blob         The file contents
  @param[in]      Root         The root of the filesystem to read from


  @param[in]      LoadedImage  Pointer to EFI_LOADED_IMAGE_PROTOCOL structure.
  @param[in][out] FileSystem   Pointer to EFI_SIMPLE_FILE_SYSTEM_PROTOCOL pointer.
  @param[in]      Format       A Null-terminated Unicode format string.
  @param[in]      ...          A Variable argument list whose contents are accessed
                               based on the format string specified by Format.

  @retval EFI_SUCCESS          The file was located successfully.
  @retval other                The error value.

**/
EFI_STATUS
ReadFdt (
  OUT      VOID              **Blob,
  IN       EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16            *Format,
  ...
  )
{
  VA_LIST    Marker;
  EFI_STATUS Status;

  VA_START (Marker, Format);

  Status = VReadFdt (Blob, Root, Format, Marker);

  VA_END (Marker);

  return Status;

}
