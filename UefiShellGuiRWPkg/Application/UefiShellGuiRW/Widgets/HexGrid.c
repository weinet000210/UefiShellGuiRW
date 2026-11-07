/** @file
  Hex grid: one 256-byte page of an RW_DATA_SOURCE, drawn in a single
  object (address column, 16 bytes per row as byte / word / dword cells,
  ASCII column), with in-place hex editing.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

#define GRID_PAD        4
#define GRID_ROWS       16
#define GRID_BYTES_ROW  16
#define DEFAULT_PERIOD  1000

struct _RW_HEXGRID {
  RW_WIDGET               Base;
  RW_DATA_SOURCE          *Source;
  UINT64                  Page;                     ///< address of byte 0 of the page
  UINTN                   Width;                    ///< 1, 2 or 4
  UINTN                   Cursor;                   ///< page offset, aligned to Width
  UINT8                   Data[RW_HEXGRID_PAGE];
  UINT8                   Prev[RW_HEXGRID_PAGE];
  BOOLEAN                 Valid[RW_HEXGRID_PAGE];
  BOOLEAN                 PrevValid[RW_HEXGRID_PAGE];
  BOOLEAN                 InRange[RW_HEXGRID_PAGE];
  BOOLEAN                 Changed[RW_HEXGRID_PAGE];
  BOOLEAN                 HavePrev;
  BOOLEAN                 Editing;
  UINTN                   EditDigits;
  UINT64                  EditValue;
  BOOLEAN                 PendingWrite;             ///< a dangerous write waits for the dialog
  UINT64                  PendingAddress;
  UINTN                   PendingWidth;
  UINT64                  PendingValue;
  lv_timer_t              *Timer;
  BOOLEAN                 AutoRefresh;
  RW_HEXGRID_CURSOR_FN    OnCursor;
  RW_HEXGRID_WRITE_FN     OnWrite;
  VOID                    *Context;
  CONST lv_font_t         *Font;
  INT32                   CharW;
  INT32                   RowH;
  UINTN                   AddrChars;
};

//
// ---- Layout --------------------------------------------------------------------
//

STATIC
INT32
AddrX (
  IN RW_HEXGRID  *Grid
  )
{
  return GRID_PAD;
}

STATIC
INT32
HexX (
  IN RW_HEXGRID  *Grid
  )
{
  return GRID_PAD + (INT32)(Grid->AddrChars + 2) * Grid->CharW;
}

/**
  X of the cell holding page offset Offset (relative to the object).
**/
STATIC
INT32
CellX (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset
  )
{
  UINTN  Column;
  INT32  X;

  Column = (Offset % GRID_BYTES_ROW) / Grid->Width;
  X      = HexX (Grid) + (INT32)(Column * (Grid->Width * 2 + 1)) * Grid->CharW;
  if ((Offset % GRID_BYTES_ROW) >= GRID_BYTES_ROW / 2) {
    X += Grid->CharW;      // gap between the two halves of a row
  }

  return X;
}

STATIC
INT32
AsciiX (
  IN RW_HEXGRID  *Grid
  )
{
  //
  // Fixed at the byte layout, the widest, so the grid keeps its size.
  //
  return HexX (Grid) + (INT32)(GRID_BYTES_ROW * 3 + 1 + 2) * Grid->CharW;
}

STATIC
INT32
RowY (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Row
  )
{
  return GRID_PAD + (INT32)(Row + 1) * Grid->RowH + 3;
}

STATIC
VOID
UpdateSize (
  IN RW_HEXGRID  *Grid
  )
{
  Grid->Font      = RwMonoFont ();
  Grid->CharW     = lv_font_get_glyph_width (Grid->Font, '0', 0);
  Grid->RowH      = lv_font_get_line_height (Grid->Font) + 2;
  Grid->AddrChars = (Grid->Source != NULL) ? MAX (Grid->Source->AddrDigits, 4) : 4;
  lv_obj_set_size (
    Grid->Base.Obj,
    AsciiX (Grid) + GRID_BYTES_ROW * Grid->CharW + GRID_PAD + 2,
    RowY (Grid, GRID_ROWS) + GRID_PAD + 2
    );
}

//
// ---- Data --------------------------------------------------------------------
//

STATIC
UINT64
CellValue (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset,
  IN UINTN       Width,
  OUT BOOLEAN    *Valid
  )
{
  UINT64  Value;
  UINTN   Index;

  Value  = 0;
  *Valid = TRUE;
  for (Index = 0; Index < Width; Index++) {
    if ((Offset + Index >= RW_HEXGRID_PAGE) || !Grid->Valid[Offset + Index]) {
      *Valid = FALSE;
    } else {
      Value |= LShiftU64 (Grid->Data[Offset + Index], Index * 8);
    }
  }

  return Value;
}

STATIC
VOID
NotifyCursor (
  IN RW_HEXGRID  *Grid
  )
{
  UINT64   Value;
  BOOLEAN  Valid;

  if (Grid->OnCursor == NULL) {
    return;
  }

  Value = CellValue (Grid, Grid->Cursor, Grid->Width, &Valid);
  Grid->OnCursor (Grid, Grid->Page + Grid->Cursor, Grid->Width, Value, Valid, Grid->Context);
}

/**
  Read the page in the view width (registers often care about the access
  width); a cell the source cannot read in that width is read byte by byte.
**/
STATIC
VOID
ReadPage (
  IN RW_HEXGRID  *Grid,
  IN BOOLEAN     KeepPrev
  )
{
  RW_DATA_SOURCE  *Src;
  UINTN           Offset;
  UINTN           Index;
  UINT64          Address;
  UINT64          Value;
  UINT64          Byte;
  EFI_STATUS      Status;
  UINTN           WidthBit;

  Src = Grid->Source;
  if (KeepPrev) {
    CopyMem (Grid->Prev, Grid->Data, sizeof (Grid->Data));
    CopyMem (Grid->PrevValid, Grid->Valid, sizeof (Grid->Valid));
  }

  SetMem (Grid->Valid, sizeof (Grid->Valid), FALSE);
  SetMem (Grid->InRange, sizeof (Grid->InRange), FALSE);
  SetMem (Grid->Data, sizeof (Grid->Data), 0xFF);
  if (Src == NULL) {
    return;
  }

  WidthBit = (Grid->Width == 1) ? RW_DS_WIDTH_8 : ((Grid->Width == 2) ? RW_DS_WIDTH_16 : RW_DS_WIDTH_32);
  for (Offset = 0; Offset < RW_HEXGRID_PAGE; Offset += Grid->Width) {
    Address = Grid->Page + Offset;
    if ((Address < Src->Base) || (Address > Src->Limit)) {
      continue;
    }

    for (Index = 0; Index < Grid->Width; Index++) {
      Grid->InRange[Offset + Index] = (BOOLEAN)(Address + Index <= Src->Limit);
    }

    Status = EFI_UNSUPPORTED;
    if (((Src->WidthMask & WidthBit) != 0) && (Grid->Width - 1 <= Src->Limit - Address)) {
      Status = RwDsRead (Src, Address, Grid->Width, &Value);
      if (!EFI_ERROR (Status)) {
        for (Index = 0; Index < Grid->Width; Index++) {
          Grid->Data[Offset + Index]  = (UINT8)RShiftU64 (Value, Index * 8);
          Grid->Valid[Offset + Index] = TRUE;
        }

        continue;
      }
    }

    for (Index = 0; Index < Grid->Width; Index++) {
      if (Grid->InRange[Offset + Index] && !EFI_ERROR (RwDsRead (Src, Address + Index, 1, &Byte))) {
        Grid->Data[Offset + Index]  = (UINT8)Byte;
        Grid->Valid[Offset + Index] = TRUE;
      }
    }
  }

  for (Offset = 0; Offset < RW_HEXGRID_PAGE; Offset++) {
    Grid->Changed[Offset] = (BOOLEAN)(Grid->HavePrev && Grid->Valid[Offset] && Grid->PrevValid[Offset] &&
                                      (Grid->Data[Offset] != Grid->Prev[Offset]));
  }

  Grid->HavePrev = TRUE;
}

VOID
RwHexGridRefresh (
  IN RW_HEXGRID  *Grid
  )
{
  ReadPage (Grid, TRUE);
  lv_obj_invalidate (Grid->Base.Obj);
  NotifyCursor (Grid);
}

STATIC
VOID
Reload (
  IN RW_HEXGRID  *Grid
  )
{
  //
  // New page or width: nothing to compare against.
  //
  Grid->HavePrev = FALSE;
  ReadPage (Grid, FALSE);
  lv_obj_invalidate (Grid->Base.Obj);
  NotifyCursor (Grid);
}

//
// ---- Writing -------------------------------------------------------------------
//

STATIC
VOID
DoWrite (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address,
  IN UINTN       Width,
  IN UINT64      Value
  )
{
  EFI_STATUS  Status;
  CHAR8       Text[96];

  Status = RwDsWrite (Grid->Source, Address, Width, Value);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "Write of %0*lXh to %lXh failed: %r", Width * 2, Value, Address, Status);
  } else {
    AsciiSPrint (Text, sizeof (Text), "Wrote %0*lXh to %lXh", Width * 2, Value, Address);
  }

  if (Grid->Base.Window != NULL) {
    RwStatusSetText (Text);
  }

  RwHexGridRefresh (Grid);
  if (Grid->OnWrite != NULL) {
    Grid->OnWrite (Grid, Address, Width, Value, Status, Grid->Context);
  }
}

STATIC
VOID
ConfirmDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_HEXGRID  *Grid;

  Grid = (RW_HEXGRID *)Context;
  if (!Grid->PendingWrite) {
    return;
  }

  Grid->PendingWrite = FALSE;
  if (Accepted) {
    DoWrite (Grid, Grid->PendingAddress, Grid->PendingWidth, Grid->PendingValue);
  } else if (Grid->Base.Window != NULL) {
    RwStatusSetText ("Write cancelled");
  }

  lv_obj_invalidate (Grid->Base.Obj);
}

VOID
RwHexGridWrite (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address,
  IN UINTN       Width,
  IN UINT64      Value
  )
{
  CHAR8  Text[200];

  if ((Grid->Source == NULL) || (Grid->Source->Write == NULL)) {
    RwStatusSetText ("This space is read-only");
    return;
  }

  if (RwDsIsDangerous (Grid->Source, Address, Width)) {
    Grid->PendingWrite   = TRUE;
    Grid->PendingAddress = Address;
    Grid->PendingWidth   = Width;
    Grid->PendingValue   = Value;
    AsciiSPrint (
      Text,
      sizeof (Text),
      "Writing %0*lXh to %lXh in %a may reset, hang or reconfigure the machine.\n\nWrite it?",
      Width * 2,
      Value,
      Address,
      Grid->Source->Name
      );
    RwDialogConfirm ("Confirm Write", Text, RW_DIALOG_WARNING, ConfirmDone, Grid);
    return;
  }

  DoWrite (Grid, Address, Width, Value);
}

STATIC
VOID
EndEdit (
  IN RW_HEXGRID  *Grid,
  IN BOOLEAN     Commit
  )
{
  UINT64  Address;
  UINTN   Width;
  UINT64  Value;

  if (!Grid->Editing) {
    return;
  }

  Grid->Editing = FALSE;
  lv_obj_invalidate (Grid->Base.Obj);
  if (!Commit || (Grid->EditDigits == 0)) {
    return;
  }

  Address = Grid->Page + Grid->Cursor;
  Width   = Grid->Width;
  Value   = Grid->EditValue;
  RwHexGridWrite (Grid, Address, Width, Value);
}

//
// ---- Cursor --------------------------------------------------------------------
//

STATIC
VOID
SetCursor (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset
  )
{
  if (Offset >= RW_HEXGRID_PAGE) {
    return;
  }

  EndEdit (Grid, FALSE);
  Grid->Cursor = Offset & ~(Grid->Width - 1);
  lv_obj_invalidate (Grid->Base.Obj);
  NotifyCursor (Grid);
}

STATIC
BOOLEAN
ChangePage (
  IN RW_HEXGRID  *Grid,
  IN BOOLEAN     Down
  )
{
  UINT64  Page;

  if (Grid->Source == NULL) {
    return FALSE;
  }

  if (Down) {
    if ((Grid->Page + RW_HEXGRID_PAGE < Grid->Page) || (Grid->Page + RW_HEXGRID_PAGE > Grid->Source->Limit)) {
      return FALSE;
    }

    Page = Grid->Page + RW_HEXGRID_PAGE;
  } else {
    if ((Grid->Page < RW_HEXGRID_PAGE) || (Grid->Page - 1 < Grid->Source->Base)) {
      return FALSE;
    }

    Page = Grid->Page - RW_HEXGRID_PAGE;
  }

  EndEdit (Grid, FALSE);
  Grid->Page = Page;
  Reload (Grid);
  return TRUE;
}

VOID
RwHexGridGoTo (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address
  )
{
  if (Grid->Source == NULL) {
    return;
  }

  if (Address < Grid->Source->Base) {
    Address = Grid->Source->Base;
  }

  if (Address > Grid->Source->Limit) {
    Address = Grid->Source->Limit;
  }

  EndEdit (Grid, FALSE);
  Grid->Page   = Address & ~(UINT64)(RW_HEXGRID_PAGE - 1);
  Grid->Cursor = (UINTN)(Address - Grid->Page) & ~(Grid->Width - 1);
  Reload (Grid);
}

STATIC
VOID
GoToDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_HEXGRID  *Grid;
  CHAR8       Text[64];

  Grid = (RW_HEXGRID *)Context;
  if (!Accepted || (Grid->Source == NULL)) {
    return;
  }

  if ((Value < Grid->Source->Base) || (Value > Grid->Source->Limit)) {
    AsciiSPrint (Text, sizeof (Text), "%lXh is outside %lXh-%lXh", Value, Grid->Source->Base, Grid->Source->Limit);
    RwStatusSetText (Text);
    return;
  }

  RwHexGridGoTo (Grid, Value);
}

//
// ---- Drawing -------------------------------------------------------------------
//

STATIC
VOID
FormatAddress (
  IN  RW_HEXGRID  *Grid,
  IN  UINT64      Address,
  OUT CHAR8       *Buffer
  )
{
  UINTN  Index;
  UINTN  Digits;

  Digits = Grid->AddrChars;
  for (Index = 0; Index < Digits; Index++) {
    Buffer[Index] = "0123456789ABCDEF"[RShiftU64 (Address, (Digits - 1 - Index) * 4) & 0xF];
  }

  Buffer[Digits] = '\0';
}

/**
  Cell text: the bytes high to low, "--" for an unreadable byte, the typed
  digits and "_" while editing, blanks outside the space.
**/
STATIC
VOID
FormatCell (
  IN  RW_HEXGRID  *Grid,
  IN  UINTN       Offset,
  OUT CHAR8       *Buffer,
  OUT BOOLEAN     *Changed
  )
{
  UINTN  Index;
  UINTN  Byte;
  UINTN  Pos;
  UINTN  Digits;

  *Changed = FALSE;
  Digits   = Grid->Width * 2;
  if (Grid->Editing && (Offset == Grid->Cursor)) {
    for (Pos = 0; Pos < Digits; Pos++) {
      Buffer[Pos] = (Pos < Grid->EditDigits) ?
                    "0123456789ABCDEF"[RShiftU64 (Grid->EditValue, (Grid->EditDigits - 1 - Pos) * 4) & 0xF] :
                    '_';
    }

    Buffer[Digits] = '\0';
    return;
  }

  Pos = 0;
  for (Index = Grid->Width; Index > 0; Index--) {
    Byte = Offset + Index - 1;
    if (!Grid->InRange[Byte]) {
      Buffer[Pos++] = ' ';
      Buffer[Pos++] = ' ';
    } else if (!Grid->Valid[Byte]) {
      Buffer[Pos++] = '-';
      Buffer[Pos++] = '-';
    } else {
      Buffer[Pos++] = "0123456789ABCDEF"[Grid->Data[Byte] >> 4];
      Buffer[Pos++] = "0123456789ABCDEF"[Grid->Data[Byte] & 0xF];
      if (Grid->Changed[Byte]) {
        *Changed = TRUE;
      }
    }
  }

  Buffer[Pos] = '\0';
}

STATIC
VOID
DrawLine (
  IN lv_layer_t  *Layer,
  IN INT32       X1,
  IN INT32       Y1,
  IN INT32       X2,
  IN INT32       Y2
  )
{
  lv_draw_line_dsc_t  Line;

  lv_draw_line_dsc_init (&Line);
  Line.color = lv_color_hex (RW_COLOR_SHADOW);
  Line.width = 1;
  Line.p1.x  = X1;
  Line.p1.y  = Y1;
  Line.p2.x  = X2;
  Line.p2.y  = Y2;
  lv_draw_line (Layer, &Line);
}

STATIC
VOID
OnDraw (
  lv_event_t  *Event
  )
{
  RW_HEXGRID  *Grid;
  lv_layer_t  *Layer;
  lv_area_t   Obj = { 0 };
  lv_area_t   Box;
  CHAR8       Text[24];
  UINTN       Row;
  UINTN       Offset;
  UINTN       Index;
  BOOLEAN     Changed;
  BOOLEAN     IsCursor;
  INT32       X0;
  INT32       Y0;
  INT32       CellW;
  lv_color_t  Color;
  lv_color_t  Header;
  UINT8       Byte;

  Grid  = (RW_HEXGRID *)lv_event_get_user_data (Event);
  Layer = lv_event_get_layer (Event);
  lv_obj_get_coords (Grid->Base.Obj, &Obj);
  X0     = Obj.x1 + 1;
  Y0     = Obj.y1 + 1;
  CellW  = (INT32)Grid->Width * 2 * Grid->CharW;
  Header = lv_color_hex (RW_COLOR_STRONG);

  //
  // Header: column offsets and the ASCII ruler.
  //
  Box.y1 = Y0 + GRID_PAD;
  Box.y2 = Box.y1 + Grid->RowH - 1;
  for (Offset = 0; Offset < GRID_BYTES_ROW; Offset += Grid->Width) {
    AsciiSPrint (Text, sizeof (Text), "%0*X", Grid->Width * 2, (UINT32)Offset);
    Box.x1 = X0 + CellX (Grid, Offset);
    Box.x2 = Box.x1 + CellW - 1;
    RwDrawText (Layer, Grid->Font, &Box, Text, Header);
  }

  Box.x1 = X0 + AsciiX (Grid);
  Box.x2 = Box.x1 + GRID_BYTES_ROW * Grid->CharW;
  RwDrawText (Layer, Grid->Font, &Box, "0123456789ABCDEF", Header);
  DrawLine (Layer, Obj.x1 + 1, Y0 + RowY (Grid, 0) - 2, Obj.x2 - 1, Y0 + RowY (Grid, 0) - 2);
  DrawLine (Layer, X0 + HexX (Grid) - Grid->CharW, Obj.y1 + 1, X0 + HexX (Grid) - Grid->CharW, Obj.y2 - 1);
  DrawLine (Layer, X0 + AsciiX (Grid) - Grid->CharW, Obj.y1 + 1, X0 + AsciiX (Grid) - Grid->CharW, Obj.y2 - 1);

  for (Row = 0; Row < GRID_ROWS; Row++) {
    Box.y1 = Y0 + RowY (Grid, Row);
    Box.y2 = Box.y1 + Grid->RowH - 1;
    if (Box.y1 > Layer->_clip_area.y2) {
      break;
    }

    if (Box.y2 < Layer->_clip_area.y1) {
      continue;
    }

    //
    // Address.
    //
    FormatAddress (Grid, Grid->Page + Row * GRID_BYTES_ROW, Text);
    Box.x1 = X0 + AddrX (Grid);
    Box.x2 = Box.x1 + (INT32)Grid->AddrChars * Grid->CharW;
    RwDrawText (Layer, Grid->Font, &Box, Text, Header);

    //
    // Cells.
    //
    for (Index = 0; Index < GRID_BYTES_ROW; Index += Grid->Width) {
      Offset   = Row * GRID_BYTES_ROW + Index;
      IsCursor = (BOOLEAN)(Offset == Grid->Cursor);
      FormatCell (Grid, Offset, Text, &Changed);
      Box.x1 = X0 + CellX (Grid, Offset);
      Box.x2 = Box.x1 + CellW - 1;
      Color  = Changed ? lv_color_hex (RW_COLOR_CHANGED) : lv_color_black ();
      if (IsCursor && Grid->Editing) {
        RwDrawFill (Layer, &Box, lv_color_hex (RW_COLOR_EDIT_BG));
        Color = lv_color_black ();
      } else if (IsCursor) {
        RwDrawFill (Layer, &Box, lv_color_hex (Grid->Base.Focused ? RW_COLOR_HILITE : RW_COLOR_SEL_INACTIVE));
        if (Grid->Base.Focused) {
          Color = lv_color_white ();
        }
      }

      RwDrawText (Layer, Grid->Font, &Box, Text, Color);
    }

    //
    // ASCII.
    //
    for (Index = 0; Index < GRID_BYTES_ROW; Index++) {
      Offset = Row * GRID_BYTES_ROW + Index;
      Box.x1 = X0 + AsciiX (Grid) + (INT32)Index * Grid->CharW;
      Box.x2 = Box.x1 + Grid->CharW - 1;
      if ((Offset >= Grid->Cursor) && (Offset < Grid->Cursor + Grid->Width)) {
        RwDrawFill (Layer, &Box, lv_color_hex (RW_COLOR_SEL_INACTIVE));
      }

      if (!Grid->InRange[Offset]) {
        continue;
      }

      Byte    = Grid->Data[Offset];
      Text[0] = (Grid->Valid[Offset] && (Byte >= 0x20) && (Byte < 0x7F)) ? (CHAR8)Byte : '.';
      Text[1] = '\0';
      RwDrawText (Layer, Grid->Font, &Box, Text, Grid->Changed[Offset] ? lv_color_hex (RW_COLOR_CHANGED) : lv_color_black ());
    }
  }
}

//
// ---- Input ---------------------------------------------------------------------
//

INTN
RwHexGridHitTest (
  IN RW_HEXGRID  *Grid,
  IN INT32       X,
  IN INT32       Y
  )
{
  lv_area_t  Obj = { 0 };
  INT32      Rx;
  INT32      Ry;
  UINTN      Row;
  UINTN      Offset;

  lv_obj_get_coords (Grid->Base.Obj, &Obj);
  Rx = X - Obj.x1 - 1;
  Ry = Y - Obj.y1 - 1;
  if ((Ry < RowY (Grid, 0)) || (Ry >= RowY (Grid, GRID_ROWS))) {
    return -1;
  }

  Row = (UINTN)((Ry - RowY (Grid, 0)) / Grid->RowH);
  if ((Rx >= AsciiX (Grid)) && (Rx < AsciiX (Grid) + GRID_BYTES_ROW * Grid->CharW)) {
    return (INTN)(Row * GRID_BYTES_ROW + (UINTN)((Rx - AsciiX (Grid)) / Grid->CharW));
  }

  for (Offset = 0; Offset < GRID_BYTES_ROW; Offset += Grid->Width) {
    if ((Rx >= CellX (Grid, Offset) - Grid->CharW / 2) &&
        (Rx < CellX (Grid, Offset) + (INT32)(Grid->Width * 2) * Grid->CharW + Grid->CharW / 2))
    {
      return (INTN)(Row * GRID_BYTES_ROW + Offset);
    }
  }

  return -1;
}

STATIC
VOID
OnPressed (
  lv_event_t  *Event
  )
{
  RW_HEXGRID  *Grid;
  lv_point_t  Point;
  INTN        Offset;

  Grid = (RW_HEXGRID *)lv_event_get_user_data (Event);
  if (lv_indev_active () == NULL) {
    return;
  }

  lv_indev_get_point (lv_indev_active (), &Point);
  Offset = RwHexGridHitTest (Grid, Point.x, Point.y);
  if (Offset >= 0) {
    SetCursor (Grid, (UINTN)Offset);
  }
}

STATIC
VOID
OnTimer (
  lv_timer_t  *Timer
  )
{
  RW_HEXGRID  *Grid;

  Grid = (RW_HEXGRID *)lv_timer_get_user_data (Timer);
  if (!Grid->AutoRefresh || Grid->Editing || Grid->PendingWrite || RwDialogIsOpen () ||
      (Grid->Base.Window == NULL) || Grid->Base.Window->Minimized)
  {
    return;
  }

  RwHexGridRefresh (Grid);
}

STATIC
BOOLEAN
OnKey (
  IN RW_WIDGET                 *Widget,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_HEXGRID  *Grid;
  INTN        Digit;
  UINTN       Row;
  BOOLEAN     Ctrl;

  Grid = (RW_HEXGRID *)Widget;
  if (Grid->Source == NULL) {
    return FALSE;
  }

  Ctrl = (BOOLEAN)((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0);

  //
  // Editing.
  //
  Digit = RwKeyHexDigit (Key);
  if (Digit >= 0) {
    if (Grid->Source->Write == NULL) {
      RwStatusSetText ("This space is read-only");
      return TRUE;
    }

    if (!Grid->Editing) {
      Grid->Editing    = TRUE;
      Grid->EditDigits = 0;
      Grid->EditValue  = 0;
    }

    Grid->EditValue = LShiftU64 (Grid->EditValue, 4) | (UINT64)Digit;
    Grid->EditDigits++;
    lv_obj_invalidate (Grid->Base.Obj);
    if (Grid->EditDigits == Grid->Width * 2) {
      Row = Grid->Cursor;
      EndEdit (Grid, TRUE);
      //
      // Like typing in RW-Everything: move on to the next cell.
      //
      if (!Grid->PendingWrite && (Row + Grid->Width < RW_HEXGRID_PAGE)) {
        SetCursor (Grid, Row + Grid->Width);
      }
    }

    return TRUE;
  }

  if (Grid->Editing) {
    if ((Key->ScanCode == SCAN_ESC) || (Key->UnicodeChar == 0x1B)) {
      EndEdit (Grid, FALSE);
      return TRUE;
    }

    if ((Key->UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key->UnicodeChar == CHAR_LINEFEED)) {
      EndEdit (Grid, TRUE);
      return TRUE;
    }

    if (Key->UnicodeChar == CHAR_BACKSPACE) {
      if (Grid->EditDigits > 0) {
        Grid->EditDigits--;
        Grid->EditValue = RShiftU64 (Grid->EditValue, 4);
      }

      if (Grid->EditDigits == 0) {
        EndEdit (Grid, FALSE);
      }

      lv_obj_invalidate (Grid->Base.Obj);
      return TRUE;
    }
  }

  //
  // View commands.
  //
  if (RwKeyIsCtrl (Key, L'B')) {
    RwHexGridSetWidth (Grid, 1);
    return TRUE;
  }

  if (RwKeyIsCtrl (Key, L'W')) {
    RwHexGridSetWidth (Grid, 2);
    return TRUE;
  }

  if (RwKeyIsCtrl (Key, L'D')) {
    RwHexGridSetWidth (Grid, 4);
    return TRUE;
  }

  if (RwKeyIsCtrl (Key, L'G')) {
    EndEdit (Grid, FALSE);
    RwDialogInputHex ("Go To Address", "Address (hex):", Grid->Page + Grid->Cursor, MIN (Grid->AddrChars, 16), GoToDone, Grid);
    return TRUE;
  }

  if (RwKeyIsCtrl (Key, L'R')) {
    RwHexGridSetAutoRefresh (Grid, (BOOLEAN)!Grid->AutoRefresh, 0);
    RwStatusSetText (Grid->AutoRefresh ? "Auto refresh on" : "Auto refresh off");
    return TRUE;
  }

  //
  // Movement.
  //
  switch (Key->ScanCode) {
    case SCAN_LEFT:
      if (Grid->Cursor >= Grid->Width) {
        SetCursor (Grid, Grid->Cursor - Grid->Width);
      }

      return TRUE;
    case SCAN_RIGHT:
      if (Grid->Cursor + Grid->Width < RW_HEXGRID_PAGE) {
        SetCursor (Grid, Grid->Cursor + Grid->Width);
      }

      return TRUE;
    case SCAN_UP:
      if (Grid->Cursor >= GRID_BYTES_ROW) {
        SetCursor (Grid, Grid->Cursor - GRID_BYTES_ROW);
      } else if (ChangePage (Grid, FALSE)) {
        SetCursor (Grid, Grid->Cursor + RW_HEXGRID_PAGE - GRID_BYTES_ROW);
      }

      return TRUE;
    case SCAN_DOWN:
      if (Grid->Cursor + GRID_BYTES_ROW < RW_HEXGRID_PAGE) {
        SetCursor (Grid, Grid->Cursor + GRID_BYTES_ROW);
      } else if (ChangePage (Grid, TRUE)) {
        SetCursor (Grid, Grid->Cursor % GRID_BYTES_ROW);
      }

      return TRUE;
    case SCAN_HOME:
      SetCursor (Grid, Ctrl ? 0 : (Grid->Cursor & ~(UINTN)(GRID_BYTES_ROW - 1)));
      return TRUE;
    case SCAN_END:
      SetCursor (Grid, Ctrl ? RW_HEXGRID_PAGE - Grid->Width : ((Grid->Cursor | (GRID_BYTES_ROW - 1)) & ~(Grid->Width - 1)));
      return TRUE;
    case SCAN_PAGE_UP:
      ChangePage (Grid, FALSE);
      return TRUE;
    case SCAN_PAGE_DOWN:
      ChangePage (Grid, TRUE);
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
VOID
OnFocus (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  RW_HEXGRID  *Grid;

  Grid = (RW_HEXGRID *)Widget;
  if (!Focused) {
    EndEdit (Grid, FALSE);
  }

  lv_obj_set_style_border_color (Grid->Base.Obj, lv_color_hex (Focused ? RW_COLOR_HILITE_EDGE : RW_COLOR_SHADOW), 0);
  lv_obj_invalidate (Grid->Base.Obj);
}

STATIC
VOID
OnDelete (
  lv_event_t  *Event
  )
{
  RW_HEXGRID  *Grid;

  Grid = (RW_HEXGRID *)lv_event_get_user_data (Event);
  if (Grid->PendingWrite && RwDialogIsOpen ()) {
    Grid->PendingWrite = FALSE;
    RwDialogCancel ();
  }

  if (Grid->Timer != NULL) {
    lv_timer_delete (Grid->Timer);
  }

  RwFocusRemove (&Grid->Base);
  FreePool (Grid);
}

RW_HEXGRID *
RwHexGridCreate (
  IN lv_obj_t        *Parent,
  IN RW_MDI_WINDOW   *Window,
  IN RW_DATA_SOURCE  *Source
  )
{
  RW_HEXGRID  *Grid;
  lv_obj_t    *Obj;

  Grid = AllocateZeroPool (sizeof (RW_HEXGRID));
  if (Grid == NULL) {
    return NULL;
  }

  Grid->Width             = 1;
  Grid->Base.Kind         = "HexGrid";
  Grid->Base.HandleKey    = OnKey;
  Grid->Base.FocusChanged = OnFocus;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_WINDOW), 0);
  lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_SHADOW), 0);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_set_event_bubble (Obj, TRUE);
  lv_obj_add_event_cb (Obj, OnDraw, LV_EVENT_DRAW_MAIN, Grid);
  lv_obj_add_event_cb (Obj, OnPressed, LV_EVENT_PRESSED, Grid);
  lv_obj_add_event_cb (Obj, OnDelete, LV_EVENT_DELETE, Grid);
  Grid->Base.Obj = Obj;

  Grid->Timer = lv_timer_create (OnTimer, DEFAULT_PERIOD, Grid);
  if (Grid->Timer != NULL) {
    lv_timer_pause (Grid->Timer);
  }

  RwFocusAdd (Window, &Grid->Base);
  RwHexGridSetSource (Grid, Source);
  return Grid;
}

RW_WIDGET *
RwHexGridWidget (
  IN RW_HEXGRID  *Grid
  )
{
  return &Grid->Base;
}

VOID
RwHexGridSetSource (
  IN RW_HEXGRID      *Grid,
  IN RW_DATA_SOURCE  *Source
  )
{
  EndEdit (Grid, FALSE);
  Grid->Source = Source;
  Grid->Cursor = 0;
  Grid->Page   = (Source != NULL) ? (Source->Base & ~(UINT64)(RW_HEXGRID_PAGE - 1)) : 0;
  UpdateSize (Grid);
  RwHexGridSetAutoRefresh (Grid, (BOOLEAN)((Source != NULL) && !Source->SideEffects), 0);
  Reload (Grid);
}

VOID
RwHexGridSetCallbacks (
  IN RW_HEXGRID            *Grid,
  IN RW_HEXGRID_CURSOR_FN  OnCursor  OPTIONAL,
  IN RW_HEXGRID_WRITE_FN   OnWrite   OPTIONAL,
  IN VOID                  *Context
  )
{
  Grid->OnCursor = OnCursor;
  Grid->OnWrite  = OnWrite;
  Grid->Context  = Context;
  NotifyCursor (Grid);
}

VOID
RwHexGridSetWidth (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Width
  )
{
  if ((Width != 1) && (Width != 2) && (Width != 4)) {
    return;
  }

  EndEdit (Grid, FALSE);
  Grid->Width  = Width;
  Grid->Cursor = Grid->Cursor & ~(Width - 1);
  Reload (Grid);
}

UINTN
RwHexGridGetWidth (
  IN RW_HEXGRID  *Grid
  )
{
  return Grid->Width;
}

UINT64
RwHexGridGetPage (
  IN RW_HEXGRID  *Grid
  )
{
  return Grid->Page;
}

UINT64
RwHexGridGetCursor (
  IN RW_HEXGRID  *Grid
  )
{
  return Grid->Page + Grid->Cursor;
}

VOID
RwHexGridSetAutoRefresh (
  IN RW_HEXGRID  *Grid,
  IN BOOLEAN     On,
  IN UINT32      PeriodMs
  )
{
  Grid->AutoRefresh = On;
  if (Grid->Timer == NULL) {
    return;
  }

  if (PeriodMs != 0) {
    lv_timer_set_period (Grid->Timer, PeriodMs);
  }

  if (On) {
    lv_timer_resume (Grid->Timer);
  } else {
    lv_timer_pause (Grid->Timer);
  }
}

BOOLEAN
RwHexGridGetAutoRefresh (
  IN RW_HEXGRID  *Grid
  )
{
  return Grid->AutoRefresh;
}

VOID
RwHexGridGetCellText (
  IN  RW_HEXGRID  *Grid,
  IN  UINTN       Offset,
  OUT CHAR8       *Buffer,
  IN  UINTN       BufferSize,
  OUT BOOLEAN     *Changed  OPTIONAL
  )
{
  CHAR8    Text[24];
  BOOLEAN  Ch;

  FormatCell (Grid, Offset & ~(Grid->Width - 1), Text, &Ch);
  AsciiStrCpyS (Buffer, BufferSize, Text);
  if (Changed != NULL) {
    *Changed = Ch;
  }
}

BOOLEAN
RwHexGridIsEditing (
  IN RW_HEXGRID  *Grid
  )
{
  return Grid->Editing;
}
