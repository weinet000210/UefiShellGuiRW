/** @file
  RwCoreTest: strings, info documents and data source helpers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

STATIC UINT8  mBytes[32];

STATIC
EFI_STATUS
EFIAPI
ArrayRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  if ((Address <= 5) && (Address + Width > 5)) {
    return EFI_DEVICE_ERROR;     // a hole, to test RwDsReadBlock
  }

  *Value = 0;
  CopyMem (Value, &mBytes[Address], Width);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ArrayWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  CopyMem (&mBytes[Address], &Value, Width);
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
ArrayDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  return (BOOLEAN)((Address <= 0x10) && (Address + Width > 0x10));
}

VOID
TestBase (
  VOID
  )
{
  RW_STR          Str;
  RW_INFO         Info;
  RW_INFO_ROW     *Row;
  RW_DATA_SOURCE  Ds;
  UINT64          Value;
  UINT8           Buf[8];
  UINT8           Valid[1];
  UINTN           Index;
  CHAR8           *Text;
  CONST CHAR8     *Bits[] = { "A", NULL, "C" };
  CHAR8           Ascii[8];

  RwStrInit (&Str);
  CHECK ((Str.Buf != NULL) && (Str.Buf[0] == '\0'), "empty RW_STR is a valid string");
  RwStrAppendF (&Str, "%a-%04X", "x", 0x1B);
  CHECK (AsciiStrCmp (Str.Buf, "x-001B") == 0, "RwStrAppendF formats with %a");
  for (Index = 0; Index < 200; Index++) {
    RwStrAppend (&Str, "0123456789");
  }

  CHECK ((Str.Len == 2006) && !Str.Oom && (AsciiStrLen (Str.Buf) == 2006), "RW_STR grows past 2 KB");
  RwStrReset (&Str);
  RwStrAppendBits (&Str, 5, Bits, 3, "none");
  CHECK (AsciiStrCmp (Str.Buf, "A C") == 0, "RwStrAppendBits names set bits");
  RwStrReset (&Str);
  RwStrAppendBits (&Str, 2, Bits, 3, "none");
  CHECK (AsciiStrCmp (Str.Buf, "none") == 0, "RwStrAppendBits falls back to NoneText");
  RwStrFree (&Str);

  Text = RwSPrintAlloc ("%d %a", 42, "ok");
  CHECK ((Text != NULL) && (AsciiStrCmp (Text, "42 ok") == 0), "RwSPrintAlloc");
  if (Text != NULL) {
    FreePool (Text);
  }

  RwUnicodeToAscii (L"Ab\x00e9z", Ascii, sizeof (Ascii));
  CHECK (AsciiStrCmp (Ascii, "Ab?z") == 0, "RwUnicodeToAscii replaces non-ASCII");

  RwInfoInit (&Info);
  RwInfoSection (&Info, 0, "Header");
  Row = RwInfoField (&Info, 1, RwEmStrong, "Vendor", "%04X", 0x8086);
  RwInfoSetReg (Row, 0, 2);
  RwInfoField (&Info, 1, RwEmNormal, "Device ID", "%04X", 0x29C0);
  RwInfoText (&Info, 1, RwEmDim, "free text");
  RwInfoAddHex (&Info, 1, (CONST UINT8 *)"0123456789ABCDEFxyz", 19, 0x100);
  CHECK (!Info.Oom && (Info.Count == 6), "RW_INFO collects rows");
  CHECK ((Row != NULL) && (Row->Offset == 0) && (Row->Size == 2) && (Row->Emphasis == RwEmStrong), "row keeps register and emphasis");
  CHECK ((Info.Rows[5].Offset == 0x110) && (Info.Rows[5].Size == 3), "hex rows record offsets");
  RwStrInit (&Str);
  RwInfoToText (&Info, &Str, FALSE);
  TestLog ("%a", Str.Buf);
  CHECK (AsciiStrStr (Str.Buf, "  Vendor     8086\n") != NULL, "RwInfoToText aligns fields");
  CHECK (AsciiStrStr (Str.Buf, "  0100: 30 31 32") != NULL, "RwInfoToText prints hex rows");
  RwStrFree (&Str);
  RwInfoFree (&Info);
  CHECK ((Info.Count == 0) && (Info.Rows == NULL), "RwInfoFree resets");

  for (Index = 0; Index < sizeof (mBytes); Index++) {
    mBytes[Index] = (UINT8)Index;
  }

  ZeroMem (&Ds, sizeof (Ds));
  Ds.Base        = 0;
  Ds.Limit       = sizeof (mBytes) - 1;
  Ds.WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32;
  Ds.Read        = ArrayRead;
  Ds.Write       = ArrayWrite;
  Ds.IsDangerous = ArrayDangerous;
  CHECK (RwDsSize (&Ds) == 32, "RwDsSize");
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 8, 4, &Value)) && (Value == 0x0B0A0908), "RwDsRead 32-bit");
  CHECK (EFI_ERROR (RwDsRead (&Ds, 30, 4, &Value)), "RwDsRead past Limit fails");
  CHECK (EFI_ERROR (RwDsRead (&Ds, 0, 8, &Value)), "RwDsRead with a width outside WidthMask fails");
  CHECK (!EFI_ERROR (RwDsWrite (&Ds, 2, 2, 0xBEEF)) && (mBytes[2] == 0xEF) && (mBytes[3] == 0xBE), "RwDsWrite 16-bit");
  CHECK (RwDsIsDangerous (&Ds, 0x0E, 4) && !RwDsIsDangerous (&Ds, 0, 4), "RwDsIsDangerous");
  CHECK (EFI_ERROR (RwDsReadBlock (&Ds, 2, 6, Buf, Valid)), "RwDsReadBlock reports the hole");
  CHECK ((Buf[0] == 0xEF) && (Buf[2] == 4) && (Buf[3] == 0xFF) && (Buf[5] == 7) && (Valid[0] == 0x37), "RwDsReadBlock fills bytes and Valid bits");
  Ds.Write = NULL;
  CHECK (RwDsWrite (&Ds, 0, 1, 0) == EFI_WRITE_PROTECTED, "read-only source refuses writes");
}
