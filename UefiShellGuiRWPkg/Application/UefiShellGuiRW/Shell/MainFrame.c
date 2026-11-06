/** @file
  Main frame: menu bar, toolbar, MDI desktop and status bar, top to bottom,
  like the RW-Everything main window.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

RW_MAIN_FRAME  gRwFrame;

EFI_STATUS
RwMainFrameCreate (
  VOID
  )
{
  lv_obj_t  *Screen;
  lv_obj_t  *Desktop;

  ZeroMem (&gRwFrame, sizeof (gRwFrame));
  Screen = lv_screen_active ();
  lv_obj_remove_style_all (Screen);
  lv_obj_set_style_bg_color (Screen, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font (Screen, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color (Screen, lv_color_black (), 0);
  lv_obj_set_flex_flow (Screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable (Screen, FALSE);
  gRwFrame.Screen = Screen;

  RwMenuBarCreate (Screen);
  RwToolbarCreate (Screen);

  //
  // MDI client area: sunken, gray, clips the child windows.
  //
  Desktop = lv_obj_create (Screen);
  lv_obj_remove_style_all (Desktop);
  lv_obj_set_width (Desktop, LV_PCT (100));
  lv_obj_set_flex_grow (Desktop, 1);
  lv_obj_set_style_bg_color (Desktop, lv_color_hex (RW_COLOR_WORKSPACE), 0);
  lv_obj_set_style_bg_opa (Desktop, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Desktop, 1, 0);
  lv_obj_set_style_border_color (Desktop, lv_color_hex (RW_COLOR_DARK), 0);
  lv_obj_set_scrollable (Desktop, FALSE);
  gRwFrame.Desktop = Desktop;
  RwMdiInitialize (Desktop);

  RwStatusBarCreate (Screen);
  return EFI_SUCCESS;
}
