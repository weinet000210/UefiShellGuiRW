/** @file
  Bit field view: the bits of one value as boxes, highest bit first, in
  rows of 16, with the bit numbers above and the value below.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

#define BIT_BOX_W     22
#define BIT_BOX_H     20
#define BIT_GAP       2
#define BIT_HALF_GAP  8
#define BIT_PAD       4
#define BITS_ROW      16

struct _RW_BITFIELD {
  RW_WIDGET                Base;
  UINT64                   Value;
  UINTN                    Bits;
  BOOLEAN                  Valid;
  UINTN                    Cursor;     ///< bit number
  RW_BITFIELD_CHANGE_FN    OnChange;
  VOID                     *Context;
  CONST lv_font_t          *Font;
  INT32                    LabelH;
};

STATIC
UINTN
RowsOf (
  IN RW_BITFIELD  *View
  )
{
  return (View->Bits + BITS_ROW - 1) / BITS_ROW;
}

STATIC
UINTN
PerRow (
  IN RW_BITFIELD  *View
  )
{
  return MIN (View->Bits, BITS_ROW);
}

/**
  Box of bit Bit relative to the object.
**/
STATIC
VOID
BitBox (
  IN  RW_BITFIELD  *View,
  IN  UINTN        Bit,
  OUT lv_area_t    *Box
  )
{
  UINTN  FromTop;
  UINTN  Row;
  UINTN  Column;
  INT32  X;
  INT32  Y;

  FromTop = View->Bits - 1 - Bit;
  Row     = FromTop / PerRow (View);
  Column  = FromTop % PerRow (View);
  X       = BIT_PAD + (INT32)Column * (BIT_BOX_W + BIT_GAP) + ((Column >= 8) ? BIT_HALF_GAP : 0);
  Y       = BIT_PAD + (INT32)Row * (View->LabelH + BIT_BOX_H + 4) + View->LabelH;
  Box->x1 = X;
  Box->y1 = Y;
  Box->x2 = X + BIT_BOX_W - 1;
  Box->y2 = Y + BIT_BOX_H - 1;
}

STATIC
VOID
UpdateSize (
  IN RW_BITFIELD  *View
  )
{
  UINTN  Columns;

  Columns = PerRow (View);
  lv_obj_set_size (
    View->Base.Obj,
    BIT_PAD * 2 + (INT32)Columns * (BIT_BOX_W + BIT_GAP) + ((Columns > 8) ? BIT_HALF_GAP : 0) + 2,
    BIT_PAD * 2 + (INT32)RowsOf (View) * (View->LabelH + BIT_BOX_H + 4) + View->LabelH + 6
    );
}

STATIC
VOID
OnDraw (
  lv_event_t  *Event
  )
{
  RW_BITFIELD         *View;
  lv_layer_t          *Layer;
  lv_area_t           Obj = { 0 };
  lv_area_t           Box;
  lv_area_t           Label;
  lv_draw_rect_dsc_t  Rect;
  UINTN               Bit;
  BOOLEAN             Set;
  BOOLEAN             IsCursor;
  CHAR8               Text[48];
  lv_color_t          Color;

  View  = (RW_BITFIELD *)lv_event_get_user_data (Event);
  Layer = lv_event_get_layer (Event);
  lv_obj_get_coords (View->Base.Obj, &Obj);
  for (Bit = 0; Bit < View->Bits; Bit++) {
    BitBox (View, Bit, &Box);
    lv_area_move (&Box, Obj.x1 + 1, Obj.y1 + 1);
    Set      = (BOOLEAN)(View->Valid && ((RShiftU64 (View->Value, Bit) & 1) != 0));
    IsCursor = (BOOLEAN)(Bit == View->Cursor);
    lv_draw_rect_dsc_init (&Rect);
    Rect.bg_opa       = LV_OPA_COVER;
    Rect.bg_color     = Set ? lv_color_hex (RW_COLOR_HILITE_SOFT) : lv_color_white ();
    Rect.border_width = IsCursor ? 2 : 1;
    Rect.border_color = lv_color_hex (IsCursor ? (View->Base.Focused ? RW_COLOR_HILITE : RW_COLOR_DARK) : RW_COLOR_SHADOW);
    lv_draw_rect (Layer, &Rect, &Box);
    Text[0] = View->Valid ? (Set ? '1' : '0') : ' ';
    Text[1] = '\0';
    Label    = Box;
    Label.x1 = Box.x1 + (BIT_BOX_W - lv_font_get_glyph_width (View->Font, '0', 0)) / 2;
    RwDrawText (Layer, View->Font, &Label, Text, Set ? lv_color_hex (RW_COLOR_STRONG) : lv_color_hex (RW_COLOR_TEXT_GRAY));

    AsciiSPrint (Text, sizeof (Text), "%d", Bit);
    Label.y2 = Box.y1 - 1;
    Label.y1 = Label.y2 - View->LabelH + 1;
    Label.x1 = Box.x1 + (BIT_BOX_W - (INT32)AsciiStrLen (Text) * lv_font_get_glyph_width (View->Font, '0', 0)) / 2;
    Label.x2 = Box.x2 + 4;
    RwDrawText (Layer, View->Font, &Label, Text, lv_color_hex (RW_COLOR_TEXT_GRAY));
  }

  //
  // Value line: hex, and the cursor bit.
  //
  BitBox (View, 0, &Box);
  lv_area_move (&Box, Obj.x1 + 1, Obj.y1 + 1);
  Label.x1 = Obj.x1 + 1 + BIT_PAD;
  Label.x2 = Obj.x2 - 1;
  Label.y1 = Box.y2 + 4;
  Label.y2 = Label.y1 + View->LabelH + 2;
  if (View->Valid) {
    AsciiSPrint (Text, sizeof (Text), "%0*lXh   bit %d = %d", View->Bits / 4, View->Value, View->Cursor, (UINTN)(RShiftU64 (View->Value, View->Cursor) & 1));
    Color = lv_color_black ();
  } else {
    AsciiSPrint (Text, sizeof (Text), "(no value)");
    Color = lv_color_hex (RW_COLOR_TEXT_GRAY);
  }

  RwDrawText (Layer, View->Font, &Label, Text, Color);
}

STATIC
VOID
Toggle (
  IN RW_BITFIELD  *View,
  IN UINTN        Bit
  )
{
  UINT64  NewValue;

  if (!View->Valid || (View->OnChange == NULL) || (Bit >= View->Bits)) {
    return;
  }

  NewValue = View->Value ^ LShiftU64 (1, Bit);
  View->OnChange (View, NewValue, View->Context);
}

STATIC
BOOLEAN
Hit (
  IN CONST lv_area_t   *Box,
  IN CONST lv_point_t  *Point,
  IN INT32             Unused
  )
{
  return (BOOLEAN)((Point->x >= Box->x1) && (Point->x <= Box->x2) && (Point->y >= Box->y1) && (Point->y <= Box->y2));
}

STATIC
VOID
OnPressed (
  lv_event_t  *Event
  )
{
  RW_BITFIELD  *View;
  lv_point_t   Point;
  lv_area_t    Obj = { 0 };
  lv_area_t    Box;
  UINTN        Bit;

  View = (RW_BITFIELD *)lv_event_get_user_data (Event);
  if (lv_indev_active () == NULL) {
    return;
  }

  lv_indev_get_point (lv_indev_active (), &Point);
  lv_obj_get_coords (View->Base.Obj, &Obj);
  for (Bit = 0; Bit < View->Bits; Bit++) {
    BitBox (View, Bit, &Box);
    lv_area_move (&Box, Obj.x1 + 1, Obj.y1 + 1);
    if (Hit (&Box, &Point, 0)) {
      View->Cursor = Bit;
      lv_obj_invalidate (View->Base.Obj);
      Toggle (View, Bit);
      return;
    }
  }
}

STATIC
BOOLEAN
OnKey (
  IN RW_WIDGET                 *Widget,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_BITFIELD  *View;

  View = (RW_BITFIELD *)Widget;
  if ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0) {
    return FALSE;
  }

  switch (Key->ScanCode) {
    case SCAN_LEFT:
      if (View->Cursor + 1 < View->Bits) {
        View->Cursor++;
      }

      break;
    case SCAN_RIGHT:
      if (View->Cursor > 0) {
        View->Cursor--;
      }

      break;
    case SCAN_UP:
      if (View->Cursor + BITS_ROW < View->Bits) {
        View->Cursor += BITS_ROW;
      }

      break;
    case SCAN_DOWN:
      if (View->Cursor >= BITS_ROW) {
        View->Cursor -= BITS_ROW;
      }

      break;
    case SCAN_HOME:
      View->Cursor = View->Bits - 1;
      break;
    case SCAN_END:
      View->Cursor = 0;
      break;
    default:
      if ((Key->UnicodeChar == L' ') || (Key->UnicodeChar == CHAR_CARRIAGE_RETURN)) {
        Toggle (View, View->Cursor);
        break;
      }

      return FALSE;
  }

  lv_obj_invalidate (View->Base.Obj);
  return TRUE;
}

STATIC
VOID
OnFocus (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  lv_obj_set_style_border_color (Widget->Obj, lv_color_hex (Focused ? RW_COLOR_HILITE_EDGE : RW_COLOR_SHADOW), 0);
  lv_obj_invalidate (Widget->Obj);
}

STATIC
VOID
OnDelete (
  lv_event_t  *Event
  )
{
  RW_BITFIELD  *View;

  View = (RW_BITFIELD *)lv_event_get_user_data (Event);
  RwFocusRemove (&View->Base);
  FreePool (View);
}

RW_BITFIELD *
RwBitFieldCreate (
  IN lv_obj_t       *Parent,
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_BITFIELD  *View;
  lv_obj_t     *Obj;

  View = AllocateZeroPool (sizeof (RW_BITFIELD));
  if (View == NULL) {
    return NULL;
  }

  View->Bits              = 32;
  View->Font              = &gRwFontMono12;
  View->LabelH            = lv_font_get_line_height (View->Font);
  View->Base.Kind         = "BitField";
  View->Base.HandleKey    = OnKey;
  View->Base.FocusChanged = OnFocus;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_WINDOW), 0);
  lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_SHADOW), 0);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_set_event_bubble (Obj, TRUE);
  lv_obj_add_event_cb (Obj, OnDraw, LV_EVENT_DRAW_MAIN, View);
  lv_obj_add_event_cb (Obj, OnPressed, LV_EVENT_PRESSED, View);
  lv_obj_add_event_cb (Obj, OnDelete, LV_EVENT_DELETE, View);
  View->Base.Obj = Obj;
  UpdateSize (View);
  RwFocusAdd (Window, &View->Base);
  return View;
}

RW_WIDGET *
RwBitFieldWidget (
  IN RW_BITFIELD  *View
  )
{
  return &View->Base;
}

VOID
RwBitFieldSetValue (
  IN RW_BITFIELD  *View,
  IN UINT64       Value,
  IN UINTN        Bits,
  IN BOOLEAN      Valid
  )
{
  if ((Bits != 8) && (Bits != 16) && (Bits != 32) && (Bits != 64)) {
    Bits = 32;
  }

  View->Value = (Bits == 64) ? Value : (Value & (LShiftU64 (1, Bits) - 1));
  View->Valid = Valid;
  if (View->Bits != Bits) {
    View->Bits = Bits;
    UpdateSize (View);
  }

  if (View->Cursor >= Bits) {
    View->Cursor = 0;
  }

  lv_obj_invalidate (View->Base.Obj);
}

UINT64
RwBitFieldGetValue (
  IN RW_BITFIELD  *View
  )
{
  return View->Value;
}

VOID
RwBitFieldSetCallback (
  IN RW_BITFIELD            *View,
  IN RW_BITFIELD_CHANGE_FN  OnChange  OPTIONAL,
  IN VOID                   *Context
  )
{
  View->OnChange = OnChange;
  View->Context  = Context;
}

UINTN
RwBitFieldGetCursor (
  IN RW_BITFIELD  *View
  )
{
  return View->Cursor;
}
