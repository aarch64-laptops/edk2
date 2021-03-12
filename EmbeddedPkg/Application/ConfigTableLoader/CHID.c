/** @file
  Application to load and register a .dtb file.

  Replaces any existing registration.

  Copyright (c) 2019, Linaro. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Guid/SmBios.h>
#include <IndustryStandard/SmBios.h>

#include "CHID.h"
#include "Common.h"

/*
 * These map to what linux prints at boot, when you see a string like:
 *
 *   DMI: LENOVO 81JL/LNVNB161216, BIOS ...
 *
 * We don't really care about the BIOS version information, but the
 * first part gives a reasonable way to pick a dtb.
 */
STATIC struct {
  CHAR16 *Manufacturer;           /* System Information/Manufacturer */
  CHAR16 *ProductName;            /* System Information/Product Name */
  CHAR16 *ProductSku;             /* System Information/SKU Number */
  CHAR16 *Family;                 /* System Information/Family */
  CHAR16 *BaseboardProduct;       /* Base Board Information/Product Name */
  CHAR16 *BaseboardManufacturer;  /* Base Board Information/Manufacturer */
} mSmbiosInfo;

// HACK, why do I need this?
EFI_GUID gEfiSmbios3TableGuid = { 0xF2FD1544, 0x9794, 0x4A2C, { 0x99, 0x2E, 0xE5, 0xBB, 0xCF, 0x20, 0xE3, 0x94 }};
EFI_GUID gEfiSmbiosTableGuid = { 0xEB9D2D31, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D }};

VOID *
GetSmbiosTable (VOID)
{
  SMBIOS_TABLE_ENTRY_POINT     *SmbiosTable = NULL;
  SMBIOS_TABLE_3_0_ENTRY_POINT *Smbios64BitTable = NULL;
  EFI_STATUS                   Status;

  Status = EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, (VOID**)&Smbios64BitTable);
  if (Smbios64BitTable) {
    Dbg (L"Got 64b SMBIOS Table\n");
    return (VOID *) (UINTN) (Smbios64BitTable->TableAddress);
  } else {
    Status = EfiGetSystemConfigurationTable (&gEfiSmbiosTableGuid, (VOID**)&SmbiosTable);
    if (SmbiosTable) {
      Dbg (L"Got SMBIOS Table\n");
      return (VOID *) (UINTN) (SmbiosTable->TableAddress);
    } else {
      Print (L"%a:%d: Status = %x\n", __func__, __LINE__, Status);
      return NULL;
    }
  }
}


STATIC
CHAR16*
LibGetSmbiosString (
  IN  SMBIOS_STRUCTURE_POINTER    *Smbios,
  IN  UINT16                      StringNumber
  )
{
  UINT16  Index;
  CHAR8   *String;

  ASSERT (Smbios != NULL);

  //
  // Skip over formatted section
  //
  String = (CHAR8 *) (Smbios->Raw + Smbios->Hdr->Length);

  //
  // Look through unformatted section
  //
  for (Index = 1; Index <= StringNumber; Index++) {
    if (StringNumber == Index) {
      UINTN StrSize = AsciiStrnLenS (String, ~0) + 1;
      CHAR16 *String16 = AllocatePool (StrSize * sizeof (CHAR16));
      AsciiStrToUnicodeStrS (String, String16, StrSize);
      return String16;
    }
    //
    // Skip string
    //
    for (; *String != 0; String++);
    String++;

    if (*String == 0) {
      //
      // If double NULL then we are done.
      //  Return pointer to next structure in Smbios.
      //  if you pass in a -1 you will always get here
      //
      Smbios->Raw = (UINT8 *)++String;
      return NULL;
    }
  }

  return NULL;
}

EFI_STATUS
ReadSmbiosInfo (VOID)
{
  SMBIOS_TABLE_TYPE1           *Type1Record;
  SMBIOS_TABLE_TYPE2           *Type2Record;
  SMBIOS_STRUCTURE_POINTER     Smbios;

  Smbios.Raw = GetSmbiosTable();

  if (!Smbios.Raw)
    return EFI_NOT_FOUND;

#define GetStr(Name, Str) do {                                  \
    mSmbiosInfo.Name = LibGetSmbiosString(&Smbios, Str);        \
    Dbg (L"%a=%s       (%a)\n", #Name, mSmbiosInfo.Name, #Str); \
  } while (0)

  while (Smbios.Hdr->Type != 127) {

    if (Smbios.Hdr->Type == SMBIOS_TYPE_SYSTEM_INFORMATION) {
      Type1Record = (SMBIOS_TABLE_TYPE1 *) Smbios.Raw;
      GetStr(Manufacturer, Type1Record->Manufacturer);
      GetStr(ProductName, Type1Record->ProductName);
      GetStr(ProductSku, Type1Record->SKUNumber);
      GetStr(Family, Type1Record->Family);
    }

    if (Smbios.Hdr->Type == SMBIOS_TYPE_BASEBOARD_INFORMATION) {
      Type2Record = (SMBIOS_TABLE_TYPE2 *) Smbios.Raw;
      GetStr(BaseboardManufacturer, Type2Record->Manufacturer);
      GetStr(BaseboardProduct, Type2Record->ProductName);
    }

    //
    // Walk to next structure
    //
    LibGetSmbiosString (&Smbios, (UINT16) (-1));
  }

  return EFI_SUCCESS;
}

/**
  Hash a string, after stripping leading and trailing whitespace:
 */
STATIC
BOOLEAN
Sha1Str (
  IN OUT   VOID   *Sha1Context,
  IN CONST CHAR16 *Str
  )
{
  UINTN Len;

  /* strip leading spaces: */
  while (*Str == L' ')
    Str++;

  /* also strip leading zero's.. fwupd does this after stripping leading
   * spaces, and it seems to match what ComputerHardwareIds.exe generates:
   */
  while (*Str == L'0')
    Str++;

  Len = StrLen (Str);

  /* strip trailing spaces: */
  while ((Len > 0) && (Str[Len - 1] == L' '))
    Len--;

  return Sha1Update (Sha1Context, Str, Len * sizeof (CHAR16));
}

EFI_STATUS
GetComputerHardwareId(
  EFI_GUID *DstCHID,
  CHID chid
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_GUID Namespace = { 0x70ffd812, 0x4c7f, 0x4c7d, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
  VOID     *HashCtx;
  UINTN    CtxSize;
  UINT8    HashValue[20];

  /* Seeded with GUID_NAMESPACE_MICROSOFT as big-endian, so endian swap: */
  Namespace.Data1 = SwapBytes32 (Namespace.Data1);
  Namespace.Data2 = SwapBytes16 (Namespace.Data2);
  Namespace.Data3 = SwapBytes16 (Namespace.Data3);

  CtxSize = Sha1GetContextSize ();
  HashCtx = AllocatePool (CtxSize);

  Sha1Init   (HashCtx);
  Sha1Update (HashCtx, &Namespace, sizeof (Namespace));
  switch (chid) {
//case CHID_0:      // Manufacturer + Family + ProductName + ProductSku + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
//case CHID_1:      // Manufacturer + Family + ProductName + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
//case CHID_2:      // Manufacturer + ProductName + BiosVendor + BiosVersion + BiosMajorRelease + BiosMinorRelease
  case CHID_3:      // Manufacturer + Family + ProductName + ProductSku + BaseboardManufacturer + BaseboardProduct
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.Family);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductName);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductSku);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardManufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardProduct);
    break;
  case CHID_4:      // Manufacturer + Family + ProductName + ProductSku
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.Family);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductName);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductSku);
    break;
  case CHID_5:      // Manufacturer + Family + ProductName
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.Family);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductName);
    break;
  case CHID_6:      // Manufacturer + ProductSku + BaseboardManufacturer + BaseboardProduct
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductSku);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardManufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardProduct);
    break;
  case CHID_7:      // Manufacturer + ProductSku
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductSku);
    break;
  case CHID_8:      // Manufacturer + ProductName + BaseboardManufacturer + BaseboardProduct
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductName);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardManufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardProduct);
    break;
  case CHID_9:      // Manufacturer + ProductName
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.ProductName);
    break;
  case CHID_10:     // Manufacturer + Family + BaseboardManufacturer + BaseboardProduct
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.Family);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardManufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardProduct);
    break;
  case CHID_11:     // Manufacturer + Family
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.Family);
    break;
//case CHID_12:     // Manufacturer + EnclosureKind
  case CHID_13:     // Manufacturer + BaseboardManufacturer + BaseboardProduct
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardManufacturer);
    Sha1Str (HashCtx, L"&");
    Sha1Str (HashCtx, mSmbiosInfo.BaseboardProduct);
    break;
  case CHID_14:     // Manufacturer
    Sha1Str (HashCtx, mSmbiosInfo.Manufacturer);
    break;
  default:
    Status = EFI_NOT_FOUND;
  }
  Sha1Final  (HashCtx, HashValue);

  CopyMem (DstCHID, HashValue, sizeof (*DstCHID));

  /* Convert the resulting CHID back to little-endian: */
  DstCHID->Data1 = SwapBytes32 (DstCHID->Data1);
  DstCHID->Data2 = SwapBytes16 (DstCHID->Data2);
  DstCHID->Data3 = SwapBytes16 (DstCHID->Data3);

  /* set specific bits according to RFC4122 Section 4.1.3 */
  DstCHID->Data3    = (DstCHID->Data3 & 0x0fff) | (5 << 12);
  DstCHID->Data4[0] = (DstCHID->Data4[0] & 0x3f) | 0x80;

  FreePool (HashCtx);

  return Status;
}
