/** @file
  Calculator window: a 64-bit expression (hex by default, 'n' suffix for
  decimal, 0y for binary; + - * / % & | ^ ~ << >> and parentheses) shown in
  hex, unsigned and signed decimal, octal, binary and ASCII, with its 64 bits
  as boxes that toggle on click or Space. Evaluates as you type.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"

#define CALC_TEXT_MAX  128

STATIC RW_CALC_VIEW  mCalc;

STATIC
VOID
Evaluate (
  VOID
  )
{
  CONST CHAR8  *Text;
  CHAR8        Error[80];
  CHAR8        Line[120];
  RW_INFO      Info;
  EFI_STATUS   Status;
  UINT64       Value;

  Text   = lv_textarea_get_text (mCalc.Area);
  Status = RwCalcEval (Text, &Value, Error, sizeof (Error));
  if (EFI_ERROR (Status)) {
    mCalc.Valid = FALSE;
    lv_label_set_text (mCalc.Message, Error);
    lv_obj_set_style_text_color (mCalc.Message, lv_color_hex (0xC00000), 0);
    return;
  }

  mCalc.Valid = TRUE;
  mCalc.Value = Value;
  AsciiSPrint (Line, sizeof (Line), "= %lXh = %lu", Value, Value);
  lv_label_set_text (mCalc.Message, Line);
  lv_obj_set_style_text_color (mCalc.Message, lv_color_hex (RW_COLOR_STRONG), 0);
  RwInfoInit (&Info);
  RwCalcDecode (Value, &Info);
  RwPropTableSetInfo (mCalc.Result, &Info);
  RwBitFieldSetValue (mCalc.Bits, Value, 64, TRUE);
}

VOID
RwCalcViewSetText (
  IN CONST CHAR8  *Expression
  )
{
  if (mCalc.Window == NULL) {
    return;
  }

  lv_textarea_set_text (mCalc.Area, Expression);
  lv_textarea_set_cursor_pos (mCalc.Area, LV_TEXTAREA_CURSOR_LAST);
  Evaluate ();
}

STATIC
VOID
OnBits (
  IN RW_BITFIELD  *View,
  IN UINT64       NewValue,
  IN VOID         *Context
  )
{
  CHAR8  Text[24];

  //
  // A toggled bit replaces the expression by the new value.
  //
  AsciiSPrint (Text, sizeof (Text), "%lX", NewValue);
  RwCalcViewSetText (Text);
}

STATIC
VOID
TextChanged (
  lv_event_t  *Event
  )
{
  Evaluate ();
}

STATIC
BOOLEAN
InputKey (
  IN RW_WIDGET                 *Widget,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  UINT32  Code;

  if ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0) {
    return FALSE;
  }

  if ((Key->UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key->UnicodeChar == CHAR_LINEFEED)) {
    //
    // Enter: replace the expression by its value, ready for the next step.
    //
    if (mCalc.Valid) {
      CHAR8  Text[24];

      AsciiSPrint (Text, sizeof (Text), "%lX", mCalc.Value);
      RwCalcViewSetText (Text);
    }

    return TRUE;
  }

  if ((Key->ScanCode == SCAN_HOME) || (Key->ScanCode == SCAN_END)) {
    lv_textarea_set_cursor_pos (mCalc.Area, (Key->ScanCode == SCAN_HOME) ? 0 : LV_TEXTAREA_CURSOR_LAST);
    return TRUE;
  }

  if ((Key->ScanCode == SCAN_UP) || (Key->ScanCode == SCAN_DOWN) || (Key->ScanCode == SCAN_PAGE_UP) || (Key->ScanCode == SCAN_PAGE_DOWN)) {
    return FALSE;
  }

  Code = UefiGuiKeyToLvgl (Key);
  if (Code == 0) {
    return FALSE;
  }

  lv_obj_send_event (mCalc.Area, LV_EVENT_KEY, &Code);
  return TRUE;
}

STATIC
VOID
InputFocus (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  lv_obj_set_state (Widget->Obj, LV_STATE_FOCUSED, Focused);
  lv_obj_set_style_border_color (Widget->Obj, lv_color_hex (Focused ? RW_COLOR_HILITE_EDGE : RW_COLOR_SHADOW), 0);
}

STATIC
VOID
InputDeleted (
  lv_event_t  *Event
  )
{
  RW_WIDGET  *Widget;

  Widget = (RW_WIDGET *)lv_event_get_user_data (Event);
  RwFocusRemove (Widget);
  FreePool (Widget);
  mCalc.Input = NULL;
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mCalc, sizeof (mCalc));
}

RW_MDI_WINDOW *
RwViewOpenCalc (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Column;
  lv_obj_t       *Pane;
  RW_WIDGET      *Widget;
  INT32          BitsW;

  if (mCalc.Window != NULL) {
    RwMdiActivate (mCalc.Window);
    return mCalc.Window;
  }

  Window = RwMdiCreate ("Calculator", RwCmdCalc, 700, 420);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mCalc, sizeof (mCalc));
  mCalc.Window    = Window;
  Window->OnClose = OnClose;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  Column = RwViewColumn (Window->Content, 0);
  RwViewLabel (Column, "Expression: hex by default, 31n = decimal, 0y101 = binary; + - * / % & | ^ ~ << >> ( )   Enter: keep the value");

  mCalc.Area = lv_textarea_create (Column);
  lv_textarea_set_one_line (mCalc.Area, TRUE);
  lv_textarea_set_max_length (mCalc.Area, CALC_TEXT_MAX);
  lv_obj_set_width (mCalc.Area, LV_PCT (100));
  lv_obj_set_style_text_font (mCalc.Area, RwMonoFont (), 0);
  lv_obj_set_style_radius (mCalc.Area, 0, 0);
  lv_obj_set_style_border_width (mCalc.Area, 1, 0);
  lv_obj_set_style_border_color (mCalc.Area, lv_color_hex (RW_COLOR_SHADOW), 0);
  lv_obj_set_style_pad_all (mCalc.Area, 4, 0);
  lv_textarea_set_text (mCalc.Area, "0");
  lv_obj_add_event_cb (mCalc.Area, TextChanged, LV_EVENT_VALUE_CHANGED, NULL);

  mCalc.Message = RwViewLabel (Column, "");
  Pane          = RwViewPane (Column, 0);
  lv_obj_set_height (Pane, 162);       // header + the six result rows
  mCalc.Result  = RwPropTableCreate (Pane, Window, FALSE);
  mCalc.Bits    = RwBitFieldCreate (Column, Window);
  RwBitFieldSetCallback (mCalc.Bits, OnBits, NULL);

  Widget = AllocateZeroPool (sizeof (RW_WIDGET));
  if (Widget != NULL) {
    Widget->Obj          = mCalc.Area;
    Widget->Kind         = "CalcInput";
    Widget->HandleKey    = InputKey;
    Widget->FocusChanged = InputFocus;
    mCalc.Input          = Widget;
    lv_obj_add_event_cb (mCalc.Area, InputDeleted, LV_EVENT_DELETE, Widget);
    RwFocusAdd (Window, Widget);
  }

  Evaluate ();
  lv_obj_update_layout (RwBitFieldWidget (mCalc.Bits)->Obj);
  BitsW = lv_obj_get_width (RwBitFieldWidget (mCalc.Bits)->Obj);
  RwMdiFitContent (Window, MAX (BitsW, 620), 400);
  if (Widget != NULL) {
    RwFocusSet (Widget);
  }

  return Window;
}

RW_CALC_VIEW *
RwCalcViewGet (
  VOID
  )
{
  return &mCalc;
}
