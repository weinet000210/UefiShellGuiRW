/** @file
  Status bar: hint text, active window, display mode and version.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

STATIC lv_obj_t  *mHint;
STATIC lv_obj_t  *mWindow;
STATIC lv_obj_t  *mInfo;

STATIC
lv_obj_t *
Panel (
  IN lv_obj_t  *Bar,
  IN INT32     Width   // 0 = grow
  )
{
  lv_obj_t  *Obj;
  lv_obj_t  *Label;

  Obj = lv_obj_create (Bar);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_height (Obj, RW_STATUSBAR_HEIGHT - 4);
  if (Width == 0) {
    lv_obj_set_flex_grow (Obj, 1);
  } else {
    lv_obj_set_width (Obj, Width);
  }

  lv_obj_set_style_border_width (Obj, 1, 0);
  lv_obj_set_style_border_color (Obj, lv_color_hex (0xD0D0D0), 0);
  lv_obj_set_style_pad_left (Obj, 4, 0);
  lv_obj_set_style_pad_right (Obj, 4, 0);
  lv_obj_set_scrollable (Obj, FALSE);
  Label = lv_label_create (Obj);
  lv_label_set_text (Label, "");
  lv_label_set_long_mode (Label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width (Label, LV_PCT (100));
  lv_obj_align (Label, LV_ALIGN_LEFT_MID, 0, 0);
  return Label;
}

VOID
RwStatusBarCreate (
  IN lv_obj_t  *Parent
  )
{
  lv_obj_t                *Bar;
  UEFI_GUI_DISPLAY_INFO   Info;
  CHAR8                   Text[96];

  Bar = lv_obj_create (Parent);
  lv_obj_remove_style_all (Bar);
  lv_obj_set_size (Bar, LV_PCT (100), RW_STATUSBAR_HEIGHT);
  lv_obj_set_style_bg_color (Bar, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side (Bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width (Bar, 1, 0);
  lv_obj_set_style_border_color (Bar, lv_color_hex (0xD5D5D5), 0);
  lv_obj_set_style_pad_hor (Bar, 2, 0);
  lv_obj_set_style_pad_column (Bar, 2, 0);
  lv_obj_set_style_text_font (Bar, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color (Bar, lv_color_black (), 0);
  lv_obj_set_flex_flow (Bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Bar, FALSE);
  gRwFrame.StatusBar = Bar;

  mHint   = Panel (Bar, 0);
  mWindow = Panel (Bar, 260);
  mInfo   = Panel (Bar, 250);

  RwStatusSetText ("Ready");
  RwStatusSetWindow (NULL);
  ZeroMem (&Info, sizeof (Info));
  if (!EFI_ERROR (UefiGuiGetDisplayInfo (&Info))) {
    AsciiSPrint (
      Text,
      sizeof (Text),
      "%ux%u  |  %a %a",
      Info.HorizontalResolution,
      Info.VerticalResolution,
      RW_APP_NAME,
      RW_APP_VERSION
      );
  } else {
    AsciiSPrint (Text, sizeof (Text), "%a %a", RW_APP_NAME, RW_APP_VERSION);
  }

  lv_label_set_text (mInfo, Text);
}

VOID
RwStatusSetText (
  IN CONST CHAR8  *Text
  )
{
  if (mHint != NULL) {
    lv_label_set_text (mHint, Text);
  }
}

VOID
RwStatusSetWindow (
  IN CONST CHAR8  *Text  OPTIONAL
  )
{
  if (mWindow != NULL) {
    lv_label_set_text (mWindow, (Text != NULL) ? Text : "No window open");
  }
}
