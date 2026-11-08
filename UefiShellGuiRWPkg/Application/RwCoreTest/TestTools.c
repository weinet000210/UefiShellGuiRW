/** @file
  RwCoreTest: write log, BMP / hex text / snapshot formats, Dump All.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

STATIC
VOID
TestWlog (
  VOID
  )
{
  RW_DATA_SOURCE       Mem;
  UINT32               *Buf;
  CONST RW_WLOG_ENTRY  *E;
  UINT64               Total;
  UINTN                Index;
  RW_STR               Text;
  CHAR8                Time[16];
  CHAR8                Addr[20];
  CHAR8                Old[20];
  CHAR8                New[20];
  CHAR8                Status[40];

  RwWlogClear ();
  CHECK (RwWlogCount () == 0, "write log: empty after clear");
  Buf = AllocateZeroPool (16);
  if (Buf == NULL) {
    return;
  }

  Buf[1] = 0x12345678;
  Total  = RwWlogTotal ();
  RwMemSourceInit (&Mem);
  RwDsWrite (&Mem, (UINTN)&Buf[1], 4, 0xCAFEF00D);
  E = RwWlogGet (0);
  CHECK (RwWlogCount () == 1 && RwWlogTotal () == Total + 1, "write log: RwDsWrite adds an entry");
  CHECK (E != NULL && E->Address == (UINTN)&Buf[1] && E->Width == 4 && E->OldValid && E->Old == 0x12345678 && E->New == 0xCAFEF00D,
    "write log: address, width, old and new value");
  CHECK (E != NULL && !EFI_ERROR (E->Status) && E->Serial == Total + 1 && AsciiStrCmp (E->Space, Mem.Name) == 0, "write log: status, serial, space name");
  RwWlogFormat (E, Time, sizeof (Time), Addr, sizeof (Addr), Old, sizeof (Old), New, sizeof (New), Status, sizeof (Status));
  CHECK (AsciiStrCmp (Old, "12345678") == 0 && AsciiStrCmp (New, "CAFEF00D") == 0 && AsciiStrCmp (Status, "OK") == 0 &&
    AsciiStrLen (Addr) >= 8, "write log: formatted cells");

  //
  // Side-effect spaces are not read back.
  //
  RwWlogAdd ("I/O", 0x80, 1, FALSE, 0, 0x55, EFI_SUCCESS);
  RwWlogFormat (RwWlogGet (1), Time, sizeof (Time), Addr, sizeof (Addr), Old, sizeof (Old), New, sizeof (New), Status, sizeof (Status));
  CHECK (AsciiStrCmp (Old, "--") == 0 && AsciiStrCmp (New, "55") == 0 && AsciiStrCmp (Addr, "00000080") == 0, "write log: unknown old value shows --");
  RwWlogAdd ("x", 0, 8, TRUE, 1, 2, EFI_DEVICE_ERROR);
  RwWlogFormat (RwWlogGet (2), Time, sizeof (Time), Addr, sizeof (Addr), Old, sizeof (Old), New, sizeof (New), Status, sizeof (Status));
  CHECK (AsciiStrCmp (Status, "Device Error") == 0 && AsciiStrLen (New) == 16, "write log: failed write keeps its status");

  //
  // Ring: the newest RW_WLOG_MAX stay.
  //
  for (Index = 0; Index < RW_WLOG_MAX; Index++) {
    RwWlogAdd ("ring", Index, 1, FALSE, 0, Index & 0xFF, EFI_SUCCESS);
  }

  CHECK (RwWlogCount () == RW_WLOG_MAX && RwWlogGet (0)->Address == 0 && RwWlogGet (RW_WLOG_MAX - 1)->Address == RW_WLOG_MAX - 1 &&
    RwWlogGet (RW_WLOG_MAX) == NULL, "write log: ring keeps the newest 1000");
  RwStrInit (&Text);
  RwWlogToText (&Text, FALSE);
  CHECK (!Text.Oom && AsciiStrStr (Text.Buf, "1000 entries") != NULL && AsciiStrStr (Text.Buf, "ring") != NULL, "write log: text form");
  RwStrFree (&Text);
  RwWlogClear ();
  FreePool (Buf);
}

STATIC
VOID
TestFormats (
  VOID
  )
{
  STATIC CONST UINT32  Pixels[3 * 2] = { 0xFF0000, 0x00FF00, 0x0000FF, 0x123456, 0xABCDEF, 0x000000 };
  UINT8                *Bmp;
  UINTN                Size;
  RW_STR               Text;
  UINT8                Data[20];
  BOOLEAN              Valid[20];
  UINT8                Data2[20];
  BOOLEAN              Valid2[20];
  CHAR8                Name[RW_SNAP_NAME_MAX];
  UINT64               Page;
  UINT8                *File;
  UINTN                FileSize;
  UINTN                Index;

  //
  // BMP: 3x2, rows padded to 12 bytes, bottom row first, BGR.
  //
  CHECK (!EFI_ERROR (RwBmpEncode (Pixels, 3, 2, &Bmp, &Size)), "bmp: encode");
  if (Bmp != NULL) {
    CHECK (Size == 54 + 12 * 2 && Bmp[0] == 'B' && Bmp[1] == 'M' && *(UINT32 *)(Bmp + 2) == Size && *(UINT32 *)(Bmp + 10) == 54,
      "bmp: file header");
    CHECK (*(UINT32 *)(Bmp + 18) == 3 && *(UINT32 *)(Bmp + 22) == 2 && *(UINT16 *)(Bmp + 28) == 24, "bmp: 3 x 2, 24 bpp");
    CHECK (Bmp[54] == 0x56 && Bmp[55] == 0x34 && Bmp[56] == 0x12 && Bmp[66 + 0] == 0x00 && Bmp[66 + 2] == 0xFF &&
      Bmp[66 + 4] == 0xFF && Bmp[66 + 6] == 0xFF, "bmp: bottom-up rows of B, G, R");
    FreePool (Bmp);
  }

  CHECK (RwBmpEncode (Pixels, 0, 2, &Bmp, &Size) == EFI_INVALID_PARAMETER, "bmp: zero width refused");

  //
  // Hex text.
  //
  for (Index = 0; Index < sizeof (Data); Index++) {
    Data[Index]  = (UINT8)(0x41 + Index);
    Valid[Index] = (BOOLEAN)(Index != 3);
  }

  RwStrInit (&Text);
  RwHexText (Data, Valid, sizeof (Data), 0x1000, 8, FALSE, &Text);
  CHECK (AsciiStrnCmp (Text.Buf, "00001000: 41 42 43 -- 45", 24) == 0, "hex text: address, bytes, -- for unreadable");
  CHECK (AsciiStrStr (Text.Buf, "  ABC.EFGHIJKLMNOP\n") != NULL && AsciiStrStr (Text.Buf, "00001010: 51 52 53 54") != NULL,
    "hex text: ASCII column, second line");
  RwStrFree (&Text);

  //
  // Snapshot round trip.
  //
  CHECK (!EFI_ERROR (RwSnapEncode ("PCI 00:1F.0", 0x12345678900ULL, Data, Valid, sizeof (Data), &File, &FileSize)), "snapshot: encode");
  if (File != NULL) {
    CHECK (FileSize == RW_SNAP_HEADER + 20 + 3 && CompareMem (File, RW_SNAP_MAGIC, 8) == 0, "snapshot: size and magic");
    CHECK (!EFI_ERROR (RwSnapDecode (File, FileSize, sizeof (Data), Name, &Page, Data2, Valid2)), "snapshot: decode");
    CHECK (Page == 0x12345678900ULL && AsciiStrCmp (Name, "PCI 00:1F.0") == 0 && CompareMem (Data, Data2, sizeof (Data)) == 0 &&
      CompareMem (Valid, Valid2, sizeof (Valid)) == 0, "snapshot: page, name, data and valid bits round trip");
    CHECK (RwSnapDecode (File, FileSize, 16, Name, &Page, Data2, Valid2) == EFI_VOLUME_CORRUPTED, "snapshot: wrong size refused");
    File[0] = 'X';
    CHECK (RwSnapDecode (File, FileSize, sizeof (Data), Name, &Page, Data2, Valid2) == EFI_VOLUME_CORRUPTED, "snapshot: bad magic refused");
    FreePool (File);
  }
}

STATIC
VOID
TestDump (
  VOID
  )
{
  RW_STR  Text;

  RwStrInit (&Text);
  RwDumpAll (RW_DUMP_PCI | RW_DUMP_SMBIOS | RW_DUMP_SIO | RW_DUMP_EDID, TRUE, &Text);
  TestLog ("    dump of PCI, SMBIOS, Super I/O, EDID: %d bytes", Text.Len);
  CHECK (!Text.Oom && AsciiStrStr (Text.Buf, "==== PCI ====\r\n") != NULL && AsciiStrStr (Text.Buf, "8086") != NULL, "dump: PCI part");
  CHECK (AsciiStrStr (Text.Buf, "==== SMBIOS ====") != NULL && AsciiStrStr (Text.Buf, "BIOS Information") != NULL, "dump: SMBIOS part");
  CHECK (AsciiStrStr (Text.Buf, "No Super I/O chip") != NULL, "dump: a missing part says so");
  CHECK (AsciiStrStr (Text.Buf, "==== ACPI ====") == NULL, "dump: parts not asked for are left out");
  CHECK (AsciiStrStr (Text.Buf, "\n") == NULL || AsciiStrStr (Text.Buf, "\r\n") != NULL, "dump: CRLF line ends");
  RwStrFree (&Text);

  RwStrInit (&Text);
  RwDumpAll (RW_DUMP_ACPI | RW_DUMP_MSR, FALSE, &Text);
  CHECK (!Text.Oom && AsciiStrStr (Text.Buf, "DefinitionBlock") != NULL && AsciiStrStr (Text.Buf, "---- FACP") != NULL, "dump: ACPI with the DSDT as ASL");
  CHECK (AsciiStrStr (Text.Buf, "CPU 0") != NULL && AsciiStrStr (Text.Buf, "CPU 1") != NULL, "dump: MSRs of every CPU");
  RwStrFree (&Text);
}

VOID
TestTools (
  VOID
  )
{
  TestWlog ();
  TestFormats ();
  TestDump ();
}
