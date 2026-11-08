/** @file
  Focus chain inside a child window, key helpers and drawing helpers shared
  by the widgets.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwWidgets.h"

CONST lv_font_t *
RwMonoFont (
  VOID
  )
{
  return (lv_display_get_horizontal_resolution (NULL) < 1024) ? &gRwFontMono12 : &gRwFontMono14;
}

lv_color_t
RwEmphasisColor (
  IN UINT8  Emphasis
  )
{
  switch (Emphasis) {
    case RwEmStrong:
      return lv_color_hex (RW_COLOR_STRONG);
    case RwEmAlert:
      return lv_color_hex (RW_COLOR_CHANGED);
    case RwEmDim:
      return lv_color_hex (RW_COLOR_TEXT_GRAY);
    default:
      return lv_color_black ();
  }
}

//
// ---- Focus chain -------------------------------------------------------------
//

STATIC
VOID
OnWidgetPressed (
  lv_event_t  *Event
  )
{
  RwFocusSet ((RW_WIDGET *)lv_event_get_user_data (Event));
}

STATIC
VOID
SetFocused (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  if ((Widget == NULL) || (Widget->Focused == Focused)) {
    return;
  }

  Widget->Focused = Focused;
  if (Widget->FocusChanged != NULL) {
    Widget->FocusChanged (Widget, Focused);
  }
}

VOID
RwFocusAdd (
  IN RW_MDI_WINDOW  *Window,
  IN RW_WIDGET      *Widget
  )
{
  Widget->Window = Window;
  if ((Window == NULL) || (Window->FocusCount >= RW_MDI_MAX_FOCUS)) {
    return;
  }

  Window->Focus[Window->FocusCount++] = Widget;
  lv_obj_add_event_cb (Widget->Obj, OnWidgetPressed, LV_EVENT_PRESSED, Widget);
  if (Window->FocusCount == 1) {
    Window->FocusIndex = 0;
    SetFocused (Widget, TRUE);
  }
}

VOID
RwFocusRemove (
  IN RW_WIDGET  *Widget
  )
{
  RW_MDI_WINDOW  *Window;
  UINTN          Index;
  BOOLEAN        WasFocused;

  Window = Widget->Window;
  if (Window == NULL) {
    return;
  }

  WasFocused = Widget->Focused;
  for (Index = 0; Index < Window->FocusCount; Index++) {
    if (Window->Focus[Index] == Widget) {
      CopyMem (&Window->Focus[Index], &Window->Focus[Index + 1], (Window->FocusCount - Index - 1) * sizeof (RW_WIDGET *));
      Window->FocusCount--;
      if (Window->FocusIndex > Index) {
        Window->FocusIndex--;
      }

      break;
    }
  }

  if (Window->FocusIndex >= Window->FocusCount) {
    Window->FocusIndex = 0;
  }

  Widget->Window = NULL;
  if (WasFocused && (Window->FocusCount > 0)) {
    SetFocused (Window->Focus[Window->FocusIndex], TRUE);
  }
}

VOID
RwFocusSet (
  IN RW_WIDGET  *Widget
  )
{
  RW_MDI_WINDOW  *Window;
  UINTN          Index;

  if ((Widget == NULL) || (Widget->Window == NULL)) {
    return;
  }

  Window = Widget->Window;
  for (Index = 0; Index < Window->FocusCount; Index++) {
    if (Window->Focus[Index] == Widget) {
      if (Index != Window->FocusIndex) {
        SetFocused (Window->Focus[Window->FocusIndex], FALSE);
        Window->FocusIndex = Index;
      }

      SetFocused (Widget, TRUE);
      return;
    }
  }
}

RW_WIDGET *
RwFocusGet (
  IN RW_MDI_WINDOW  *Window
  )
{
  if ((Window == NULL) || (Window->FocusCount == 0)) {
    return NULL;
  }

  return Window->Focus[Window->FocusIndex];
}

STATIC
BOOLEAN
WidgetHidden (
  IN RW_WIDGET  *Widget
  )
{
  lv_obj_t  *Obj;

  for (Obj = Widget->Obj; Obj != NULL; Obj = lv_obj_get_parent (Obj)) {
    if (lv_obj_is_hidden (Obj)) {
      return TRUE;
    }
  }

  return FALSE;
}

BOOLEAN
RwFocusHandleKey (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_MDI_WINDOW  *Window;
  RW_WIDGET      *Widget;
  UINTN          Next;
  UINTN          Tries;

  Window = RwMdiGetActive ();
  if ((Window == NULL) || Window->Minimized || (Window->FocusCount == 0)) {
    return FALSE;
  }

  Widget = Window->Focus[Window->FocusIndex];
  if ((Widget->HandleKey != NULL) && Widget->HandleKey (Widget, Key)) {
    return TRUE;
  }

  if ((Key->UnicodeChar == CHAR_TAB) && ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) == 0)) {
    //
    // Skip widgets that are hidden (a view can swap panes in one place).
    //
    Next = Window->FocusIndex;
    for (Tries = 1; Tries < Window->FocusCount; Tries++) {
      if ((Key->Modifiers & UEFI_GUI_KEY_MOD_SHIFT) != 0) {
        Next = (Next + Window->FocusCount - 1) % Window->FocusCount;
      } else {
        Next = (Next + 1) % Window->FocusCount;
      }

      if (!WidgetHidden (Window->Focus[Next])) {
        RwFocusSet (Window->Focus[Next]);
        break;
      }
    }

    return TRUE;
  }

  return FALSE;
}

VOID
RwFocusWindowClosed (
  IN RW_MDI_WINDOW  *Window
  )
{
  UINTN  Index;

  for (Index = 0; Index < Window->FocusCount; Index++) {
    Window->Focus[Index]->Window  = NULL;
    Window->Focus[Index]->Focused = FALSE;
  }

  Window->FocusCount = 0;
  Window->FocusIndex = 0;
}

//
// ---- Keys ------------------------------------------------------------------
//

BOOLEAN
RwKeyIsChar (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN CHAR16                    Char
  )
{
  return (BOOLEAN)((Key->ScanCode == SCAN_NULL) && (Key->UnicodeChar == Char) &&
                   ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) == 0));
}

/**
  Ctrl+Letter arrives either as the letter with the Ctrl modifier or as the
  control character (1..26).
**/
BOOLEAN
RwKeyIsCtrl (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN CHAR16                    Letter
  )
{
  CHAR16  Upper;

  if ((Key->ScanCode != SCAN_NULL) || ((Key->Modifiers & UEFI_GUI_KEY_MOD_ALT) != 0)) {
    return FALSE;
  }

  Upper = (Letter >= L'a' && Letter <= L'z') ? (CHAR16)(Letter - 0x20) : Letter;
  if (Key->UnicodeChar == (CHAR16)(Upper - L'A' + 1)) {
    return TRUE;
  }

  return (BOOLEAN)(((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0) &&
                   ((Key->UnicodeChar == Upper) || (Key->UnicodeChar == (CHAR16)(Upper + 0x20))));
}

INTN
RwKeyHexDigit (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  CHAR16  Ch;

  if ((Key->ScanCode != SCAN_NULL) || ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0)) {
    return -1;
  }

  Ch = Key->UnicodeChar;
  if ((Ch >= L'0') && (Ch <= L'9')) {
    return Ch - L'0';
  }

  if ((Ch >= L'a') && (Ch <= L'f')) {
    return Ch - L'a' + 10;
  }

  if ((Ch >= L'A') && (Ch <= L'F')) {
    return Ch - L'A' + 10;
  }

  return -1;
}

//
// ---- Drawing -----------------------------------------------------------------
//

VOID
RwDrawText (
  IN lv_layer_t       *Layer,
  IN CONST lv_font_t  *Font,
  IN CONST lv_area_t  *Area,
  IN CONST CHAR8      *Text,
  IN lv_color_t       Color
  )
{
  lv_draw_label_dsc_t  Dsc;
  lv_area_t            Box;
  INT32                LineH;

  if ((Text == NULL) || (Text[0] == '\0') || (Area->x2 < Area->x1)) {
    return;
  }

  lv_draw_label_dsc_init (&Dsc);
  Dsc.font       = Font;
  Dsc.color      = Color;
  Dsc.text       = Text;
  Dsc.text_local = 1;     // the caller's buffer does not outlive the draw task
  Dsc.flag       = LV_TEXT_FLAG_EXPAND;   // one line, never wrapped; the clip area cuts it
  LineH          = lv_font_get_line_height (Font);
  Box            = *Area;
  Box.y1         = Area->y1 + (lv_area_get_height (Area) - LineH) / 2;
  Box.y2         = Box.y1 + LineH - 1;
  //
  // Clip horizontally: draw into the area only.
  //
  {
    lv_area_t  Clip;
    lv_area_t  Saved;

    Saved   = Layer->_clip_area;
    Clip.x1 = MAX (Saved.x1, Area->x1);
    Clip.y1 = MAX (Saved.y1, Area->y1);
    Clip.x2 = MIN (Saved.x2, Area->x2);
    Clip.y2 = MIN (Saved.y2, Area->y2);
    if ((Clip.x1 > Clip.x2) || (Clip.y1 > Clip.y2)) {
      return;
    }

    Layer->_clip_area = Clip;
    lv_draw_label (Layer, &Dsc, &Box);
    Layer->_clip_area = Saved;
  }
}

VOID
RwDrawFill (
  IN lv_layer_t       *Layer,
  IN CONST lv_area_t  *Area,
  IN lv_color_t       Color
  )
{
  lv_draw_fill_dsc_t  Dsc;

  lv_draw_fill_dsc_init (&Dsc);
  Dsc.color = Color;
  Dsc.opa   = LV_OPA_COVER;
  lv_draw_fill (Layer, &Dsc, Area);
}
