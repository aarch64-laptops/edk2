/** @file

  Copyright (c) 2019, Linaro. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef COMMON_H_
#define COMMON_H_

#include <Uefi.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>

#if !defined(MDEPKG_NDEBUG)
#define Dbg(...) do { Print(__VA_ARGS__); gBS->Stall(100000); } while (0)
#else
#define Dbg(...)
#endif

/**
  Acquires a LOADED_IMAGE_PROTOCOL structure that points to the instance
  for the currently executing image.

  @param[in][out] LoadedImage  Pointer to LOADED_IMAGE_PROTOCOL pointer.

  @retval EFI_SUCCESS          The structure was located successfully.
  @retval other                The return value from gBS->OpenProtocol call.

**/
EFI_STATUS
EFIAPI
GetLoadedImageProtocol (
  IN OUT EFI_LOADED_IMAGE_PROTOCOL **LoadedImage
  );

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
  );

CHAR16 *
EFIAPI
StrRChr (
 IN CHAR16 *String,
 IN CHAR16 Character
 );

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
  );
#endif /* COMMON_H_ */
