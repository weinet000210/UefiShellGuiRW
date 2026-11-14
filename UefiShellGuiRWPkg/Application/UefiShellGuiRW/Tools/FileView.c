/** @file
  File Browser window: the volumes on the left, the folder on the right.
  Enter opens a folder, or a file in the text / hex editor; Backspace or
  ".." goes up; F5 reloads.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"

#define VOL_WIDTH  250
#define TAG_UP     (-2)

STATIC RW_FILE_VIEW  mFiles;

STATIC
VOID
FormatSize (
  IN  UINT64  Size,
  OUT CHAR8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  if (Size >= SIZE_1GB) {
    AsciiSPrint (Buffer, BufferSize, "%ld.%ld GB", DivU64x32 (Size, SIZE_1GB), DivU64x32 (Size % SIZE_1GB, SIZE_1GB / 10));
  } else if (Size >= SIZE_1MB) {
    AsciiSPrint (Buffer, BufferSize, "%ld.%ld MB", DivU64x32 (Size, SIZE_1MB), DivU64x32 (Size % SIZE_1MB, SIZE_1MB / 10));
  } else if (Size >= SIZE_1KB) {
    AsciiSPrint (Buffer, BufferSize, "%ld KB", DivU64x32 (Size + SIZE_1KB - 1, SIZE_1KB));
  } else {
    AsciiSPrint (Buffer, BufferSize, "%ld bytes", Size);
  }
}

VOID
RwFileViewReload (
  VOID
  )
{
  UINTN         Index;
  CHAR8         Name[128];
  CHAR8         Size[24];
  CHAR8         Time[24];
  CHAR8         Heading[RW_FILE_PATH_MAX + 64];
  CHAR8         Path[RW_FILE_PATH_MAX];
  CONST CHAR8   *Cells[3];
  RW_DIR_ENTRY  *E;
  RW_VOLUME     *Vol;
  EFI_STATUS    Status;

  if (mFiles.Window == NULL) {
    return;
  }

  RwDirFree (mFiles.List, mFiles.ListCount);
  mFiles.List      = NULL;
  mFiles.ListCount = 0;
  RwTableClear (mFiles.Entries);
  if ((mFiles.Vol < 0) || ((UINTN)mFiles.Vol >= mFiles.VolCount)) {
    lv_label_set_text (mFiles.Heading, "Choose a volume");
    return;
  }

  Vol    = &mFiles.Vols[mFiles.Vol];
  Status = RwDirList (Vol->Handle, mFiles.Dir, &mFiles.List, &mFiles.ListCount);
  RwUnicodeToAscii (mFiles.Dir, Path, sizeof (Path));
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Heading, sizeof (Heading), "%a  -  cannot read: %r", Path, Status);
  } else {
    AsciiSPrint (
      Heading,
      sizeof (Heading),
      "%a%a  -  %d items%a",
      (Vol->Label[0] != '\0') ? Vol->Label : "",
      Path,
      mFiles.ListCount,
      Vol->ReadOnly ? "  (read-only)" : ""
      );
  }

  lv_label_set_text (mFiles.Heading, Heading);
  if (StrCmp (mFiles.Dir, L"\\") != 0) {
    Cells[0] = "..";
    Cells[1] = "";
    Cells[2] = "";
    RwTableAddRow (mFiles.Entries, Cells, TAG_UP, RwEmDim);
  }

  for (Index = 0; Index < mFiles.ListCount; Index++) {
    E = &mFiles.List[Index];
    RwUnicodeToAscii (E->Name, Name + 1, sizeof (Name) - 2);
    if (E->IsDir) {
      Name[0] = '[';
      AsciiStrCatS (Name, sizeof (Name), "]");
      Cells[0] = Name;
      Size[0]  = '\0';
    } else {
      Cells[0] = Name + 1;
      FormatSize (E->Size, Size, sizeof (Size));
    }

    AsciiSPrint (
      Time,
      sizeof (Time),
      "%04d-%02d-%02d %02d:%02d",
      E->Time.Year,
      E->Time.Month,
      E->Time.Day,
      E->Time.Hour,
      E->Time.Minute
      );
    Cells[1] = Size;
    Cells[2] = Time;
    RwTableAddRow (mFiles.Entries, Cells, (INT32)Index, E->IsDir ? RwEmStrong : RwEmNormal);
  }

  if (RwTableRowCount (mFiles.Entries) > 0) {
    RwVListSelect (RwTableList (mFiles.Entries), 0);
  }
}

STATIC
VOID
GoUp (
  VOID
  )
{
  UINTN  Length;

  Length = StrLen (mFiles.Dir);
  if (Length <= 1) {
    return;
  }

  //
  // "\A\B\" -> "\A\"
  //
  Length--;
  while ((Length > 0) && (mFiles.Dir[Length - 1] != L'\\')) {
    Length--;
  }

  mFiles.Dir[Length] = L'\0';
  RwFileViewReload ();
}

STATIC
VOID
OnVolumeSelect (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  if (Tag == mFiles.Vol) {
    return;
  }

  mFiles.Vol = Tag;
  StrCpyS (mFiles.Dir, ARRAY_SIZE (mFiles.Dir), L"\\");
  RwFileViewReload ();
}

STATIC
VOID
OnVolumeActivate (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  RwFocusSet (&RwTableList (mFiles.Entries)->Base);
}

STATIC
VOID
OnEntryActivate (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  RW_DIR_ENTRY  *E;
  CHAR16        Path[RW_FILE_PATH_MAX];

  if (Tag == TAG_UP) {
    GoUp ();
    return;
  }

  if ((Tag < 0) || ((UINTN)Tag >= mFiles.ListCount) || (mFiles.Vol < 0)) {
    return;
  }

  E = &mFiles.List[Tag];
  if (StrLen (mFiles.Dir) + StrLen (E->Name) + 2 >= ARRAY_SIZE (Path)) {
    RwDialogMessage ("File Browser", "The path is too long.", NULL, NULL);
    return;
  }

  if (E->IsDir) {
    StrCatS (mFiles.Dir, ARRAY_SIZE (mFiles.Dir), E->Name);
    StrCatS (mFiles.Dir, ARRAY_SIZE (mFiles.Dir), L"\\");
    RwFileViewReload ();
    return;
  }

  StrCpyS (Path, ARRAY_SIZE (Path), mFiles.Dir);
  StrCatS (Path, ARRAY_SIZE (Path), E->Name);
  RwFileEditOpen (mFiles.Vols[mFiles.Vol].Handle, Path);
}

STATIC
BOOLEAN
EntryKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if ((Key->ScanCode == SCAN_NULL) && (Key->UnicodeChar == CHAR_BACKSPACE)) {
    GoUp ();
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwFileViewReload ();
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwDirFree (mFiles.List, mFiles.ListCount);
  if (mFiles.Vols != NULL) {
    FreePool (mFiles.Vols);
  }

  ZeroMem (&mFiles, sizeof (mFiles));
}

RW_MDI_WINDOW *
RwViewOpenFiles (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  UINTN                         Index;
  CHAR8                         Label[48];
  CONST CHAR8                   *Cells[2];
  STATIC CONST RW_VLIST_COLUMN  VolColumns[]   = { { "Volume", 90 }, { "Device", 0 } };
  STATIC CONST RW_VLIST_COLUMN  EntryColumns[] = { { "Name", 0 }, { "Size", 90 }, { "Modified", 130 } };

  if (mFiles.Window != NULL) {
    RwMdiActivate (mFiles.Window);
    return mFiles.Window;
  }

  Window = RwMdiCreate ("File Browser", RwCmdFiles, 760, 460);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mFiles, sizeof (mFiles));
  mFiles.Window     = Window;
  mFiles.Vol        = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  Window->SavePrefix = "DIR";
  StrCpyS (mFiles.Dir, ARRAY_SIZE (mFiles.Dir), L"\\");
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  RwVolumeList (&mFiles.Vols, &mFiles.VolCount);
  Left = RwViewColumn (Window->Content, VOL_WIDTH);
  RwViewLabel (Left, (mFiles.VolCount > 0) ? "Volumes" : "No file system");
  mFiles.Volumes = RwTableCreate (RwViewPane (Left, 1), Window, VolColumns, ARRAY_SIZE (VolColumns), FALSE);
  RwTableSetCallbacks (mFiles.Volumes, OnVolumeSelect, OnVolumeActivate, NULL);

  Right          = RwViewColumn (Window->Content, 0);
  mFiles.Heading = RwViewLabel (Right, "");
  mFiles.Entries = RwTableCreate (RwViewPane (Right, 1), Window, EntryColumns, ARRAY_SIZE (EntryColumns), FALSE);
  RwTableSetCallbacks (mFiles.Entries, NULL, OnEntryActivate, NULL);
  RwTableList (mFiles.Entries)->ExtraKey = EntryKey;

  for (Index = 0; Index < mFiles.VolCount; Index++) {
    AsciiSPrint (
      Label,
      sizeof (Label),
      "%a%a%a",
      (mFiles.Vols[Index].Label[0] != '\0') ? mFiles.Vols[Index].Label : "(no label)",
      mFiles.Vols[Index].IsImage ? " *" : "",
      mFiles.Vols[Index].ReadOnly ? " RO" : ""
      );
    Cells[0] = Label;
    Cells[1] = mFiles.Vols[Index].Path;
    RwTableAddRow (mFiles.Volumes, Cells, (INT32)Index, mFiles.Vols[Index].IsImage ? RwEmStrong : RwEmNormal);
  }

  if (mFiles.VolCount > 0) {
    RwVListSelect (RwTableList (mFiles.Volumes), 0);    // the image volume is first
  } else {
    RwFileViewReload ();
  }

  RwMdiFitContent (Window, VOL_WIDTH + 4 + 480, 420);
  RwFocusSet (&RwTableList (mFiles.Entries)->Base);
  return Window;
}

RW_FILE_VIEW *
RwFileViewGet (
  VOID
  )
{
  return &mFiles;
}
