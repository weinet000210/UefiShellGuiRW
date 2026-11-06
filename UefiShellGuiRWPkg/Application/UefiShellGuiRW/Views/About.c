/** @file
  Help > About window: version, firmware, display and input information.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

STATIC
VOID
AddLine (
  IN lv_obj_t     *Parent,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Value
  )
{
  lv_obj_t  *Row;
  lv_obj_t  *Obj;

  Row = lv_obj_create (Parent);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_PCT (100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_event_bubble (Row, TRUE);
  Obj = lv_label_create (Row);
  lv_label_set_text (Obj, Name);
  lv_obj_set_width (Obj, 130);
  lv_obj_set_style_text_color (Obj, lv_color_hex (RW_COLOR_TEXT_GRAY), 0);
  lv_obj_set_event_bubble (Obj, TRUE);
  Obj = lv_label_create (Row);
  lv_label_set_text (Obj, Value);
  lv_label_set_long_mode (Obj, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_flex_grow (Obj, 1);
  lv_obj_set_event_bubble (Obj, TRUE);
}

RW_MDI_WINDOW *
RwViewOpenAbout (
  VOID
  )
{
  RW_MDI_WINDOW          *Window;
  lv_obj_t               *Obj;
  lv_obj_t               *Row;
  UEFI_GUI_DISPLAY_INFO  Info;
  UINT32                 Devices;
  CHAR8                  Text[160];

  Window = RwMdiFind (RwCmdAbout);
  if (Window != NULL) {
    RwMdiActivate (Window);
    return Window;
  }

  Window = RwMdiCreate ("About UefiShellGuiRW", RwCmdAbout, 520, 380);
  if (Window == NULL) {
    return NULL;
  }

  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all (Window->Content, 12, 0);
  lv_obj_set_style_pad_row (Window->Content, 4, 0);
  lv_obj_set_scrollable (Window->Content, TRUE);

  Row = lv_obj_create (Window->Content);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_PCT (100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column (Row, 10, 0);
  lv_obj_set_style_pad_bottom (Row, 8, 0);
  lv_obj_set_event_bubble (Row, TRUE);
  Obj = lv_image_create (Row);
  lv_image_set_src (Obj, &gIconPci);
  lv_image_set_scale (Obj, 512);
  lv_obj_set_size (Obj, 40, 40);
  lv_image_set_inner_align (Obj, LV_IMAGE_ALIGN_CENTER);
  Obj = lv_label_create (Row);
  lv_label_set_text (Obj, RW_APP_NAME " " RW_APP_VERSION);
  lv_obj_set_style_text_font (Obj, &lv_font_montserrat_20, 0);

  AddLine (Window->Content, "Description", "Read & write hardware resources under the UEFI Shell, in the style of RW-Everything.");
  AddLine (Window->Content, "Build", RW_APP_PHASE " (" __DATE__ ")");
  AsciiSPrint (Text, sizeof (Text), "LVGL %d.%d.%d on LVGLUefiGuiPkg", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  AddLine (Window->Content, "GUI", Text);
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%s, rev %u.%u (UEFI %u.%u)",
    gST->FirmwareVendor,
    gST->FirmwareRevision >> 16,
    gST->FirmwareRevision & 0xFFFF,
    gST->Hdr.Revision >> 16,
    (gST->Hdr.Revision & 0xFFFF) / 10
    );
  AddLine (Window->Content, "Firmware", Text);
  if (!EFI_ERROR (UefiGuiGetDisplayInfo (&Info))) {
    AsciiSPrint (
      Text,
      sizeof (Text),
      "%u x %u, mode %u of %u, %a",
      Info.HorizontalResolution,
      Info.VerticalResolution,
      Info.ModeNumber,
      Info.MaxMode,
      (Info.ActiveBackend == UefiGuiDisplayBackendFrameBuffer) ? "frame buffer" : "GOP Blt"
      );
    AddLine (Window->Content, "Display", Text);
  }

  Devices = UefiGuiGetInputDevices ();
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%a%a%a",
    (Devices & (UEFI_GUI_INPUT_DEVICE_KEYBOARD_EX | UEFI_GUI_INPUT_DEVICE_KEYBOARD)) ? "keyboard " : "",
    (Devices & UEFI_GUI_INPUT_DEVICE_SIMPLE_POINTER) ? "mouse " : "",
    (Devices & UEFI_GUI_INPUT_DEVICE_ABSOLUTE_POINTER) ? "absolute-pointer" : ""
    );
  AddLine (Window->Content, "Input", Text);
  AddLine (Window->Content, "License", "BSD-2-Clause-Patent. LVGL is MIT licensed.");
  AddLine (Window->Content, "Warning", "Writing hardware registers can hang or damage the system. Use at your own risk.");
  return Window;
}
