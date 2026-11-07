/** @file
  Shared view pieces: layout helpers and the space pane (heading, hex grid,
  bit field view) every raw-space window is built around.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

lv_obj_t *
RwViewColumn (
  IN lv_obj_t  *Parent,
  IN INT32     Width
  )
{
  lv_obj_t  *Obj;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_height (Obj, LV_PCT (100));
  if (Width > 0) {
    lv_obj_set_width (Obj, Width);
  } else {
    lv_obj_set_flex_grow (Obj, 1);
  }

  lv_obj_set_flex_flow (Obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row (Obj, 4, 0);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Obj;
}

lv_obj_t *
RwViewPane (
  IN lv_obj_t  *Parent,
  IN INT32     Grow
  )
{
  lv_obj_t  *Obj;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_width (Obj, LV_PCT (100));
  if (Grow > 0) {
    lv_obj_set_flex_grow (Obj, (uint8_t)Grow);
  } else {
    lv_obj_set_height (Obj, LV_SIZE_CONTENT);
  }

  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Obj;
}

lv_obj_t *
RwViewLabel (
  IN lv_obj_t     *Parent,
  IN CONST CHAR8  *Text
  )
{
  lv_obj_t  *Obj;

  Obj = lv_label_create (Parent);
  lv_label_set_text (Obj, Text);
  lv_label_set_long_mode (Obj, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_width (Obj, LV_PCT (100));
  lv_obj_set_style_text_color (Obj, lv_color_hex (RW_COLOR_STRONG), 0);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Obj;
}

//
// ---- Space pane ------------------------------------------------------------
//

STATIC
VOID
OnCursor (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address,
  IN UINTN       Width,
  IN UINT64      Value,
  IN BOOLEAN     Valid,
  IN VOID        *Context
  )
{
  RW_SPACE_PANE  *Pane;
  CHAR8          Text[96];

  Pane = Context;
  if (Pane->Bits != NULL) {
    RwBitFieldSetValue (Pane->Bits, Value, Width * 8, Valid);
  }

  if (Valid) {
    AsciiSPrint (Text, sizeof (Text), "%0*lXh = %0*lXh", Pane->Source->AddrDigits, Address, Width * 2, Value);
  } else {
    AsciiSPrint (Text, sizeof (Text), "%0*lXh: cannot be read", Pane->Source->AddrDigits, Address);
  }

  RwStatusSetText (Text);
}

STATIC
VOID
OnWrite (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address,
  IN UINTN       Width,
  IN UINT64      Value,
  IN EFI_STATUS  Status,
  IN VOID        *Context
  )
{
  RW_SPACE_PANE  *Pane;
  CHAR8          Text[96];

  Pane = Context;
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "Write to %0*lXh failed: %r", Pane->Source->AddrDigits, Address, Status);
  } else {
    AsciiSPrint (Text, sizeof (Text), "Wrote %0*lXh to %0*lXh", Width * 2, Value, Pane->Source->AddrDigits, Address);
  }

  RwStatusSetText (Text);
}

STATIC
VOID
OnBits (
  IN RW_BITFIELD  *View,
  IN UINT64       NewValue,
  IN VOID         *Context
  )
{
  RW_SPACE_PANE  *Pane;

  Pane = Context;
  RwHexGridWrite (Pane->Grid, RwHexGridGetCursor (Pane->Grid), RwHexGridGetWidth (Pane->Grid), NewValue);
}

STATIC
VOID
SetHeading (
  IN RW_SPACE_PANE  *Pane
  )
{
  CHAR8  Text[RW_DS_NAME_MAX + 32];

  AsciiSPrint (
    Text,
    sizeof (Text),
    "%a   (%0*lX-%0*lX)",
    Pane->Source->Name,
    Pane->Source->AddrDigits,
    Pane->Source->Base,
    Pane->Source->AddrDigits,
    Pane->Source->Limit
    );
  lv_label_set_text (Pane->Heading, Text);
}

VOID
RwSpacePaneCreate (
  OUT RW_SPACE_PANE   *Pane,
  IN  lv_obj_t        *Parent,
  IN  RW_MDI_WINDOW   *Window,
  IN  RW_DATA_SOURCE  *Source
  )
{
  Pane->Window  = Window;
  Pane->Source  = Source;
  Pane->Column  = RwViewColumn (Parent, 0);
  Pane->Heading = RwViewLabel (Pane->Column, "");
  Pane->Grid    = RwHexGridCreate (Pane->Column, Window, Source);
  Pane->Bits    = RwBitFieldCreate (Pane->Column, Window);
  RwHexGridSetCallbacks (Pane->Grid, OnCursor, OnWrite, Pane);
  RwBitFieldSetCallback (Pane->Bits, (Source->Write != NULL) ? OnBits : NULL, Pane);
  SetHeading (Pane);
  RwHexGridGoTo (Pane->Grid, Source->Base);
}

VOID
RwSpacePaneSetSource (
  IN RW_SPACE_PANE   *Pane,
  IN RW_DATA_SOURCE  *Source
  )
{
  Pane->Source = Source;
  RwHexGridSetSource (Pane->Grid, Source);
  RwBitFieldSetCallback (Pane->Bits, (Source->Write != NULL) ? OnBits : NULL, Pane);
  SetHeading (Pane);
  RwHexGridGoTo (Pane->Grid, Source->Base);
}

VOID
RwSpacePaneSize (
  IN  RW_SPACE_PANE  *Pane,
  OUT INT32          *Width,
  OUT INT32          *Height
  )
{
  lv_obj_t  *Grid;
  lv_obj_t  *Bits;

  Grid = RwHexGridWidget (Pane->Grid)->Obj;
  Bits = RwBitFieldWidget (Pane->Bits)->Obj;
  lv_obj_update_layout (Pane->Heading);
  *Width  = MAX (lv_obj_get_width (Grid), lv_obj_get_width (Bits));
  *Height = lv_obj_get_height (Pane->Heading) + lv_obj_get_height (Grid) + lv_obj_get_height (Bits) + 8;
}
