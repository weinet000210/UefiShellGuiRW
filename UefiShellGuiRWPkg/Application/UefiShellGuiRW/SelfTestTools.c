/** @file
  Phase 6 self test: write log, hex grid snapshots, Save Window (hex range
  and list text), screenshot, Dump All, File Browser and the text / hex
  editors, driven with keys. Every file goes next to UefiShellGuiRW.efi on
  the test ESP; the only memory written is a buffer the test allocates.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools/Tools.h"
#include <Library/UefiGuiLib.h>

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

#define CTRL  UEFI_GUI_KEY_MOD_CTRL

STATIC UINT8  *mBuf;

STATIC
BOOLEAN
LastIs (
  IN CONST CHAR8  *Prefix
  )
{
  return (BOOLEAN)(AsciiStrnCmp (RwToolLastSaved (), Prefix, AsciiStrLen (Prefix)) == 0);
}

/**
  The last saved file (or Name), NUL-terminated. FreePool.
**/
STATIC
CHAR8 *
ReadSaved (
  IN  CONST CHAR8  *Name  OPTIONAL,
  OUT UINTN        *Size
  )
{
  UINT8  *Data;

  if (EFI_ERROR (RwSaveRead ((Name != NULL) ? Name : RwToolLastSaved (), SIZE_16MB, &Data, Size))) {
    *Size = 0;
    return NULL;
  }

  return (CHAR8 *)Data;      // RwFileRead leaves two zero bytes after the data
}

STATIC
BOOLEAN
CellChanged (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset
  )
{
  CHAR8    Text[16];
  BOOLEAN  Changed;

  RwHexGridGetCellText (Grid, Offset, Text, sizeof (Text), &Changed);
  return Changed;
}

STATIC
VOID
TestWriteLogAndSnapshot (
  VOID
  )
{
  RW_MEM_VIEW          *Mem;
  RW_HEXGRID           *Grid;
  CONST RW_WLOG_ENTRY  *Entry;
  UINT64               Page;
  UINTN                Index;
  CHAR8                Number[8];
  CHAR8                *Text;
  UINTN                Size;
  CHAR8                Base[16];
  RW_WLOG_VIEW         *Wlog;
  RW_DATA_SOURCE       MemSource;

  mBuf = AllocatePages (1);
  if (mBuf == NULL) {
    Check ("tools: test buffer", FALSE);
    return;
  }

  for (Index = 0; Index < SIZE_4KB; Index++) {
    mBuf[Index] = (UINT8)(Index * 7);
  }

  RwWlogClear ();
  RwRunCommand (RwCmdMemory);
  Pump (30);
  Mem  = RwMemViewGet ();
  Grid = Mem->Pane.Grid;
  RwHexGridGoTo (Grid, (UINTN)mBuf);
  RwFocusSet (RwHexGridWidget (Grid));
  Pump (30);

  //
  // Snapshot: Ctrl+N, then a change made behind the grid's back stays red
  // on every later read (the previous-read compare would clear it).
  //
  Key (SCAN_NULL, L'n', CTRL);
  Pump (30);
  Check ("snapshot: Ctrl+N takes a snapshot of the page and saves SNAPnnnn.BIN",
    (BOOLEAN)(RwHexGridHasSnapshot (Grid, &Page) && Page == (UINTN)mBuf && LastIs ("SNAP")));
  AsciiStrCpyS (Number, sizeof (Number), RwToolLastSaved () + 4);
  Number[4] = '\0';
  mBuf[5]  ^= 0xFF;
  RwHexGridRefresh (Grid);
  RwHexGridRefresh (Grid);
  Check ("snapshot: a changed byte stays red against the snapshot", (BOOLEAN)(CellChanged (Grid, 5) && !CellChanged (Grid, 6)));

  //
  // Write log: a write through the grid is logged with its old value.
  //
  RwHexGridWrite (Grid, (UINTN)mBuf + 0x10, 1, 0xA5);
  Pump (30);
  Entry = RwWlogGet (RwWlogCount () - 1);
  Check (
    "write log: a grid write is logged (space, address, old, new, status)",
    (BOOLEAN)(RwWlogCount () == 1 && Entry != NULL && Entry->Address == (UINTN)mBuf + 0x10 && Entry->Width == 1 &&
              Entry->OldValid && Entry->Old == (UINT8)(0x10 * 7) && Entry->New == 0xA5 && !EFI_ERROR (Entry->Status) &&
              AsciiStrStr (Entry->Space, "Memory") != NULL && mBuf[0x10] == 0xA5)
    );
  Check ("snapshot: the grid write shows red too", CellChanged (Grid, 0x10));

  Key (SCAN_NULL, L'n', CTRL);
  Pump (30);
  RwHexGridRefresh (Grid);
  Check ("snapshot: Ctrl+N again clears it; red goes on the next read", (BOOLEAN)(!RwHexGridHasSnapshot (Grid, NULL) && !CellChanged (Grid, 5)));

  //
  // Ctrl+L: load the snapshot file back by number.
  //
  Key (SCAN_NULL, L'l', CTRL);
  Pump (30);
  Check ("snapshot: Ctrl+L asks the snapshot number", RwDialogIsOpen ());
  RwTestType (Number);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (30);
  Check ("snapshot: the loaded snapshot shows the changes again",
    (BOOLEAN)(!RwDialogIsOpen () && RwHexGridHasSnapshot (Grid, NULL) && CellChanged (Grid, 5) && CellChanged (Grid, 0x10) && !CellChanged (Grid, 6)));

  //
  // Save Window on a hex window: start (default: the page), length.
  //
  Key (SCAN_NULL, L's', CTRL);
  Pump (30);
  Check ("save window: Ctrl+S on a hex window asks the start address", RwDialogIsOpen ());
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (30);
  Check ("save window: then the length (default 100h)", (BOOLEAN)(RwDialogIsOpen () && AsciiStrCmp (RwDialogGetText (), "100") == 0));
  RwTestType ("40");
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (30);
  Check ("save window: HEXnnnn.TXT is saved", LastIs ("HEX"));
  AsciiStrCpyS (Base, sizeof (Base), RwToolLastSaved ());
  Base[7] = '\0';
  AsciiStrCatS (Base, sizeof (Base), ".BIN");
  Text = ReadSaved (Base, &Size);
  Check ("save window: HEXnnnn.BIN holds the 40h bytes", (BOOLEAN)(Text != NULL && Size == 0x40 && CompareMem (Text, mBuf, 0x40) == 0));
  if (Text != NULL) {
    FreePool (Text);
  }

  Text = ReadSaved (NULL, &Size);
  Check ("save window: the text has the hex dump", (BOOLEAN)(Text != NULL && AsciiStrStr (Text, " A5 ") != NULL && AsciiStrStr (Text, "Memory") != NULL));
  if (Text != NULL) {
    FreePool (Text);
  }

  //
  // Write Log window follows new writes on its own.
  //
  RwRunCommand (RwCmdWriteLog);
  Pump (30);
  Wlog = RwWlogViewGet ();
  Check ("write log: the window lists the write", (BOOLEAN)(Wlog->Window != NULL && RwTableRowCount (Wlog->Table) == 1));
  RwMemSourceInit (&MemSource);
  RwDsWrite (&MemSource, (UINTN)mBuf + 0x20, 4, 0x11223344);
  Pump (700);
  Check ("write log: a new write appears without F5", (BOOLEAN)(RwTableRowCount (Wlog->Table) == 2 &&
    AsciiStrCmp (RwTableGetCell (Wlog->Table, 1, 6), "11223344") == 0 && AsciiStrCmp (RwTableGetCell (Wlog->Table, 1, 5), "F5EEE7E0") == 0));
  Key (SCAN_NULL, L's', CTRL);
  Pump (30);
  Text = ReadSaved (NULL, &Size);
  Check ("write log: Ctrl+S saves WLOGnnnn.TXT", (BOOLEAN)(LastIs ("WLOG") && Text != NULL && AsciiStrStr (Text, "11223344") != NULL));
  if (Text != NULL) {
    FreePool (Text);
  }
}

STATIC
VOID
TestScreenshotDump (
  VOID
  )
{
  CHAR8                  *Data;
  UINTN                  Size;
  UEFI_GUI_DISPLAY_INFO  Info;
  UINT8                  *Bmp;

  Check ("commands: Screenshot and Dump All are enabled", (BOOLEAN)(RwIsCommandEnabled (RwCmdScreenshot) && RwIsCommandEnabled (RwCmdDumpAll)));
  Pump (50);
  Key (SCAN_F12, 0, 0);
  Pump (30);
  Data = ReadSaved (NULL, &Size);
  Bmp  = (UINT8 *)Data;
  UefiGuiGetDisplayInfo (&Info);
  Check (
    "screenshot: F12 saves a 24-bit BMP of the screen",
    (BOOLEAN)(LastIs ("SCR") && Bmp != NULL && Size > 54 && Bmp[0] == 'B' && Bmp[1] == 'M' &&
              *(UINT32 *)(Bmp + 18) == Info.HorizontalResolution && *(UINT32 *)(Bmp + 22) == Info.VerticalResolution &&
              *(UINT16 *)(Bmp + 28) == 24 && Size == 54 + ((Info.HorizontalResolution * 3 + 3) & ~3U) * Info.VerticalResolution)
    );
  if (Data != NULL) {
    FreePool (Data);
  }

  RwRunCommand (RwCmdDumpAll);
  Pump (30);
  Data = ReadSaved (NULL, &Size);
  Check ("dump all: DUMPnnnn.TXT is saved", (BOOLEAN)(LastIs ("DUMP") && Data != NULL && Size > SIZE_4KB));
  if (Data != NULL) {
    Check ("dump all: PCI, ACPI with ASL, SMBIOS sections",
      (BOOLEAN)(AsciiStrStr (Data, "==== PCI ====") != NULL && AsciiStrStr (Data, "DefinitionBlock") != NULL &&
                AsciiStrStr (Data, "==== SMBIOS ====") != NULL));
    Check ("dump all: USB, storage (the NVMe drive) and EDID sections",
      (BOOLEAN)(AsciiStrStr (Data, "==== USB ====") != NULL && AsciiStrStr (Data, "RWNVME01") != NULL && AsciiStrStr (Data, "==== Monitor EDID ====") != NULL));
    Check ("dump all: MSRs of both CPUs", (BOOLEAN)(AsciiStrStr (Data, "CPU 0") != NULL && AsciiStrStr (Data, "CPU 1") != NULL));
    Check ("dump all: SMBus section (host enabled earlier by the SMBus test)", (BOOLEAN)(AsciiStrStr (Data, "==== SMBus / SPD ====") != NULL));
    FreePool (Data);
  }

  //
  // Generic Save Window: the text of the window's lists.
  //
  RwRunCommand (RwCmdSmbios);
  Pump (30);
  Key (SCAN_NULL, L's', CTRL);
  Pump (30);
  Data = ReadSaved (NULL, &Size);
  Check ("save window: a decoder window saves its lists as SAVEnnnn.TXT",
    (BOOLEAN)(LastIs ("SAVE") && Data != NULL && AsciiStrStr (Data, "SMBIOS") != NULL && AsciiStrStr (Data, "BIOS Information") != NULL));
  if (Data != NULL) {
    FreePool (Data);
  }

  if (RwMdiFind (RwCmdSmbios) != NULL) {
    RwMdiClose (RwMdiFind (RwCmdSmbios));
  }

  Pump (30);
}

STATIC
INTN
FindEntry (
  IN RW_FILE_VIEW  *F,
  IN CONST CHAR8   *Name
  )
{
  UINTN  Row;

  for (Row = 0; Row < RwTableRowCount (F->Entries); Row++) {
    if (AsciiStriCmp (RwTableGetCell (F->Entries, Row, 0), Name) == 0) {
      return (INTN)Row;
    }
  }

  return -1;
}

STATIC
VOID
TestFiles (
  VOID
  )
{
  RW_FILE_VIEW   *F;
  RW_FILE_EDIT   *Edit;
  RW_MDI_WINDOW  *Browser;
  INTN           Row;
  UINT8          Bin[64];
  UINTN          Index;
  CHAR8          *Data;
  UINTN          Size;

  for (Index = 0; Index < sizeof (Bin); Index++) {
    Bin[Index] = (UINT8)Index;
  }

  RwSaveWrite ("RWTEST.TXT", "hello\r\nworld\r\n", 14);
  RwSaveWrite ("RWTEST.BIN", Bin, sizeof (Bin));

  RwRunCommand (RwCmdFiles);
  Pump (50);
  F       = RwFileViewGet ();
  Browser = F->Window;
  Check ("files: the browser opens on the image volume", (BOOLEAN)(Browser != NULL && F->VolCount >= 1 && F->Vol == 0 && F->Vols[0].IsImage));
  Check ("files: the folder lists the application and the test files",
    (BOOLEAN)(FindEntry (F, "UefiShellGuiRW.efi") >= 0 && FindEntry (F, "RWTEST.TXT") >= 0 && FindEntry (F, "RWTEST.BIN") >= 0));

  //
  // A long device path in the last column scrolls into view with Right.
  //
  RwFocusSet (&RwTableList (F->Volumes)->Base);
  Key (SCAN_RIGHT, 0, 0);
  Key (SCAN_RIGHT, 0, 0);
  Pump (30);
  Check ("lists: Right scrolls a long last column into view", (BOOLEAN)(lv_obj_get_scroll_x (RwTableList (F->Volumes)->Body) > 0));
  Key (SCAN_LEFT, 0, 0);
  Key (SCAN_LEFT, 0, 0);
  Pump (30);
  Check ("lists: Left scrolls back", (BOOLEAN)(lv_obj_get_scroll_x (RwTableList (F->Volumes)->Body) == 0));
  RwFocusSet (&RwTableList (F->Entries)->Base);

  //
  // Text editor.
  //
  Row = FindEntry (F, "RWTEST.TXT");
  if (Row >= 0) {
    RwVListSelect (RwTableList (F->Entries), (UINTN)Row);
  }

  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (50);
  Edit = RwFileEditOf (RwMdiGetActive ());
  Check ("editor: Enter opens a text file in the text editor",
    (BOOLEAN)(Edit != NULL && Edit->Text && AsciiStrCmp (RwFileEditGetText (Edit), "hello\nworld\n") == 0 && !Edit->Dirty));
  if (Edit == NULL) {
    return;
  }

  RwTestType ("Hi ");
  Key (SCAN_DOWN, 0, 0);
  Key (SCAN_END, 0, 0);
  RwTestType ("!");
  Pump (30);
  Check ("editor: typing edits the text and marks the window changed",
    (BOOLEAN)(Edit->Dirty && AsciiStrCmp (RwFileEditGetText (Edit), "Hi hello\nworld!\n") == 0 && lv_label_get_text (Edit->Window->Title)[0] == '*'));
  Key (SCAN_F4, 0, CTRL);
  Pump (30);
  Check ("editor: closing a changed file asks first", (BOOLEAN)(RwDialogIsOpen () && RwFileEditOf (RwMdiGetActive ()) == Edit));
  Key (SCAN_ESC, 0, 0);
  Pump (30);
  Check ("editor: No keeps the window open", (BOOLEAN)(!RwDialogIsOpen () && RwFileEditOf (RwMdiGetActive ()) == Edit && Edit->Dirty));
  Key (SCAN_NULL, L's', CTRL);
  Pump (30);
  Check ("editor: Ctrl+S asks before replacing the file", RwDialogIsOpen ());
  Key (SCAN_NULL, L'y', 0);
  Pump (30);
  Data = ReadSaved ("RWTEST.TXT", &Size);
  Check ("editor: the file is saved with CRLF line ends",
    (BOOLEAN)(!Edit->Dirty && Data != NULL && Size == 18 && CompareMem (Data, "Hi hello\r\nworld!\r\n", 18) == 0));
  if (Data != NULL) {
    FreePool (Data);
  }

  Check ("editor: the browser shows the new size", (BOOLEAN)(FindEntry (F, "RWTEST.TXT") >= 0 &&
    AsciiStrCmp (RwTableGetCell (F->Entries, (UINTN)FindEntry (F, "RWTEST.TXT"), 1), "18 bytes") == 0));
  Key (SCAN_F4, 0, CTRL);
  Pump (30);
  Check ("editor: a saved file closes without asking", (BOOLEAN)(!RwDialogIsOpen () && RwFileEditOf (RwMdiGetActive ()) == NULL));

  //
  // Hex editor.
  //
  RwMdiActivate (Browser);
  RwFocusSet (&RwTableList (F->Entries)->Base);
  Row = FindEntry (F, "RWTEST.BIN");
  if (Row >= 0) {
    RwVListSelect (RwTableList (F->Entries), (UINTN)Row);
  }

  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (50);
  Edit = RwFileEditOf (RwMdiGetActive ());
  Check ("editor: a binary file opens in the hex editor", (BOOLEAN)(Edit != NULL && !Edit->Text && Edit->Size == 64 && Edit->Grid != NULL));
  if (Edit == NULL) {
    return;
  }

  RwFocusSet (RwHexGridWidget (Edit->Grid));
  RwTestType ("ff");
  Pump (30);
  Check ("editor: typing in the hex grid changes the buffer only", (BOOLEAN)(Edit->Dirty && Edit->Data[0] == 0xFF));
  Data = ReadSaved ("RWTEST.BIN", &Size);
  Check ("editor: the file is unchanged before Ctrl+S", (BOOLEAN)(Data != NULL && Size == 64 && (UINT8)Data[0] == 0));
  if (Data != NULL) {
    FreePool (Data);
  }

  Key (SCAN_NULL, L's', CTRL);
  Key (SCAN_NULL, L'y', 0);
  Pump (30);
  Data = ReadSaved ("RWTEST.BIN", &Size);
  Check ("editor: Ctrl+S writes the hex edit", (BOOLEAN)(!Edit->Dirty && Data != NULL && Size == 64 && (UINT8)Data[0] == 0xFF && Data[1] == 1));
  if (Data != NULL) {
    FreePool (Data);
  }

  RwMdiClose (Edit->Window);
  Pump (30);

  //
  // Backspace goes up, Enter on a folder goes in (EFI\ on the test ESP if any).
  //
  Check ("files: the root has no \"..\" entry", (BOOLEAN)(RwTableRowCount (F->Entries) > 0 && AsciiStrCmp (RwTableGetCell (F->Entries, 0, 0), "..") != 0));
}

VOID
RwSelfTestTools (
  VOID
  )
{
  RW_FILE_VIEW   *F;
  INTN           Row;
  RW_MDI_WINDOW  *Window;
  RW_MDI_WINDOW  *Other;
  INT32          Desk;
  INT32          Top;

  TestWriteLogAndSnapshot ();
  TestScreenshotDump ();
  TestFiles ();

  //
  // Screenshot layout: File Browser with the text file open, the Write Log,
  // and the memory window showing the snapshot compare on top.
  //
  F   = RwFileViewGet ();
  Row = FindEntry (F, "RWTEST.TXT");
  if ((F->Window != NULL) && (Row >= 0)) {
    RwMdiActivate (F->Window);
    RwVListSelect (RwTableList (F->Entries), (UINTN)Row);
    Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  }

  RwRunCommand (RwCmdCascade);
  Pump (50);
  Window = RwMdiFind (RwCmdMemory);
  if (Window != NULL) {
    RwMdiActivate (Window);
    Pump (30);
    Desk = lv_obj_get_height (lv_obj_get_parent (Window->Obj));
    Top  = lv_obj_get_y (Window->Obj) + lv_obj_get_height (Window->Obj) + 2;
    //
    // The Write Log and the text editor below the memory window.
    //
    Other = RwMdiFind (RwCmdWriteLog);
    if ((Other != NULL) && (Desk - Top > 120)) {
      lv_obj_set_pos (Other->Obj, 0, Top);
      lv_obj_set_size (Other->Obj, 600, Desk - Top);
    }

    Other = RwMdiFind (RwCmdFileEdit);
    if ((Other != NULL) && (Desk - Top > 120)) {
      lv_obj_set_pos (Other->Obj, 604, Top);
      lv_obj_set_size (Other->Obj, lv_obj_get_width (lv_obj_get_parent (Window->Obj)) - 604, Desk - Top);
    }
  }

  Pump (50);
}
