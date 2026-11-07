/** @file
  RW_STR: growable CHAR8 strings.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

STATIC CHAR8  mEmpty[1] = { '\0' };

VOID
RwStrInit (
  OUT RW_STR  *Str
  )
{
  Str->Buf = mEmpty;
  Str->Len = 0;
  Str->Cap = 0;
  Str->Oom = FALSE;
}

VOID
RwStrFree (
  IN OUT RW_STR  *Str
  )
{
  if ((Str->Cap != 0) && (Str->Buf != NULL)) {
    FreePool (Str->Buf);
  }

  RwStrInit (Str);
}

VOID
RwStrReset (
  IN OUT RW_STR  *Str
  )
{
  Str->Len = 0;
  if (Str->Cap != 0) {
    Str->Buf[0] = '\0';
  }

  Str->Oom = FALSE;
}

CHAR8 *
RwStrDetach (
  IN OUT RW_STR  *Str
  )
{
  CHAR8  *Buf;

  if (Str->Cap == 0) {
    Buf = AllocateZeroPool (1);
  } else {
    Buf = Str->Buf;
  }

  RwStrInit (Str);
  return Buf;
}

STATIC
BOOLEAN
RwStrReserve (
  IN OUT RW_STR  *Str,
  IN     UINTN   Extra
  )
{
  UINTN  NewCap;
  CHAR8  *NewBuf;

  if (Str->Oom) {
    return FALSE;
  }

  if (Str->Len + Extra + 1 <= Str->Cap) {
    return TRUE;
  }

  NewCap = (Str->Cap == 0) ? 64 : Str->Cap;
  while (NewCap < Str->Len + Extra + 1) {
    NewCap *= 2;
  }

  NewBuf = AllocatePool (NewCap);
  if (NewBuf == NULL) {
    Str->Oom = TRUE;
    return FALSE;
  }

  CopyMem (NewBuf, Str->Buf, Str->Len);
  NewBuf[Str->Len] = '\0';
  if (Str->Cap != 0) {
    FreePool (Str->Buf);
  }

  Str->Buf = NewBuf;
  Str->Cap = NewCap;
  return TRUE;
}

VOID
RwStrAppendN (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Text,
  IN     UINTN        Count
  )
{
  if ((Text == NULL) || (Count == 0) || !RwStrReserve (Str, Count)) {
    return;
  }

  CopyMem (Str->Buf + Str->Len, Text, Count);
  Str->Len          += Count;
  Str->Buf[Str->Len] = '\0';
}

VOID
RwStrAppend (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Text
  )
{
  if (Text != NULL) {
    RwStrAppendN (Str, Text, AsciiStrLen (Text));
  }
}

VOID
RwStrAppendChar (
  IN OUT RW_STR  *Str,
  IN     CHAR8   Ch
  )
{
  RwStrAppendN (Str, &Ch, 1);
}

VOID
EFIAPI
RwStrAppendV (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Format,
  IN     VA_LIST      Marker
  )
{
  CHAR8    Small[256];
  UINTN    Need;
  CHAR8    *Big;
  VA_LIST  Copy;

  VA_COPY (Copy, Marker);
  Need = AsciiVSPrint (Small, sizeof (Small), Format, Copy);
  VA_END (Copy);
  if (Need < sizeof (Small) - 1) {
    RwStrAppendN (Str, Small, Need);
    return;
  }

  //
  // Possibly truncated: format again into a buffer that fits.
  //
  Big = AllocatePool (4096);
  if (Big == NULL) {
    RwStrAppendN (Str, Small, Need);
    return;
  }

  Need = AsciiVSPrint (Big, 4096, Format, Marker);
  RwStrAppendN (Str, Big, Need);
  FreePool (Big);
}

VOID
EFIAPI
RwStrAppendF (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Marker;

  VA_START (Marker, Format);
  RwStrAppendV (Str, Format, Marker);
  VA_END (Marker);
}

CHAR8 *
EFIAPI
RwSPrintAlloc (
  IN CONST CHAR8  *Format,
  ...
  )
{
  RW_STR   Str;
  VA_LIST  Marker;

  RwStrInit (&Str);
  VA_START (Marker, Format);
  RwStrAppendV (&Str, Format, Marker);
  VA_END (Marker);
  if (Str.Oom) {
    RwStrFree (&Str);
    return NULL;
  }

  return RwStrDetach (&Str);
}

VOID
RwUnicodeToAscii (
  IN  CONST CHAR16  *Src,
  OUT CHAR8         *Dst,
  IN  UINTN         DstSize
  )
{
  UINTN  Index;

  if (DstSize == 0) {
    return;
  }

  for (Index = 0; (Src != NULL) && (Src[Index] != 0) && (Index + 1 < DstSize); Index++) {
    Dst[Index] = ((Src[Index] >= 0x20) && (Src[Index] <= 0x7E)) ? (CHAR8)Src[Index] : '?';
  }

  Dst[Index] = '\0';
}

VOID
RwStrAppendBits (
  IN OUT RW_STR       *Str,
  IN     UINT64       Value,
  IN     CONST CHAR8  **Names,
  IN     UINTN        Count,
  IN     CONST CHAR8  *NoneText
  )
{
  UINTN    Bit;
  BOOLEAN  Any;

  Any = FALSE;
  for (Bit = 0; (Names != NULL) && (Bit < Count) && (Bit < 64); Bit++) {
    if ((Names[Bit] != NULL) && ((RShiftU64 (Value, Bit) & 1) != 0)) {
      if (Any || (Str->Len != 0)) {
        RwStrAppendChar (Str, ' ');
      }

      RwStrAppend (Str, Names[Bit]);
      Any = TRUE;
    }
  }

  if (!Any && (NoneText != NULL)) {
    if (Str->Len != 0) {
      RwStrAppendChar (Str, ' ');
    }

    RwStrAppend (Str, NoneText);
  }
}
