/** @file
  File formats of the tools: BMP, hex dump text, page snapshots.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define BMP_HEADER  54

STATIC
VOID
Put16 (
  OUT UINT8   *Ptr,
  IN  UINT16  Value
  )
{
  Ptr[0] = (UINT8)Value;
  Ptr[1] = (UINT8)(Value >> 8);
}

STATIC
VOID
Put32 (
  OUT UINT8   *Ptr,
  IN  UINT32  Value
  )
{
  Ptr[0] = (UINT8)Value;
  Ptr[1] = (UINT8)(Value >> 8);
  Ptr[2] = (UINT8)(Value >> 16);
  Ptr[3] = (UINT8)(Value >> 24);
}

EFI_STATUS
RwBmpEncode (
  IN  CONST UINT32  *Pixels,
  IN  UINTN         Width,
  IN  UINTN         Height,
  OUT UINT8         **Bmp,
  OUT UINTN         *Size
  )
{
  UINTN   RowBytes;
  UINTN   Total;
  UINT8   *Buf;
  UINT8   *Row;
  UINTN   X;
  UINTN   Y;
  UINT32  Pixel;

  *Bmp  = NULL;
  *Size = 0;
  if ((Pixels == NULL) || (Width == 0) || (Height == 0) || (Width > 16384) || (Height > 16384)) {
    return EFI_INVALID_PARAMETER;
  }

  RowBytes = (Width * 3 + 3) & ~(UINTN)3;
  Total    = BMP_HEADER + RowBytes * Height;
  Buf      = AllocateZeroPool (Total);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // BITMAPFILEHEADER + BITMAPINFOHEADER, 24 bpp, no compression, 96 dpi.
  //
  Buf[0] = 'B';
  Buf[1] = 'M';
  Put32 (Buf + 2, (UINT32)Total);
  Put32 (Buf + 10, BMP_HEADER);
  Put32 (Buf + 14, 40);
  Put32 (Buf + 18, (UINT32)Width);
  Put32 (Buf + 22, (UINT32)Height);
  Put16 (Buf + 26, 1);
  Put16 (Buf + 28, 24);
  Put32 (Buf + 34, (UINT32)(RowBytes * Height));
  Put32 (Buf + 38, 3780);
  Put32 (Buf + 42, 3780);

  for (Y = 0; Y < Height; Y++) {
    Row = Buf + BMP_HEADER + (Height - 1 - Y) * RowBytes;     // bottom-up
    for (X = 0; X < Width; X++) {
      Pixel          = Pixels[Y * Width + X];
      Row[X * 3]     = (UINT8)Pixel;             // blue
      Row[X * 3 + 1] = (UINT8)(Pixel >> 8);      // green
      Row[X * 3 + 2] = (UINT8)(Pixel >> 16);     // red
    }
  }

  *Bmp  = Buf;
  *Size = Total;
  return EFI_SUCCESS;
}

VOID
RwHexText (
  IN     CONST UINT8    *Data,
  IN     CONST BOOLEAN  *Valid  OPTIONAL,
  IN     UINTN          Size,
  IN     UINT64         Base,
  IN     UINTN          AddrDigits,
  IN     BOOLEAN        Crlf,
  IN OUT RW_STR         *Out
  )
{
  UINTN    Line;
  UINTN    Index;
  UINTN    Offset;
  BOOLEAN  Ok;
  CHAR8    Ch;

  if ((AddrDigits < 4) || (AddrDigits > 16)) {
    AddrDigits = 8;
  }

  for (Line = 0; Line < Size; Line += 16) {
    RwStrAppendF (Out, "%0*lX:", AddrDigits, Base + Line);
    for (Index = 0; Index < 16; Index++) {
      Offset = Line + Index;
      if (Index == 8) {
        RwStrAppendChar (Out, ' ');
      }

      if (Offset >= Size) {
        RwStrAppend (Out, "   ");
      } else if ((Valid != NULL) && !Valid[Offset]) {
        RwStrAppend (Out, " --");
      } else {
        RwStrAppendF (Out, " %02X", Data[Offset]);
      }
    }

    RwStrAppend (Out, "  ");
    for (Index = 0; (Index < 16) && (Line + Index < Size); Index++) {
      Offset = Line + Index;
      Ok     = (BOOLEAN)((Valid == NULL) || Valid[Offset]);
      Ch     = (CHAR8)Data[Offset];
      RwStrAppendChar (Out, (Ok && (Ch >= 0x20) && (Ch < 0x7F)) ? Ch : '.');
    }

    RwStrAppend (Out, Crlf ? "\r\n" : "\n");
  }
}

EFI_STATUS
RwSnapEncode (
  IN  CONST CHAR8    *Name,
  IN  UINT64         Page,
  IN  CONST UINT8    *Data,
  IN  CONST BOOLEAN  *Valid,
  IN  UINTN          Size,
  OUT UINT8          **File,
  OUT UINTN          *FileSize
  )
{
  UINT8  *Buf;
  UINTN  Total;
  UINTN  Index;

  *File     = NULL;
  *FileSize = 0;
  if ((Size == 0) || (Size > SIZE_1MB)) {
    return EFI_INVALID_PARAMETER;
  }

  Total = RW_SNAP_HEADER + Size + (Size + 7) / 8;
  Buf   = AllocateZeroPool (Total);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Buf, RW_SNAP_MAGIC, 8);
  Put32 (Buf + 8, (UINT32)Page);
  Put32 (Buf + 12, (UINT32)RShiftU64 (Page, 32));
  Put32 (Buf + 16, (UINT32)Size);
  if (Name != NULL) {
    AsciiStrnCpyS ((CHAR8 *)(Buf + 24), RW_SNAP_NAME_MAX, Name, RW_SNAP_NAME_MAX - 1);
  }

  CopyMem (Buf + RW_SNAP_HEADER, Data, Size);
  for (Index = 0; Index < Size; Index++) {
    if ((Valid == NULL) || Valid[Index]) {
      Buf[RW_SNAP_HEADER + Size + Index / 8] |= (UINT8)(1 << (Index % 8));
    }
  }

  *File     = Buf;
  *FileSize = Total;
  return EFI_SUCCESS;
}

STATIC
UINT32
Get32 (
  IN CONST UINT8  *Ptr
  )
{
  return (UINT32)Ptr[0] | ((UINT32)Ptr[1] << 8) | ((UINT32)Ptr[2] << 16) | ((UINT32)Ptr[3] << 24);
}

EFI_STATUS
RwSnapDecode (
  IN  CONST UINT8  *File,
  IN  UINTN        FileSize,
  IN  UINTN        Size,
  OUT CHAR8        *Name,
  OUT UINT64       *Page,
  OUT UINT8        *Data,
  OUT BOOLEAN      *Valid
  )
{
  UINTN  Index;

  if ((File == NULL) || (FileSize != RW_SNAP_HEADER + Size + (Size + 7) / 8) ||
      (CompareMem (File, RW_SNAP_MAGIC, 8) != 0) || (Get32 (File + 16) != Size))
  {
    return EFI_VOLUME_CORRUPTED;
  }

  *Page = LShiftU64 (Get32 (File + 12), 32) | Get32 (File + 8);
  CopyMem (Name, File + 24, RW_SNAP_NAME_MAX);
  Name[RW_SNAP_NAME_MAX - 1] = '\0';
  CopyMem (Data, File + RW_SNAP_HEADER, Size);
  for (Index = 0; Index < Size; Index++) {
    Valid[Index] = (BOOLEAN)((File[RW_SNAP_HEADER + Size + Index / 8] & (1 << (Index % 8))) != 0);
  }

  return EFI_SUCCESS;
}
