/** @file
  Toolbar: one flat icon button per module, grouped like RW-Everything.
  Hovering a button shows its name in the status bar.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

#define TB_BUTTON_SIZE  26

STATIC CONST RW_COMMAND_ID  mToolbar[] = {
  RwCmdRefresh, RwCmdScreenshot, RwCmdNone,
  RwCmdPci,     RwCmdMemory,     RwCmdIo,     RwCmdIndexData, RwCmdCmos, RwCmdMsr, RwCmdNone,
  RwCmdEc,      RwCmdSuperIo,    RwCmdSmbus,  RwCmdStorage,   RwCmdUsb,  RwCmdNone,
  RwCmdAcpi,    RwCmdSmbios,     RwCmdPcieTree, RwCmdEdid,    RwCmdComPort, RwCmdNone,
  RwCmdCascade, RwCmdTileH,      RwCmdTileV,  RwCmdNone,
  RwCmdAbout
};

STATIC lv_obj_t  *mButtons[ARRAY_SIZE (mToolbar)];
STATIC UINTN     mButtonCount;

STATIC
VOID
OnButtonClicked (
  lv_event_t  *Event
  )
{
  RwRunCommand ((RW_COMMAND_ID)(UINTN)lv_event_get_user_data (Event));
}

STATIC
VOID
OnButtonHover (
  lv_event_t  *Event
  )
{
  CONST RW_COMMAND  *Cmd;
  CHAR8             Text[96];

  Cmd = RwGetCommand ((RW_COMMAND_ID)(UINTN)lv_event_get_user_data (Event));
  if (lv_event_get_code (Event) == LV_EVENT_HOVER_LEAVE) {
    RwStatusSetText ("Ready");
  } else if (Cmd->Planned != NULL) {
    AsciiSPrint (Text, sizeof (Text), "%a (coming in %a)", Cmd->Name, Cmd->Planned);
    RwStatusSetText (Text);
  } else {
    RwStatusSetText (Cmd->Name);
  }
}

VOID
RwToolbarCreate (
  IN lv_obj_t  *Parent
  )
{
  lv_obj_t          *Bar;
  lv_obj_t          *Obj;
  lv_obj_t          *Image;
  CONST RW_COMMAND  *Cmd;
  UINTN             Index;

  Bar = lv_obj_create (Parent);
  lv_obj_remove_style_all (Bar);
  lv_obj_set_size (Bar, LV_PCT (100), RW_TOOLBAR_HEIGHT);
  lv_obj_set_style_bg_color (Bar, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side (Bar, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width (Bar, 1, 0);
  lv_obj_set_style_border_color (Bar, lv_color_hex (0xD5D5D5), 0);
  lv_obj_set_style_pad_left (Bar, 4, 0);
  lv_obj_set_style_pad_column (Bar, 1, 0);
  lv_obj_set_flex_flow (Bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Bar, FALSE);
  gRwFrame.Toolbar = Bar;

  mButtonCount = 0;
  for (Index = 0; Index < ARRAY_SIZE (mToolbar); Index++) {
    if (mToolbar[Index] == RwCmdNone) {
      Obj = lv_obj_create (Bar);
      lv_obj_remove_style_all (Obj);
      lv_obj_set_size (Obj, 1, TB_BUTTON_SIZE - 4);
      lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_SHADOW), 0);
      lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, 0);
      lv_obj_set_style_margin_left (Obj, 3, 0);
      lv_obj_set_style_margin_right (Obj, 3, 0);
      continue;
    }

    Cmd = RwGetCommand (mToolbar[Index]);
    Obj = lv_obj_create (Bar);
    lv_obj_remove_style_all (Obj);
    lv_obj_set_size (Obj, TB_BUTTON_SIZE, TB_BUTTON_SIZE);
    lv_obj_set_style_radius (Obj, 2, 0);
    lv_obj_set_style_bg_opa (Obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_HILITE_SOFT), LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, LV_STATE_HOVERED);
    lv_obj_set_style_border_color (Obj, lv_color_hex (RW_COLOR_HILITE_EDGE), LV_STATE_HOVERED);
    lv_obj_set_style_border_width (Obj, 1, LV_STATE_HOVERED);
    lv_obj_set_style_bg_color (Obj, lv_color_hex (RW_COLOR_HILITE_EDGE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa (Obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_image_opa (Obj, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_scrollable (Obj, FALSE);
    lv_obj_set_clickable (Obj, TRUE);
    lv_obj_add_event_cb (Obj, OnButtonClicked, LV_EVENT_CLICKED, (VOID *)(UINTN)Cmd->Id);
    lv_obj_add_event_cb (Obj, OnButtonHover, LV_EVENT_HOVER_OVER, (VOID *)(UINTN)Cmd->Id);
    lv_obj_add_event_cb (Obj, OnButtonHover, LV_EVENT_HOVER_LEAVE, (VOID *)(UINTN)Cmd->Id);

    Image = lv_image_create (Obj);
    lv_image_set_src (Image, Cmd->Icon);
    lv_obj_center (Image);
    lv_obj_set_clickable (Image, FALSE);
    lv_obj_set_style_image_opa (Image, LV_OPA_30, LV_STATE_DISABLED);
    mButtons[Index] = Obj;
    mButtonCount++;
  }

  RwToolbarUpdate ();
}

VOID
RwToolbarUpdate (
  VOID
  )
{
  UINTN     Index;
  BOOLEAN   Enabled;
  lv_obj_t  *Image;

  for (Index = 0; Index < ARRAY_SIZE (mToolbar); Index++) {
    if (mButtons[Index] == NULL) {
      continue;
    }

    Enabled = RwIsCommandEnabled (mToolbar[Index]);
    Image   = lv_obj_get_child (mButtons[Index], 0);
    if (Enabled) {
      lv_obj_set_disabled (mButtons[Index], FALSE);
      lv_obj_set_disabled (Image, FALSE);
    } else {
      lv_obj_set_disabled (mButtons[Index], TRUE);
      lv_obj_set_disabled (Image, TRUE);
    }
  }
}

UINTN
RwToolbarButtonCount (
  VOID
  )
{
  return mButtonCount;
}

lv_obj_t *
RwToolbarGetButton (
  IN RW_COMMAND_ID  Id
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mToolbar); Index++) {
    if (mToolbar[Index] == Id) {
      return mButtons[Index];
    }
  }

  return NULL;
}
