/** @file
  RW_INFO: structured information documents.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

VOID
RwInfoInit (
  OUT RW_INFO  *Info
  )
{
  ZeroMem (Info, sizeof (*Info));
}

VOID
RwInfoFree (
  IN OUT RW_INFO  *Info
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    FreePool (Info->Rows[Index].Name);
    FreePool (Info->Rows[Index].Value);
  }

  if (Info->Rows != NULL) {
    FreePool (Info->Rows);
  }

  ZeroMem (Info, sizeof (*Info));
}

STATIC
CHAR8 *
DupAscii (
  IN CONST CHAR8  *Text
  )
{
  if (Text == NULL) {
    Text = "";
  }

  return AllocateCopyPool (AsciiStrSize (Text), Text);
}

RW_INFO_ROW *
RwInfoAdd (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Kind,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Value
  )
{
  RW_INFO_ROW  *NewRows;
  RW_INFO_ROW  *Row;
  UINTN        NewCap;
  CHAR8        *N;
  CHAR8        *V;

  if (Info->Count == Info->Capacity) {
    NewCap  = (Info->Capacity == 0) ? 32 : Info->Capacity * 2;
    NewRows = ReallocatePool (
                Info->Capacity * sizeof (RW_INFO_ROW),
                NewCap * sizeof (RW_INFO_ROW),
                Info->Rows
                );
    if (NewRows == NULL) {
      Info->Oom = TRUE;
      return NULL;
    }

    Info->Rows     = NewRows;
    Info->Capacity = NewCap;
  }

  N = DupAscii (Name);
  V = DupAscii (Value);
  if ((N == NULL) || (V == NULL)) {
    if (N != NULL) {
      FreePool (N);
    }

    if (V != NULL) {
      FreePool (V);
    }

    Info->Oom = TRUE;
    return NULL;
  }

  Row           = &Info->Rows[Info->Count++];
  Row->Kind     = Kind;
  Row->Level    = Level;
  Row->Emphasis = Emphasis;
  Row->Reserved = 0;
  Row->Tag      = RW_INFO_NO_TAG;
  Row->Offset   = RW_INFO_NO_OFFSET;
  Row->Size     = 0;
  Row->Name     = N;
  Row->Value    = V;
  return Row;
}

RW_INFO_ROW *
EFIAPI
RwInfoSection (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  RW_STR       Str;
  VA_LIST      Marker;
  RW_INFO_ROW  *Row;

  RwStrInit (&Str);
  VA_START (Marker, Format);
  RwStrAppendV (&Str, Format, Marker);
  VA_END (Marker);
  Row = RwInfoAdd (Info, RwKindSection, Level, RwEmNormal, Str.Buf, NULL);
  RwStrFree (&Str);
  return Row;
}

RW_INFO_ROW *
EFIAPI
RwInfoField (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  RW_STR       Str;
  VA_LIST      Marker;
  RW_INFO_ROW  *Row;

  RwStrInit (&Str);
  VA_START (Marker, Format);
  RwStrAppendV (&Str, Format, Marker);
  VA_END (Marker);
  Row = RwInfoAdd (Info, RwKindField, Level, Emphasis, Name, Str.Buf);
  RwStrFree (&Str);
  return Row;
}

RW_INFO_ROW *
EFIAPI
RwInfoText (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  RW_STR       Str;
  VA_LIST      Marker;
  RW_INFO_ROW  *Row;

  RwStrInit (&Str);
  VA_START (Marker, Format);
  RwStrAppendV (&Str, Format, Marker);
  VA_END (Marker);
  Row = RwInfoAdd (Info, RwKindText, Level, Emphasis, Str.Buf, NULL);
  RwStrFree (&Str);
  return Row;
}

VOID
RwInfoSetReg (
  IN RW_INFO_ROW  *Row,
  IN UINT32       Offset,
  IN UINT32       Size
  )
{
  if (Row != NULL) {
    Row->Offset = Offset;
    Row->Size   = Size;
  }
}

VOID
RwInfoSetTag (
  IN RW_INFO_ROW  *Row,
  IN INT32        Tag
  )
{
  if (Row != NULL) {
    Row->Tag = Tag;
  }
}

VOID
RwInfoAddHex (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST UINT8  *Data,
  IN     UINTN        Size,
  IN     UINTN        Base
  )
{
  CHAR8  Line[96];
  UINTN  Offset;
  UINTN  Index;
  UINTN  Pos;
  UINT8  Byte;

  for (Offset = 0; Offset < Size; Offset += 16) {
    Pos = AsciiSPrint (Line, sizeof (Line), "%04X: ", Base + Offset);
    for (Index = 0; Index < 16; Index++) {
      if (Offset + Index < Size) {
        Pos += AsciiSPrint (Line + Pos, sizeof (Line) - Pos, "%02X ", Data[Offset + Index]);
      } else {
        Pos += AsciiSPrint (Line + Pos, sizeof (Line) - Pos, "   ");
      }
    }

    Line[Pos++] = ' ';
    for (Index = 0; (Index < 16) && (Offset + Index < Size); Index++) {
      Byte        = Data[Offset + Index];
      Line[Pos++] = ((Byte >= 0x20) && (Byte < 0x7F)) ? (CHAR8)Byte : '.';
    }

    Line[Pos] = '\0';
    RwInfoSetReg (
      RwInfoAdd (Info, RwKindText, Level, RwEmNormal, Line, NULL),
      (UINT32)(Base + Offset),
      (UINT32)MIN (16, Size - Offset)
      );
  }
}

VOID
RwInfoAppend (
  IN OUT RW_INFO        *Dst,
  IN     CONST RW_INFO  *Src,
  IN     UINT8          LevelShift
  )
{
  UINTN        Index;
  RW_INFO_ROW  *S;
  RW_INFO_ROW  *D;

  for (Index = 0; Index < Src->Count; Index++) {
    S = &Src->Rows[Index];
    D = RwInfoAdd (Dst, S->Kind, (UINT8)(S->Level + LevelShift), S->Emphasis, S->Name, S->Value);
    if (D != NULL) {
      D->Tag    = S->Tag;
      D->Offset = S->Offset;
      D->Size   = S->Size;
    }
  }
}

VOID
RwInfoToText (
  IN     CONST RW_INFO  *Info,
  IN OUT RW_STR         *Out,
  IN     BOOLEAN        Crlf
  )
{
  UINTN        Index;
  UINTN        Pad;
  UINTN        NameLen;
  UINTN        Width;
  UINTN        Scan;
  UINTN        RunWidth;
  RW_INFO_ROW  *Row;
  CONST CHAR8  *Eol;

  Eol      = Crlf ? "\r\n" : "\n";
  RunWidth = 0;
  for (Index = 0; Index < Info->Count; Index++) {
    Row = &Info->Rows[Index];
    for (Pad = 0; Pad < Row->Level; Pad++) {
      RwStrAppend (Out, "  ");
    }

    switch (Row->Kind) {
      case RwKindSection:
        RwStrAppend (Out, Row->Name);
        if (Row->Value[0] != '\0') {
          RwStrAppend (Out, "  ");
          RwStrAppend (Out, Row->Value);
        }

        break;

      case RwKindField:
        //
        // Align the values of a run of fields at the same level; the width
        // is measured once, at the first field of the run.
        //
        if ((Index == 0) || (Info->Rows[Index - 1].Kind != RwKindField) || (Info->Rows[Index - 1].Level != Row->Level)) {
          Width = 0;
          for (Scan = Index; Scan < Info->Count; Scan++) {
            if ((Info->Rows[Scan].Kind != RwKindField) || (Info->Rows[Scan].Level != Row->Level)) {
              break;
            }

            Width = MAX (Width, AsciiStrLen (Info->Rows[Scan].Name));
          }

          RunWidth = MIN (Width, 32);
        }

        NameLen = AsciiStrLen (Row->Name);
        RwStrAppend (Out, Row->Name);
        for (Pad = NameLen; Pad < RunWidth + 2; Pad++) {
          RwStrAppendChar (Out, ' ');
        }

        RwStrAppend (Out, Row->Value);
        break;

      default:
        RwStrAppend (Out, Row->Name);
        break;
    }

    RwStrAppend (Out, Eol);
  }
}
