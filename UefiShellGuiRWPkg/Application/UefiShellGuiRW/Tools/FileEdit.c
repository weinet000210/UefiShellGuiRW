/** @file
  File editor windows opened from the File Browser: a text editor for small
  text files (ASCII / UTF-8, or UCS-2 with a BOM), else a hex editor (up to
  16 MB). Edits stay in memory; Ctrl+S asks, then writes the file. Closing
  a changed file asks before the changes are dropped. Files are never
  deleted or renamed here.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"

STATIC RW_FILE_EDIT  *mEditors;

//
// ---- Helpers -------------------------------------------------------------------
//

STATIC
VOID
UpdateTitle (
  IN RW_FILE_EDIT  *Edit
  )
{
  CHAR8  Title[96];

  AsciiSPrint (Title, sizeof (Title), "%a%a - %a", Edit->Dirty ? "*" : "", Edit->Name, Edit->Text ? "Text Editor" : "Hex Editor");
  RwMdiSetTitle (Edit->Window, Title);
}

STATIC
VOID
SetDirty (
  IN RW_FILE_EDIT  *Edit,
  IN BOOLEAN       Dirty
  )
{
  if (Edit->Dirty != Dirty) {
    Edit->Dirty = Dirty;
    UpdateTitle (Edit);
  }
}

/**
  Small, no NUL bytes, mostly printable: a text file.
**/
STATIC
BOOLEAN
LooksLikeText (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  UINTN  Index;
  UINTN  Odd;

  if (Size > RW_TEXT_EDIT_MAX) {
    return FALSE;
  }

  Odd = 0;
  for (Index = 0; Index < Size; Index++) {
    if (Data[Index] == 0) {
      return FALSE;
    }

    if ((Data[Index] < 0x20) && (Data[Index] != '\r') && (Data[Index] != '\n') && (Data[Index] != '\t')) {
      Odd++;
    }
  }

  return (BOOLEAN)(Odd * 50 <= Size);
}

/**
  UCS-2 LE with BOM whose characters are all ASCII: the ASCII text, else NULL.
**/
STATIC
CHAR8 *
Ucs2ToText (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  UINTN   Count;
  UINTN   Index;
  UINTN   Out;
  CHAR16  Ch;
  CHAR8   *Text;

  if ((Size < 2) || (Size % 2 != 0) || (Data[0] != 0xFF) || (Data[1] != 0xFE) || (Size - 2 > RW_TEXT_EDIT_MAX * 2)) {
    return NULL;
  }

  Count = (Size - 2) / 2;
  Text  = AllocatePool (Count + 1);
  if (Text == NULL) {
    return NULL;
  }

  for (Index = 0, Out = 0; Index < Count; Index++) {
    Ch = (CHAR16)(Data[2 + Index * 2] | (Data[3 + Index * 2] << 8));
    if ((Ch == 0) || (Ch > 0x7E) || ((Ch < 0x20) && (Ch != L'\r') && (Ch != L'\n') && (Ch != L'\t'))) {
      FreePool (Text);
      return NULL;
    }

    if (Ch != L'\r') {
      Text[Out++] = (CHAR8)Ch;
    }
  }

  Text[Out] = '\0';
  return Text;
}

/**
  The text with "\r\n" turned into "\n" (the editor works with "\n").
**/
STATIC
CHAR8 *
StripCr (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  CHAR8  *Text;
  UINTN  Index;
  UINTN  Out;

  Text = AllocatePool (Size + 1);
  if (Text == NULL) {
    return NULL;
  }

  for (Index = 0, Out = 0; Index < Size; Index++) {
    if ((Data[Index] == '\r') && (Index + 1 < Size) && (Data[Index + 1] == '\n')) {
      continue;
    }

    Text[Out++] = (CHAR8)Data[Index];
  }

  Text[Out] = '\0';
  return Text;
}

//
// ---- Hex editor data source ------------------------------------------------------
//

STATIC
EFI_STATUS
EFIAPI
BufRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  RW_FILE_EDIT  *Edit;

  Edit   = (RW_FILE_EDIT *)(UINTN)Source->Ctx[0];
  *Value = 0;
  CopyMem (Value, Edit->Data + (UINTN)Address, Width);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BufWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  RW_FILE_EDIT  *Edit;

  Edit = (RW_FILE_EDIT *)(UINTN)Source->Ctx[0];
  CopyMem (Edit->Data + (UINTN)Address, &Value, Width);
  SetDirty (Edit, TRUE);
  return EFI_SUCCESS;
}

//
// ---- Text editor widget -----------------------------------------------------------
//

/**
  Character index of the start (or end) of the line holding character Pos.
  Positions count UTF-8 characters, as lv_textarea does.
**/
STATIC
UINTN
LineEdge (
  IN CONST CHAR8  *Text,
  IN UINTN        Pos,
  IN BOOLEAN      End
  )
{
  UINTN  Byte;
  UINTN  Char;
  UINTN  LineStart;

  LineStart = 0;
  for (Byte = 0, Char = 0; Text[Byte] != '\0'; Byte++) {
    if (((UINT8)Text[Byte] & 0xC0) == 0x80) {
      continue;          // UTF-8 continuation byte
    }

    if (Char >= Pos) {
      if (!End) {
        return LineStart;
      }

      if (Text[Byte] == '\n') {
        return Char;
      }
    }

    if (Text[Byte] == '\n') {
      LineStart = Char + 1;
    }

    Char++;
  }

  return End ? Char : LineStart;
}

STATIC
BOOLEAN
TextKey (
  IN RW_WIDGET                 *Widget,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  RW_FILE_EDIT  *Edit;
  UINT32        Code;
  UINTN         Lines;

  Edit = (RW_FILE_EDIT *)lv_obj_get_user_data (Widget->Obj);
  if (Edit == NULL) {
    return FALSE;
  }

  //
  // Home / End: start / end of the line; with Ctrl, of the text.
  //
  if ((Key->ScanCode == SCAN_HOME) || (Key->ScanCode == SCAN_END)) {
    if ((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0) {
      lv_textarea_set_cursor_pos (Edit->Area, (Key->ScanCode == SCAN_HOME) ? 0 : LV_TEXTAREA_CURSOR_LAST);
    } else {
      lv_textarea_set_cursor_pos (Edit->Area, (INT32)LineEdge (lv_textarea_get_text (Edit->Area), lv_textarea_get_cursor_pos (Edit->Area), Key->ScanCode == SCAN_END));
    }

    return TRUE;
  }

  if ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) != 0) {
    return FALSE;
  }

  if ((Key->ScanCode == SCAN_PAGE_UP) || (Key->ScanCode == SCAN_PAGE_DOWN)) {
    for (Lines = 0; Lines < 20; Lines++) {
      if (Key->ScanCode == SCAN_PAGE_UP) {
        lv_textarea_cursor_up (Edit->Area);
      } else {
        lv_textarea_cursor_down (Edit->Area);
      }
    }

    return TRUE;
  }

  Code = UefiGuiKeyToLvgl (Key);
  if (Code == 0) {
    return FALSE;
  }

  lv_obj_send_event (Edit->Area, LV_EVENT_KEY, &Code);
  return TRUE;
}

STATIC
VOID
TextFocus (
  IN RW_WIDGET  *Widget,
  IN BOOLEAN    Focused
  )
{
  lv_obj_set_state (Widget->Obj, LV_STATE_FOCUSED, Focused);
  lv_obj_set_style_border_color (Widget->Obj, lv_color_hex (Focused ? RW_COLOR_HILITE_EDGE : RW_COLOR_SHADOW), 0);
}

STATIC
VOID
TextChanged (
  lv_event_t  *Event
  )
{
  RW_FILE_EDIT  *Edit;

  Edit = (RW_FILE_EDIT *)lv_event_get_user_data (Event);
  SetDirty (Edit, TRUE);
}

STATIC
VOID
TextDeleted (
  lv_event_t  *Event
  )
{
  RW_WIDGET  *Widget;

  Widget = (RW_WIDGET *)lv_event_get_user_data (Event);
  RwFocusRemove (Widget);
  FreePool (Widget);
}

//
// ---- Window ------------------------------------------------------------------------
//

STATIC
VOID
OnSaveDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_FILE_EDIT  *Edit;
  EFI_STATUS    Status;
  CHAR8         Text[128];

  Edit = (RW_FILE_EDIT *)Context;
  if (!Accepted || (RwFileEditOf (Edit->Window) != Edit)) {
    return;
  }

  Status = RwFileEditSave (Edit);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "Could not save %a: %r", Edit->Name, Status);
    RwDialogMessage ("Save", Text, NULL, NULL);
  } else {
    AsciiSPrint (Text, sizeof (Text), "Saved %a", Edit->Name);
  }

  RwStatusSetText (Text);
}

STATIC
VOID
OnSave (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_FILE_EDIT  *Edit;
  CHAR8         Text[160];

  Edit = RwFileEditOf (Window);
  if (Edit == NULL) {
    return;
  }

  AsciiSPrint (Text, sizeof (Text), "Save %a?\nThis replaces the file on disk.", Edit->Name);
  RwDialogConfirm ("Save", Text, 0, OnSaveDone, Edit);
}

STATIC
VOID
DiscardDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_FILE_EDIT  *Edit;

  Edit = (RW_FILE_EDIT *)Context;
  if (Accepted && (RwFileEditOf (Edit->Window) == Edit)) {
    Edit->Dirty = FALSE;
    RwMdiClose (Edit->Window);
  }
}

STATIC
BOOLEAN
OnQueryClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_FILE_EDIT  *Edit;
  CHAR8         Text[160];

  Edit = RwFileEditOf (Window);
  if ((Edit == NULL) || !Edit->Dirty) {
    return TRUE;
  }

  AsciiSPrint (Text, sizeof (Text), "%a has unsaved changes.\nClose it and discard them?", Edit->Name);
  RwDialogConfirm ("Close", Text, RW_DIALOG_WARNING, DiscardDone, Edit);
  return FALSE;
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_FILE_EDIT  **Link;
  RW_FILE_EDIT  *Edit;

  Edit = RwFileEditOf (Window);
  if (Edit == NULL) {
    return;
  }

  for (Link = &mEditors; *Link != NULL; Link = &(*Link)->Next) {
    if (*Link == Edit) {
      *Link = Edit->Next;
      break;
    }
  }

  if (Edit->Area != NULL) {
    lv_obj_set_user_data (Edit->Area, NULL);
  }

  if (Edit->Data != NULL) {
    FreePool (Edit->Data);
  }

  FreePool (Edit);
  Window->Context = NULL;
}

RW_FILE_EDIT *
RwFileEditOf (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_FILE_EDIT  *Edit;

  for (Edit = mEditors; Edit != NULL; Edit = Edit->Next) {
    if (Edit->Window == Window) {
      return Edit;
    }
  }

  return NULL;
}

UINTN
RwFileEditDirtyCount (
  VOID
  )
{
  RW_FILE_EDIT  *Edit;
  UINTN         Count;

  Count = 0;
  for (Edit = mEditors; Edit != NULL; Edit = Edit->Next) {
    if (Edit->Dirty) {
      Count++;
    }
  }

  return Count;
}

CONST CHAR8 *
RwFileEditGetText (
  IN RW_FILE_EDIT  *Edit
  )
{
  return (Edit->Text && (Edit->Area != NULL)) ? lv_textarea_get_text (Edit->Area) : "";
}

EFI_STATUS
RwFileEditSave (
  IN RW_FILE_EDIT  *Edit
  )
{
  CONST CHAR8  *Text;
  UINT8        *Out;
  UINTN        Size;
  UINTN        Length;
  UINTN        Index;
  EFI_STATUS   Status;

  if (!Edit->Text) {
    Status = RwFileWrite (Edit->Vol, Edit->Path, Edit->Data, Edit->Size);
  } else {
    //
    // "\n" back to "\r\n" (UEFI text files, Shell scripts), UCS-2 when it was.
    //
    Text   = lv_textarea_get_text (Edit->Area);
    Length = AsciiStrLen (Text);
    Out    = AllocatePool ((Length * 2 + 1) * (Edit->Ucs2 ? 2 : 1) + 2);
    if (Out == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Size = 0;
    if (Edit->Ucs2) {
      Out[Size++] = 0xFF;
      Out[Size++] = 0xFE;
    }

    for (Index = 0; Index < Length; Index++) {
      if (Text[Index] == '\n') {
        Out[Size++] = '\r';
        if (Edit->Ucs2) {
          Out[Size++] = 0;
        }
      }

      Out[Size++] = (UINT8)Text[Index];
      if (Edit->Ucs2) {
        Out[Size++] = 0;
      }
    }

    Status = RwFileWrite (Edit->Vol, Edit->Path, Out, Size);
    FreePool (Out);
  }

  if (!EFI_ERROR (Status)) {
    SetDirty (Edit, FALSE);
    RwFileViewReload ();
  }

  return Status;
}

RW_FILE_EDIT *
RwFileEditOpen (
  IN EFI_HANDLE    Vol,
  IN CONST CHAR16  *Path
  )
{
  RW_FILE_EDIT   *Edit;
  RW_MDI_WINDOW  *Window;
  RW_WIDGET      *Widget;
  UINT8          *Data;
  UINTN          Size;
  CHAR8          *Text;
  CONST CHAR16   *Name;
  CHAR8          Message[160];
  CHAR8          Heading[RW_FILE_PATH_MAX + 64];
  CHAR8          PathText[RW_FILE_PATH_MAX];
  lv_obj_t       *Column;
  EFI_STATUS     Status;
  INT32          PaneW;
  INT32          PaneH;

  for (Edit = mEditors; Edit != NULL; Edit = Edit->Next) {
    if ((Edit->Vol == Vol) && (StrCmp (Edit->Path, Path) == 0)) {
      RwMdiActivate (Edit->Window);
      return Edit;
    }
  }

  Status = RwFileRead (Vol, Path, RW_HEX_EDIT_MAX, &Data, &Size);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_BAD_BUFFER_SIZE) {
      AsciiSPrint (Message, sizeof (Message), "The file is larger than %d MB, the editor limit.", RW_HEX_EDIT_MAX / SIZE_1MB);
    } else {
      AsciiSPrint (Message, sizeof (Message), "Cannot open the file: %r", Status);
    }

    RwDialogMessage ("File Editor", Message, NULL, NULL);
    return NULL;
  }

  Edit = AllocateZeroPool (sizeof (*Edit));
  if (Edit == NULL) {
    FreePool (Data);
    return NULL;
  }

  Edit->Vol = Vol;
  StrCpyS (Edit->Path, ARRAY_SIZE (Edit->Path), Path);
  for (Name = Path + StrLen (Path); Name > Path && Name[-1] != L'\\'; Name--) {
  }

  RwUnicodeToAscii (Name, Edit->Name, sizeof (Edit->Name));
  RwUnicodeToAscii (Path, PathText, sizeof (PathText));

  //
  // Text or hex.
  //
  Text = Ucs2ToText (Data, Size);
  if (Text != NULL) {
    Edit->Text = TRUE;
    Edit->Ucs2 = TRUE;
  } else if ((Size == 0) || LooksLikeText (Data, Size)) {
    Text       = StripCr (Data, Size);
    Edit->Text = (BOOLEAN)(Text != NULL);
  }

  Window = RwMdiCreate (Edit->Name, RwCmdFileEdit, 640, 440);
  if (Window == NULL) {
    FreePool (Data);
    if (Text != NULL) {
      FreePool (Text);
    }

    FreePool (Edit);
    return NULL;
  }

  Edit->Window         = Window;
  Edit->Next           = mEditors;
  mEditors             = Edit;
  Window->Context      = Edit;
  Window->OnClose      = OnClose;
  Window->OnSave       = OnSave;
  Window->OnQueryClose = OnQueryClose;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  if (Edit->Text) {
    FreePool (Data);
    Column = RwViewColumn (Window->Content, 0);
    AsciiSPrint (Heading, sizeof (Heading), "%a   %a, %d bytes   (Ctrl+S: save)", PathText, Edit->Ucs2 ? "UCS-2" : "ASCII / UTF-8", Size);
    RwViewLabel (Column, Heading);
    Edit->Area = lv_textarea_create (RwViewPane (Column, 1));
    lv_obj_set_size (Edit->Area, LV_PCT (100), LV_PCT (100));
    lv_obj_set_style_text_font (Edit->Area, RwMonoFont (), 0);
    lv_obj_set_style_radius (Edit->Area, 0, 0);
    lv_obj_set_style_border_width (Edit->Area, 1, 0);
    lv_obj_set_style_border_color (Edit->Area, lv_color_hex (RW_COLOR_SHADOW), 0);
    lv_obj_set_style_pad_all (Edit->Area, 4, 0);
    lv_textarea_set_text (Edit->Area, Text);
    lv_textarea_set_cursor_pos (Edit->Area, 0);
    FreePool (Text);
    lv_obj_set_user_data (Edit->Area, Edit);
    lv_obj_add_event_cb (Edit->Area, TextChanged, LV_EVENT_VALUE_CHANGED, Edit);

    Widget = AllocateZeroPool (sizeof (RW_WIDGET));
    if (Widget != NULL) {
      Widget->Obj          = Edit->Area;
      Widget->Kind         = "TextEdit";
      Widget->HandleKey    = TextKey;
      Widget->FocusChanged = TextFocus;
      Edit->Widget         = Widget;
      lv_obj_add_event_cb (Edit->Area, TextDeleted, LV_EVENT_DELETE, Widget);
      RwFocusAdd (Window, Widget);
      RwFocusSet (Widget);
    }

    RwMdiFitContent (Window, 620, 400);
  } else {
    if (Text != NULL) {
      FreePool (Text);
    }

    Edit->Data = Data;
    Edit->Size = Size;
    AsciiStrnCpyS (Edit->Source.Name, sizeof (Edit->Source.Name), Edit->Name, sizeof (Edit->Source.Name) - 1);
    Edit->Source.Base        = 0;
    Edit->Source.Limit       = Size - 1;
    Edit->Source.AddrDigits  = 8;
    Edit->Source.WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32 | RW_DS_WIDTH_64;
    Edit->Source.SideEffects = FALSE;
    Edit->Source.Read        = BufRead;
    Edit->Source.Write       = BufWrite;
    Edit->Source.Ctx[0]      = (UINT64)(UINTN)Edit;
    RwSpacePaneCreate (&Edit->Pane, Window->Content, Window, &Edit->Source);
    AsciiSPrint (Heading, sizeof (Heading), "Hex editor: %a, %d bytes; edits stay in memory until Ctrl+S", PathText, Size);
    RwStatusSetText (Heading);
    Edit->Grid = Edit->Pane.Grid;
    RwSpacePaneSize (&Edit->Pane, &PaneW, &PaneH);
    RwMdiFitContent (Window, PaneW, PaneH);
  }

  UpdateTitle (Edit);
  return Edit;
}
