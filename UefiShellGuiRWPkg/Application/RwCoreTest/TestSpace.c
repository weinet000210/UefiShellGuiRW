/** @file
  RwCoreTest: memory, I/O, CMOS and EC spaces.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"
#include <Library/UefiLib.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Acpi.h>

VOID
TestSpace (
  VOID
  )
{
  RW_DATA_SOURCE                                Ds;
  UINT64                                        Value;
  UINT64                                        *Buffer;
  UINT8                                         Block[16];
  UINT8                                         RegB;
  UINT8                                         Sec;
  UINT8                                         Status;
  BOOLEAN                                       Ec;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;

  //
  // Memory: a pool buffer written and read back through the source.
  //
  RwMemSourceInit (&Ds);
  Buffer = AllocateZeroPool (64);
  CHECK (Buffer != NULL, "pool buffer for memory checks");
  if (Buffer != NULL) {
    CHECK (!EFI_ERROR (RwDsWrite (&Ds, (UINTN)Buffer, 8, 0x1122334455667788ULL)), "memory write 64-bit");
    CHECK (!EFI_ERROR (RwDsRead (&Ds, (UINTN)Buffer + 2, 2, &Value)) && (Value == 0x5566), "memory read 16-bit");
    CHECK (!EFI_ERROR (RwDsRead (&Ds, (UINTN)Buffer + 4, 4, &Value)) && (Value == 0x11223344), "memory read 32-bit");
    CHECK (!RwDsIsDangerous (&Ds, (UINTN)Buffer, 8), "boot services memory is not dangerous");
    FreePool (Buffer);
  }

  CHECK (RwDsIsDangerous (&Ds, 0xFFFFFFF0ULL, 4), "firmware flash window is dangerous");
  Rsdp = NULL;
  EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp);
  if ((Rsdp != NULL) && (Rsdp->XsdtAddress != 0)) {
    CHECK (RwMemIsDangerous (Rsdp->XsdtAddress, 4), "the XSDT (ACPI reclaim memory) is dangerous");
  } else {
    TestLog ("    no ACPI 2.0 RSDP / XSDT, ACPI region check skipped");
  }

  //
  // I/O: reading CF8h is harmless; the reset ports are dangerous.
  //
  RwIoSourceInit (&Ds);
  CHECK ((Ds.Limit == 0xFFFF) && Ds.SideEffects, "I/O source covers 64 K ports, reads have side effects");
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 0xCF8, 4, &Value)), "I/O read of CF8h");
  CHECK (RwDsIsDangerous (&Ds, 0xCF9, 1) && RwDsIsDangerous (&Ds, 0xCF8, 4), "CF9h writes are dangerous");
  CHECK (RwDsIsDangerous (&Ds, 0x92, 1) && RwDsIsDangerous (&Ds, 0x64, 1), "92h and 64h writes are dangerous");
  CHECK (!RwDsIsDangerous (&Ds, 0x80, 1) && !RwDsIsDangerous (&Ds, 0xCFC, 4), "80h and CFCh writes are not");
  CHECK (EFI_ERROR (RwDsRead (&Ds, 0xFFFF, 2, &Value)), "I/O read past FFFFh fails");

  //
  // CMOS: the RTC is there.
  //
  RwCmosSourceInit (&Ds);
  CHECK ((Ds.Limit == 0xFF) && (Ds.WidthMask == RW_DS_WIDTH_8), "CMOS source is 256 bytes, byte access");
  CHECK ((RwCmosRead8 (0x0D) & 0x80) != 0, "RTC register D reports valid RAM and time");
  RegB = RwCmosRead8 (0x0B);
  Sec  = RwCmosRead8 (0x00);
  TestLog ("    RTC register B %02X, seconds %02X", RegB, Sec);
  if ((RegB & BIT2) == 0) {
    CHECK ((Sec <= 0x59) && ((Sec & 0x0F) <= 9), "RTC seconds are BCD 00-59");
  } else {
    CHECK (Sec <= 59, "RTC seconds are binary 0-59");
  }

  CHECK (!EFI_ERROR (RwDsReadBlock (&Ds, 0x0A, 4, Block, NULL)) && (Block[3] == RwCmosRead8 (0x0D)), "CMOS block read");

  //
  // Memory map: sorted, merged, has conventional memory and the names.
  //
  {
    RW_MEM_RANGE  *Ranges;
    UINTN         Count;
    UINTN         Index;
    BOOLEAN       Sorted;
    BOOLEAN       HaveRam;

    CHECK (!EFI_ERROR (RwMemMapGet (&Ranges, &Count)) && (Count > 0), "memory map has ranges");
    Sorted  = TRUE;
    HaveRam = FALSE;
    for (Index = 0; Index < Count; Index++) {
      if ((Index > 0) && (Ranges[Index].Start < Ranges[Index - 1].Start + LShiftU64 (Ranges[Index - 1].Pages, EFI_PAGE_SHIFT))) {
        Sorted = FALSE;
      }

      if ((Ranges[Index].Type == EfiConventionalMemory) && (Ranges[Index].Pages >= 256)) {
        HaveRam = TRUE;
      }
    }

    TestLog ("    memory map: %d ranges", Count);
    CHECK (Sorted, "memory map ranges are sorted and do not overlap");
    CHECK (HaveRam, "memory map has 1 MB or more of conventional memory");
    CHECK ((AsciiStrCmp (RwMemTypeName (EfiACPIMemoryNVS), "ACPI NVS") == 0) && (AsciiStrCmp (RwMemTypeName (EfiConventionalMemory), "Conventional") == 0), "memory type names");
    if (Ranges != NULL) {
      FreePool (Ranges);
    }
  }

  //
  // Index/Data: 70h/71h is the CMOS pair, so it has to agree with the CMOS
  // source; a write to a spare CMOS byte (38h) reads back both ways.
  //
  RwIndexDataSourceInit (&Ds, 0x70, 0x71);
  CHECK ((Ds.Limit == 0xFF) && Ds.SideEffects && (AsciiStrStr (Ds.Name, "0070h/0071h") != NULL), "Index/Data source names its ports");
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 0x0D, 1, &Value)) && ((UINT8)Value == RwCmosRead8 (0x0D)), "Index/Data 70h/71h reads CMOS register D");
  Sec = RwCmosRead8 (0x38);
  CHECK (!EFI_ERROR (RwDsWrite (&Ds, 0x38, 1, 0x5A)) && (RwCmosRead8 (0x38) == 0x5A), "Index/Data write lands in CMOS");
  RwCmosWrite8 (0x38, Sec);
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 0x38, 1, &Value)) && ((UINT8)Value == Sec), "Index/Data reads the restored byte");

  //
  // EC: q35 has none. The probe has to come back (timeouts), not hang.
  //
  Ec = RwEcProbe (RW_EC_DEFAULT_DATA, RW_EC_DEFAULT_CMD, &Status);
  TestLog ("    EC on 62h/66h: %a (status %02X)", Ec ? "present" : "none", Status);
  CHECK (TRUE, "EC probe returns");
  RwEcSourceInit (&Ds, RW_EC_DEFAULT_DATA, RW_EC_DEFAULT_CMD);
  CHECK ((Ds.Limit == 0xFF) && (Ds.Write != NULL), "EC source is 256 bytes, writable");
}
