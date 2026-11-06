/** @file
  Menu bar with drop-down menus (File, Access, Specific, Window, Help).

  Mouse: click a title to open its menu, move over the other titles to
  switch, click an entry to run it, click anywhere else to close.
  Keyboard: F10 opens File, Alt+F/A/S/W/H open a menu, Left/Right switch
  menus, Up/Down select, Enter runs, Esc closes.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

#define MENU_WIDTH        230
#define MENU_ROW_HEIGHT   22
#define MENU_SEP_HEIGHT   7
#define MENU_MAX_ROWS     40
#define MENU_WINDOW_BASE  1000   // row value of "window N" entries in the Window menu

typedef struct {
  CONST CHAR8           *Title;
  CHAR16                AltKey;   // lower case
  CONST RW_COMMAND_ID   *Entries; // RwCmdNone = separator
  UINTN                 Count;
  BOOLEAN               WindowList;
} RW_MENU;

STATIC CONST RW_COMMAND_ID  mFileEntries[]     = { RwCmdRefresh, RwCmdScreenshot, RwCmdDumpAll, RwCmdNone, RwCmdExit };
STATIC CONST RW_COMMAND_ID  mAccessEntries[]   = {
  RwCmdPci,  RwCmdMemory, RwCmdIo,      RwCmdIndexData, RwCmdNone,
  RwCmdCmos, RwCmdMsr,    RwCmdNone,
  RwCmdEc,   RwCmdSuperIo, RwCmdSmbus,  RwCmdStorage,   RwCmdUsb
};
STATIC CONST RW_COMMAND_ID  mSpecificEntries[] = { RwCmdAcpi, RwCmdSmbios, RwCmdPcieTree, RwCmdNone, RwCmdEdid, RwCmdComPort };
STATIC CONST RW_COMMAND_ID  mWindowEntries[]   = { RwCmdCascade, RwCmdTileH, RwCmdTileV, RwCmdNone, RwCmdCloseAll };
STATIC CONST RW_COMMAND_ID  mHelpEntries[]     = { RwCmdAbout };

STATIC CONST RW_MENU  mMenus[] = {
  { "File",     L'f', mFileEntries,     ARRAY_SIZE (mFileEntries),     FALSE },
  { "Access",   L'a', mAccessEntries,   ARRAY_SIZE (mAccessEntries),   FALSE },
  { "Specific", L's', mSpecificEntries, ARRAY_SIZE (mSpecificEntries), FALSE },
  { "Window",   L'w', mWindowEntries,   ARRAY_SIZE (mWindowEntries),   TRUE  },
  { "Help",     L'h', mHelpEntries,     ARRAY_SIZE (mHelpEntries),     FALSE },
};

typedef struct {
  lv_obj_t    *Obj;
  UINTN       Value;     // RW_COMMAND_ID, or MENU_WINDOW_BASE + window index
  BOOLEAN     Enabled;
} RW_MENU_ROW;

STATIC lv_obj_t     *mTitles[ARRAY_SIZE (mMenus)];
STATIC lv_obj_t     *mBackdrop;
STATIC lv_obj_t     *mPopup;
STATIC INTN         mOpen = -1;
STATIC RW_MENU_ROW  mRows[MENU_MAX_ROWS];
STATIC UINTN        mRowCount;
STATIC INTN         mSelected = -1;

STATIC
VOID
SetTitleHighlight (
  IN UINTN    Index,
  IN BOOLEAN  On
  )
{
  lv_obj_set_style_bg_opa (mTitles[Index], On ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color (mTitles[Index], lv_color_hex (On ? 0xFFFFFF : RW_COLOR_TEXT), 0);
}

STATIC
VOID
SelectRow (
  IN INTN  Index
  )
{
  if ((mSelected >= 0) && ((UINTN)mSelected < mRowCount)) {
    lv_obj_set_style_bg_opa (mRows[mSelected].Obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color (
      mRows[mSelected].Obj,
      lv_color_hex (mRows[mSelected].Enabled ? RW_COLOR_TEXT : RW_COLOR_TEXT_GRAY),
      0
      );
  }

  mSelected = Index;
  if ((Index >= 0) && ((UINTN)Index < mRowCount)) {
    lv_obj_set_style_bg_opa (mRows[Index].Obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color (
      mRows[Index].Obj,
      lv_color_hex (mRows[Index].Enabled ? 0xFFFFFF : 0xC8C8C8),
      0
      );
  }
}

STATIC
VOID
RunRow (
  IN UINTN  Value
  )
{
  RW_MDI_WINDOW  *Window;

  RwMenuClose ();
  if (Value >= MENU_WINDOW_BASE) {
    Window = RwMdiGetAt (Value - MENU_WINDOW_BASE);
    if (Window != NULL) {
      if (Window->Minimized) {
        RwMdiSetMinimized (Window, FALSE);
      }

      RwMdiActivate (Window);
    }
  } else {
    RwRunCommand ((RW_COMMAND_ID)Value);
  }
}

STATIC
VOID
OnRowHover (
  lv_event_t  *Event
  )
{
  SelectRow ((INTN)(UINTN)lv_event_get_user_data (Event));
}

STATIC
VOID
OnRowClicked (
  lv_event_t  *Event
  )
{
  UINTN  Index;

  Index = (UINTN)lv_event_get_user_data (Event);
  if ((Index < mRowCount) && mRows[Index].Enabled) {
    RunRow (mRows[Index].Value);
  }
}

STATIC
VOID
AddSeparator (
  VOID
  )
{
  lv_obj_t  *Sep;
  lv_obj_t  *Line;

  Sep = lv_obj_create (mPopup);
  lv_obj_remove_style_all (Sep);
  lv_obj_set_size (Sep, LV_PCT (100), MENU_SEP_HEIGHT);
  lv_obj_set_clickable (Sep, FALSE);
  Line = lv_obj_create (Sep);
  lv_obj_remove_style_all (Line);
  lv_obj_set_size (Line, MENU_WIDTH - 36, 1);
  lv_obj_align (Line, LV_ALIGN_LEFT_MID, 28, 0);
  lv_obj_set_style_bg_color (Line, lv_color_hex (0xD7D7D7), 0);
  lv_obj_set_style_bg_opa (Line, LV_OPA_COVER, 0);
}

STATIC
VOID
AddRow (
  IN CONST lv_image_dsc_t  *Icon      OPTIONAL,
  IN CONST CHAR8           *Text,
  IN CONST CHAR8           *Shortcut  OPTIONAL,
  IN BOOLEAN               Checked,
  IN BOOLEAN               Enabled,
  IN UINTN                 Value
  )
{
  lv_obj_t  *Row;
  lv_obj_t  *Obj;

  if (mRowCount >= MENU_MAX_ROWS) {
    return;
  }

  Row = lv_obj_create (mPopup);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_PCT (100), MENU_ROW_HEIGHT);
  lv_obj_set_style_bg_color (Row, lv_color_hex (RW_COLOR_HILITE), 0);
  lv_obj_set_style_bg_opa (Row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color (Row, lv_color_hex (Enabled ? RW_COLOR_TEXT : RW_COLOR_TEXT_GRAY), 0);
  lv_obj_set_style_pad_left (Row, 4, 0);
  lv_obj_set_style_pad_right (Row, 10, 0);
  lv_obj_set_style_pad_column (Row, 8, 0);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Row, FALSE);
  lv_obj_set_clickable (Row, TRUE);
  lv_obj_add_event_cb (Row, OnRowHover, LV_EVENT_HOVER_OVER, (VOID *)mRowCount);
  lv_obj_add_event_cb (Row, OnRowHover, LV_EVENT_PRESSED, (VOID *)mRowCount);
  lv_obj_add_event_cb (Row, OnRowClicked, LV_EVENT_CLICKED, (VOID *)mRowCount);

  if (Icon != NULL) {
    Obj = lv_image_create (Row);
    lv_image_set_src (Obj, Icon);
    lv_image_set_inner_align (Obj, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_size (Obj, 16, 16);
    if (!Enabled) {
      lv_obj_set_style_image_opa (Obj, LV_OPA_40, 0);
    }
  } else {
    Obj = lv_label_create (Row);
    lv_label_set_text (Obj, Checked ? LV_SYMBOL_OK : "");
    lv_obj_set_width (Obj, 16);
  }

  lv_obj_set_clickable (Obj, FALSE);

  Obj = lv_label_create (Row);
  lv_label_set_text (Obj, Text);
  lv_label_set_long_mode (Obj, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_flex_grow (Obj, 1);
  lv_obj_set_clickable (Obj, FALSE);

  if (Shortcut != NULL) {
    Obj = lv_label_create (Row);
    lv_label_set_text (Obj, Shortcut);
    lv_obj_set_clickable (Obj, FALSE);
  }

  mRows[mRowCount].Obj     = Row;
  mRows[mRowCount].Value   = Value;
  mRows[mRowCount].Enabled = Enabled;
  mRowCount++;
}

STATIC
VOID
OnBackdropPressed (
  lv_event_t  *Event
  )
{
  RwMenuClose ();
}

STATIC
VOID
OnTitleClicked (
  lv_event_t  *Event
  )
{
  UINTN  Index;

  Index = (UINTN)lv_event_get_user_data (Event);
  if (mOpen == (INTN)Index) {
    RwMenuClose ();
  } else {
    RwMenuOpen (Index);
  }
}

STATIC
VOID
OnTitleHover (
  lv_event_t  *Event
  )
{
  UINTN  Index;

  Index = (UINTN)lv_event_get_user_data (Event);
  if ((mOpen >= 0) && (mOpen != (INTN)Index)) {
    RwMenuOpen (Index);
  }
}

STATIC
VOID
OnBarPressed (
  lv_event_t  *Event
  )
{
  if (lv_event_get_target_obj (Event) == lv_event_get_current_target_obj (Event)) {
    RwMenuClose ();
  }
}

VOID
RwMenuBarCreate (
  IN lv_obj_t  *Parent
  )
{
  lv_obj_t  *Bar;
  lv_obj_t  *Title;
  lv_obj_t  *Label;
  UINTN     Index;

  Bar = lv_obj_create (Parent);
  lv_obj_remove_style_all (Bar);
  lv_obj_set_size (Bar, LV_PCT (100), RW_MENUBAR_HEIGHT);
  lv_obj_set_style_bg_color (Bar, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (Bar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left (Bar, 2, 0);
  lv_obj_set_flex_flow (Bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable (Bar, FALSE);
  lv_obj_set_clickable (Bar, TRUE);
  lv_obj_add_event_cb (Bar, OnBarPressed, LV_EVENT_PRESSED, NULL);
  gRwFrame.MenuBar = Bar;

  for (Index = 0; Index < ARRAY_SIZE (mMenus); Index++) {
    Title = lv_obj_create (Bar);
    lv_obj_remove_style_all (Title);
    lv_obj_set_size (Title, LV_SIZE_CONTENT, RW_MENUBAR_HEIGHT - 2);
    lv_obj_set_style_pad_left (Title, 8, 0);
    lv_obj_set_style_pad_right (Title, 8, 0);
    lv_obj_set_style_bg_color (Title, lv_color_hex (RW_COLOR_HILITE), 0);
    lv_obj_set_style_bg_opa (Title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color (Title, lv_color_hex (RW_COLOR_TEXT), 0);
    lv_obj_set_style_border_color (Title, lv_color_hex (RW_COLOR_HILITE_EDGE), LV_STATE_HOVERED);
    lv_obj_set_style_border_width (Title, 1, LV_STATE_HOVERED);
    lv_obj_set_scrollable (Title, FALSE);
    lv_obj_set_clickable (Title, TRUE);
    lv_obj_add_event_cb (Title, OnTitleClicked, LV_EVENT_PRESSED, (VOID *)Index);
    lv_obj_add_event_cb (Title, OnTitleHover, LV_EVENT_HOVER_OVER, (VOID *)Index);
    Label = lv_label_create (Title);
    lv_label_set_text (Label, mMenus[Index].Title);
    lv_obj_center (Label);
    lv_obj_set_clickable (Label, FALSE);
    mTitles[Index] = Title;
  }
}

VOID
RwMenuClose (
  VOID
  )
{
  if (mOpen < 0) {
    return;
  }

  SetTitleHighlight ((UINTN)mOpen, FALSE);
  //
  // Called from the popup's own event callbacks, so delete asynchronously.
  //
  lv_obj_set_hidden (mPopup, TRUE);
  lv_obj_set_hidden (mBackdrop, TRUE);
  lv_obj_delete_async (mPopup);
  lv_obj_delete_async (mBackdrop);
  mPopup    = NULL;
  mBackdrop = NULL;
  mOpen     = -1;
  mRowCount = 0;
  mSelected = -1;
}

VOID
RwMenuOpen (
  IN UINTN  Index
  )
{
  CONST RW_MENU     *Menu;
  CONST RW_COMMAND  *Cmd;
  RW_MDI_WINDOW     *Window;
  UINTN             Entry;
  UINTN             Count;
  CHAR8             Text[96];
  INT32             X;
  INT32             Height;
  INT32             ScreenW;
  INT32             ScreenH;

  if (Index >= ARRAY_SIZE (mMenus)) {
    return;
  }

  RwMenuClose ();
  Menu  = &mMenus[Index];
  mOpen = (INTN)Index;
  SetTitleHighlight (Index, TRUE);

  ScreenW = lv_display_get_horizontal_resolution (NULL);
  ScreenH = lv_display_get_vertical_resolution (NULL);

  //
  // Transparent backdrop below the menu bar: a click there closes the menu.
  // The menu bar itself stays reachable so moving over another title
  // switches menus.
  //
  mBackdrop = lv_obj_create (lv_layer_top ());
  lv_obj_remove_style_all (mBackdrop);
  lv_obj_set_pos (mBackdrop, 0, RW_MENUBAR_HEIGHT);
  lv_obj_set_size (mBackdrop, ScreenW, ScreenH - RW_MENUBAR_HEIGHT);
  lv_obj_set_clickable (mBackdrop, TRUE);
  lv_obj_add_event_cb (mBackdrop, OnBackdropPressed, LV_EVENT_PRESSED, NULL);

  mPopup = lv_obj_create (lv_layer_top ());
  lv_obj_remove_style_all (mPopup);
  lv_obj_set_style_bg_color (mPopup, lv_color_hex (RW_COLOR_FACE), 0);
  lv_obj_set_style_bg_opa (mPopup, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (mPopup, 1, 0);
  lv_obj_set_style_border_color (mPopup, lv_color_hex (0x979797), 0);
  lv_obj_set_style_shadow_width (mPopup, 8, 0);
  lv_obj_set_style_shadow_offset_x (mPopup, 3, 0);
  lv_obj_set_style_shadow_offset_y (mPopup, 3, 0);
  lv_obj_set_style_shadow_opa (mPopup, LV_OPA_40, 0);
  lv_obj_set_style_pad_all (mPopup, 2, 0);
  lv_obj_set_style_text_font (mPopup, &lv_font_montserrat_14, 0);
  lv_obj_set_flex_flow (mPopup, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable (mPopup, FALSE);
  lv_obj_set_clickable (mPopup, TRUE);

  Height = 6;
  for (Entry = 0; Entry < Menu->Count; Entry++) {
    if (Menu->Entries[Entry] == RwCmdNone) {
      AddSeparator ();
      Height += MENU_SEP_HEIGHT;
      continue;
    }

    Cmd = RwGetCommand (Menu->Entries[Entry]);
    AddRow (Cmd->Icon, Cmd->Name, Cmd->Shortcut, FALSE, RwIsCommandEnabled (Cmd->Id), Cmd->Id);
    Height += MENU_ROW_HEIGHT;
  }

  if (Menu->WindowList && (RwMdiCount () > 0)) {
    AddSeparator ();
    Height += MENU_SEP_HEIGHT;
    for (Count = 0; Count < RwMdiCount () && Count < 9; Count++) {
      Window = RwMdiGetAt (Count);
      AsciiSPrint (Text, sizeof (Text), "%u %a", (UINT32)(Count + 1), lv_label_get_text (Window->Title));
      AddRow (NULL, Text, NULL, (BOOLEAN)(Window == RwMdiGetActive ()), TRUE, MENU_WINDOW_BASE + Count);
      Height += MENU_ROW_HEIGHT;
    }
  }

  lv_obj_update_layout (gRwFrame.MenuBar);
  X = lv_obj_get_x (mTitles[Index]);
  X = MIN (X, ScreenW - MENU_WIDTH);
  lv_obj_set_pos (mPopup, X, RW_MENUBAR_HEIGHT - 1);
  lv_obj_set_size (mPopup, MENU_WIDTH, MIN (Height, ScreenH - RW_MENUBAR_HEIGHT));

  //
  // Keyboard users start on the first enabled entry.
  //
  for (Entry = 0; Entry < mRowCount; Entry++) {
    if (mRows[Entry].Enabled) {
      SelectRow ((INTN)Entry);
      break;
    }
  }
}

BOOLEAN
RwMenuIsOpen (
  VOID
  )
{
  return (BOOLEAN)(mOpen >= 0);
}

INTN
RwMenuOpenIndex (
  VOID
  )
{
  return mOpen;
}

UINTN
RwMenuCount (
  VOID
  )
{
  return ARRAY_SIZE (mMenus);
}

STATIC
VOID
MoveSelection (
  IN INTN  Delta
  )
{
  INTN   Index;
  UINTN  Tries;

  if (mRowCount == 0) {
    return;
  }

  Index = mSelected;
  for (Tries = 0; Tries < mRowCount; Tries++) {
    Index = (Index + Delta + (INTN)mRowCount) % (INTN)mRowCount;
    if (mRows[Index].Enabled) {
      SelectRow (Index);
      return;
    }
  }
}

BOOLEAN
RwMenuHandleKey (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  UINTN   Index;
  CHAR16  Char;

  Char = Key->UnicodeChar;
  if ((Char >= L'A') && (Char <= L'Z')) {
    Char = (CHAR16)(Char - L'A' + L'a');
  }

  if (mOpen < 0) {
    if (Key->ScanCode == SCAN_F10) {
      RwMenuOpen (0);
      return TRUE;
    }

    if ((Key->Modifiers & UEFI_GUI_KEY_MOD_ALT) != 0) {
      for (Index = 0; Index < ARRAY_SIZE (mMenus); Index++) {
        if (Char == mMenus[Index].AltKey) {
          RwMenuOpen (Index);
          return TRUE;
        }
      }
    }

    return FALSE;
  }

  //
  // A menu is open: it owns the keyboard.
  //
  switch (Key->ScanCode) {
    case SCAN_ESC:
    case SCAN_F10:
      RwMenuClose ();
      return TRUE;
    case SCAN_LEFT:
      RwMenuOpen ((UINTN)((mOpen + (INTN)ARRAY_SIZE (mMenus) - 1) % (INTN)ARRAY_SIZE (mMenus)));
      return TRUE;
    case SCAN_RIGHT:
      RwMenuOpen ((UINTN)((mOpen + 1) % (INTN)ARRAY_SIZE (mMenus)));
      return TRUE;
    case SCAN_UP:
      MoveSelection (-1);
      return TRUE;
    case SCAN_DOWN:
      MoveSelection (1);
      return TRUE;
    case SCAN_HOME:
      SelectRow (-1);
      MoveSelection (1);
      return TRUE;
    case SCAN_END:
      SelectRow (-1);
      mSelected = (INTN)mRowCount;
      MoveSelection (-1);
      return TRUE;
    default:
      break;
  }

  if ((Char == CHAR_CARRIAGE_RETURN) || (Char == L' ')) {
    if ((mSelected >= 0) && mRows[mSelected].Enabled) {
      RunRow (mRows[mSelected].Value);
    }

    return TRUE;
  }

  if ((Key->Modifiers & UEFI_GUI_KEY_MOD_ALT) != 0) {
    for (Index = 0; Index < ARRAY_SIZE (mMenus); Index++) {
      if (Char == mMenus[Index].AltKey) {
        RwMenuOpen (Index);
        return TRUE;
      }
    }
  }

  return TRUE;
}
