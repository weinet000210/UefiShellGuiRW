/** @file
  Modal dialogs: message, Yes / No question, hex number entry.

  A dialog sits on the top layer over a transparent full-screen blocker, so
  clicks outside it do nothing; while it is open RwKeyHandler gives it every
  key.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

#define DLG_MAX_BUTTONS  2
#define DLG_TEXT_WIDTH   380

typedef enum {
  DlgMessage,
  DlgConfirm,
  DlgInput
} DLG_KIND;

typedef struct {
  DLG_KIND             Kind;
  lv_obj_t             *Blocker;
  lv_obj_t             *Box;
  lv_obj_t             *Message;
  lv_obj_t             *Field;          ///< input: the label with the digits
  lv_obj_t             *Buttons[DLG_MAX_BUTTONS];
  UINTN                ButtonCount;
  UINTN                Selected;        ///< focused button
  UINTN                CancelIndex;     ///< button Esc chooses
  RW_DIALOG_DONE_FN    Done;
  VOID                 *Context;
  CHAR8                Digits[17];
  UINTN                DigitCount;
  UINTN                MaxDigits;
  BOOLEAN              Fresh;           ///< the first digit typed replaces the initial value
} DLG_STATE;

STATIC DLG_STATE  *mDlg;

STATIC
VOID
StyleButton (
  IN lv_obj_t  *Button,
  IN BOOLEAN   Selected
  )
{
  lv_obj_set_style_border_width (Button, Selected ? 2 : 1, 0);
  lv_obj_set_style_border_color (Button, lv_color_hex (Selected ? RW_COLOR_HILITE : RW_COLOR_DARK), 0);
}

STATIC
VOID
SelectButton (
  IN UINTN  Index
  )
{
  UINTN  I;

  if ((mDlg == NULL) || (Index >= mDlg->ButtonCount)) {
    return;
  }

  mDlg->Selected = Index;
  for (I = 0; I < mDlg->ButtonCount; I++) {
    StyleButton (mDlg->Buttons[I], (BOOLEAN)(I == Index));
  }
}

STATIC
UINT64
DigitsValue (
  VOID
  )
{
  UINT64  Value;
  UINTN   I;
  CHAR8   Ch;

  Value = 0;
  for (I = 0; I < mDlg->DigitCount; I++) {
    Ch    = mDlg->Digits[I];
    Value = LShiftU64 (Value, 4) | (UINT64)((Ch <= '9') ? (Ch - '0') : (Ch - 'A' + 10));
  }

  return Value;
}

/**
  Close the dialog; button 0 is the accepting one (OK / Yes).
**/
STATIC
VOID
Finish (
  IN BOOLEAN  Accepted
  )
{
  DLG_STATE          *Dlg;
  RW_DIALOG_DONE_FN  Done;
  VOID               *Context;
  UINT64             Value;

  Dlg = mDlg;
  if (Dlg == NULL) {
    return;
  }

  mDlg    = NULL;
  Done    = Dlg->Done;
  Context = Dlg->Context;
  Value   = 0;
  if (Dlg->Kind == DlgInput) {
    mDlg  = Dlg;     // DigitsValue reads mDlg
    Value = DigitsValue ();
    mDlg  = NULL;
    if (Dlg->DigitCount == 0) {
      Accepted = FALSE;   // empty entry: nothing to go to
    }
  }

  lv_obj_set_hidden (Dlg->Blocker, TRUE);
  lv_obj_delete_async (Dlg->Blocker);
  FreePool (Dlg);
  if (Done != NULL) {
    Done (Accepted, Value, Context);
  }
}

STATIC
VOID
OnButtonClicked (
  lv_event_t  *Event
  )
{
  UINTN  Index;

  if (mDlg == NULL) {
    return;
  }

  Index = (UINTN)lv_event_get_user_data (Event);
  Finish ((BOOLEAN)(Index == 0));
}

STATIC
VOID
UpdateField (
  VOID
  )
{
  CHAR8  Text[24];

  //
  // The initial value shows selected: typing replaces it.
  //
  AsciiSPrint (Text, sizeof (Text), mDlg->Fresh ? "%a" : "%a_", mDlg->Digits);
  lv_label_set_text (mDlg->Field, Text);
  lv_obj_set_style_bg_color (mDlg->Field, lv_color_hex (RW_COLOR_HILITE), 0);
  lv_obj_set_style_bg_opa (mDlg->Field, mDlg->Fresh ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color (mDlg->Field, mDlg->Fresh ? lv_color_white () : lv_color_black (), 0);
}

STATIC
lv_obj_t *
AddButton (
  IN lv_obj_t     *Row,
  IN CONST CHAR8  *Text
  )
{
  lv_obj_t  *Button;
  lv_obj_t  *Label;
  UINTN     Index;

  Index  = mDlg->ButtonCount;
  Button = lv_button_create (Row);
  lv_obj_remove_style_all (Button);
  lv_obj_set_size (Button, 84, 26);
  lv_obj_set_style_bg_color (Button, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_color (Button, lv_color_hex (RW_COLOR_HILITE_SOFT), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa (Button, LV_OPA_COVER, 0);
  lv_obj_set_style_radius (Button, 2, 0);
  StyleButton (Button, FALSE);
  lv_obj_add_event_cb (Button, OnButtonClicked, LV_EVENT_CLICKED, (VOID *)Index);
  Label = lv_label_create (Button);
  lv_label_set_text (Label, Text);
  lv_obj_set_style_text_color (Label, lv_color_black (), 0);
  lv_obj_center (Label);
  mDlg->Buttons[mDlg->ButtonCount++] = Button;
  return Button;
}

/**
  Build the frame: blocker, box with caption, icon + message row, buttons.
**/
STATIC
VOID
Open (
  IN DLG_KIND           Kind,
  IN CONST CHAR8        *Title,
  IN CONST CHAR8        *Text,
  IN UINT32             Flags,
  IN RW_DIALOG_DONE_FN  Done,
  IN VOID               *Context
  )
{
  lv_obj_t  *Obj;
  lv_obj_t  *Caption;
  lv_obj_t  *Body;
  lv_obj_t  *Row;
  lv_obj_t  *Icon;

  if (mDlg != NULL) {
    RwDialogCancel ();
  }

  mDlg = AllocateZeroPool (sizeof (DLG_STATE));
  if (mDlg == NULL) {
    if (Done != NULL) {
      Done (FALSE, 0, Context);
    }

    return;
  }

  mDlg->Kind    = Kind;
  mDlg->Done    = Done;
  mDlg->Context = Context;
  RwMenuClose ();

  Obj = lv_obj_create (lv_layer_top ());
  lv_obj_remove_style_all (Obj);
  lv_obj_set_size (Obj, LV_PCT (100), LV_PCT (100));
  lv_obj_set_clickable (Obj, TRUE);
  lv_obj_set_scrollable (Obj, FALSE);
  mDlg->Blocker = Obj;

  //
  // Box: raised frame like a child window, active caption.
  //
  Obj = lv_obj_create (mDlg->Blocker);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_size (Obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_DARK), 0);
  lv_obj_set_style_shadow_width (Obj, 10, 0);
  lv_obj_set_style_shadow_offset_x (Obj, 3, 0);
  lv_obj_set_style_shadow_offset_y (Obj, 3, 0);
  lv_obj_set_style_shadow_opa (Obj, LV_OPA_40, 0);
  lv_obj_set_style_pad_all (Obj, 3, 0);
  lv_obj_set_style_text_font (Obj, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color (Obj, lv_color_black (), 0);
  lv_obj_set_flex_flow (Obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_center (Obj);
  mDlg->Box = Obj;

  Caption = lv_obj_create (Obj);
  lv_obj_remove_style_all (Caption);
  lv_obj_set_size (Caption, LV_PCT (100), RW_CAPTION_HEIGHT);
  lv_obj_set_style_min_width (Caption, DLG_TEXT_WIDTH + 60, 0);
  lv_obj_set_style_bg_opa (Caption, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color (Caption, lv_color_hex (RW_COLOR_TITLE_A1), 0);
  lv_obj_set_style_bg_grad_color (Caption, lv_color_hex (RW_COLOR_TITLE_A2), 0);
  lv_obj_set_style_bg_grad_dir (Caption, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_pad_left (Caption, 5, 0);
  lv_obj_set_scrollable (Caption, FALSE);
  Row = lv_label_create (Caption);
  lv_label_set_text (Row, Title);
  lv_obj_set_style_text_color (Row, lv_color_white (), 0);
  lv_obj_align (Row, LV_ALIGN_LEFT_MID, 0, 0);

  Body = lv_obj_create (Obj);
  lv_obj_remove_style_all (Body);
  lv_obj_set_size (Body, LV_PCT (100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all (Body, 12, 0);
  lv_obj_set_style_pad_row (Body, 12, 0);
  lv_obj_set_flex_flow (Body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align (Body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Body, FALSE);

  Row = lv_obj_create (Body);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column (Row, 12, 0);
  lv_obj_set_scrollable (Row, FALSE);
  if ((Flags & RW_DIALOG_WARNING) != 0) {
    Icon = lv_label_create (Row);
    lv_label_set_text (Icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font (Icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color (Icon, lv_color_hex (0xE0A000), 0);
  }

  mDlg->Message = lv_label_create (Row);
  lv_label_set_text (mDlg->Message, Text);
  lv_label_set_long_mode (mDlg->Message, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width (mDlg->Message, ((Flags & RW_DIALOG_WARNING) != 0) ? DLG_TEXT_WIDTH - 40 : DLG_TEXT_WIDTH);

  if (Kind == DlgInput) {
    Obj = lv_obj_create (Body);
    lv_obj_remove_style_all (Obj);
    lv_obj_set_size (Obj, DLG_TEXT_WIDTH, 26);
    lv_obj_set_style_bg_color (Obj, lv_color_white (), 0);
    lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width (Obj, 1, 0);
    lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_DARK), 0);
    lv_obj_set_style_pad_left (Obj, 6, 0);
    lv_obj_set_scrollable (Obj, FALSE);
    mDlg->Field = lv_label_create (Obj);
    lv_obj_set_style_text_font (mDlg->Field, RwMonoFont (), 0);
    lv_obj_align (mDlg->Field, LV_ALIGN_LEFT_MID, 0, 0);
  }

  Row = lv_obj_create (Body);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Row, 10, 0);
  lv_obj_set_scrollable (Row, FALSE);
  switch (Kind) {
    case DlgMessage:
      AddButton (Row, "OK");
      mDlg->CancelIndex = 0;
      SelectButton (0);
      break;
    case DlgConfirm:
      AddButton (Row, "Yes");
      AddButton (Row, "No");
      mDlg->CancelIndex = 1;
      SelectButton (((Flags & RW_DIALOG_WARNING) != 0) ? 1 : 0);
      break;
    default:
      AddButton (Row, "OK");
      AddButton (Row, "Cancel");
      mDlg->CancelIndex = 1;
      SelectButton (0);
      break;
  }
}

VOID
RwDialogMessage (
  IN CONST CHAR8        *Title,
  IN CONST CHAR8        *Text,
  IN RW_DIALOG_DONE_FN  Done     OPTIONAL,
  IN VOID               *Context
  )
{
  Open (DlgMessage, Title, Text, 0, Done, Context);
}

VOID
RwDialogConfirm (
  IN CONST CHAR8        *Title,
  IN CONST CHAR8        *Text,
  IN UINT32             Flags,
  IN RW_DIALOG_DONE_FN  Done,
  IN VOID               *Context
  )
{
  Open (DlgConfirm, Title, Text, Flags, Done, Context);
}

VOID
RwDialogInputHex (
  IN CONST CHAR8        *Title,
  IN CONST CHAR8        *Prompt,
  IN UINT64             Initial,
  IN UINTN              MaxDigits,
  IN RW_DIALOG_DONE_FN  Done,
  IN VOID               *Context
  )
{
  Open (DlgInput, Title, Prompt, 0, Done, Context);
  if (mDlg == NULL) {
    return;
  }

  mDlg->MaxDigits = MIN (MAX (MaxDigits, 1), 16);
  AsciiSPrint (mDlg->Digits, sizeof (mDlg->Digits), "%lX", Initial);
  mDlg->DigitCount = AsciiStrLen (mDlg->Digits);
  if (mDlg->DigitCount > mDlg->MaxDigits) {
    mDlg->DigitCount = 0;
    mDlg->Digits[0]  = '\0';
  }

  mDlg->Fresh = TRUE;
  UpdateField ();
}

BOOLEAN
RwDialogIsOpen (
  VOID
  )
{
  return (BOOLEAN)(mDlg != NULL);
}

VOID
RwDialogCancel (
  VOID
  )
{
  Finish (FALSE);
}

CONST CHAR8 *
RwDialogGetText (
  VOID
  )
{
  if (mDlg == NULL) {
    return NULL;
  }

  if (mDlg->Kind == DlgInput) {
    return mDlg->Digits;
  }

  return lv_label_get_text (mDlg->Message);
}

BOOLEAN
RwDialogHandleKey (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  INTN  Digit;

  if (mDlg == NULL) {
    return FALSE;
  }

  if ((Key->ScanCode == SCAN_ESC) || (Key->UnicodeChar == 0x1B)) {
    Finish ((BOOLEAN)(mDlg->CancelIndex == 0));
    return TRUE;
  }

  if ((Key->UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key->UnicodeChar == CHAR_LINEFEED)) {
    Finish ((BOOLEAN)(mDlg->Selected == 0));
    return TRUE;
  }

  if ((Key->ScanCode == SCAN_LEFT) || (Key->ScanCode == SCAN_RIGHT) || (Key->UnicodeChar == CHAR_TAB)) {
    SelectButton ((mDlg->Selected + 1) % mDlg->ButtonCount);
    return TRUE;
  }

  if (mDlg->Kind == DlgConfirm) {
    if (RwKeyIsChar (Key, L'y') || RwKeyIsChar (Key, L'Y')) {
      Finish (TRUE);
    } else if (RwKeyIsChar (Key, L'n') || RwKeyIsChar (Key, L'N')) {
      Finish (FALSE);
    }

    return TRUE;
  }

  if (mDlg->Kind == DlgInput) {
    Digit = RwKeyHexDigit (Key);
    if (mDlg->Fresh && ((Digit >= 0) || (Key->UnicodeChar == CHAR_BACKSPACE))) {
      mDlg->Fresh      = FALSE;
      mDlg->DigitCount = 0;
      mDlg->Digits[0]  = '\0';
      if (Digit < 0) {
        UpdateField ();
        return TRUE;
      }
    }

    if ((Digit >= 0) && (mDlg->DigitCount < mDlg->MaxDigits)) {
      mDlg->Digits[mDlg->DigitCount++] = "0123456789ABCDEF"[Digit];
      mDlg->Digits[mDlg->DigitCount]   = '\0';
      UpdateField ();
    } else if ((Key->UnicodeChar == CHAR_BACKSPACE) && (mDlg->DigitCount > 0)) {
      mDlg->Digits[--mDlg->DigitCount] = '\0';
      UpdateField ();
    }
  }

  return TRUE;
}
