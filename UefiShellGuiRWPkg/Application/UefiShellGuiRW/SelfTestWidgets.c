/** @file
  Widget self test: drives the hex grid, bit field view, table, tree,
  property table, text view and dialogs of the widget test window with
  keys, the way a user would, and checks the results.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views/WidgetTest.h"

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

STATIC BOOLEAN  mDone;
STATIC BOOLEAN  mAccepted;

STATIC
VOID
DialogDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  mDone     = TRUE;
  mAccepted = Accepted;
}

STATIC
BOOLEAN
CellIs (
  IN RW_HEXGRID   *Grid,
  IN UINTN        Offset,
  IN CONST CHAR8  *Text
  )
{
  CHAR8  Cell[24];

  RwHexGridGetCellText (Grid, Offset, Cell, sizeof (Cell), NULL);
  if (AsciiStrCmp (Cell, Text) != 0) {
    RwSerialLog ("    cell %02X is \"%a\", expected \"%a\"\r\n", (UINT32)Offset, Cell, Text);
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
CellChanged (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset
  )
{
  CHAR8    Cell[24];
  BOOLEAN  Changed;

  RwHexGridGetCellText (Grid, Offset, Cell, sizeof (Cell), &Changed);
  return Changed;
}

STATIC
VOID
CtrlKey (
  IN CHAR16  Letter
  )
{
  Key (SCAN_NULL, Letter, UEFI_GUI_KEY_MOD_CTRL);
}

VOID
RwSelfTestWidgets (
  VOID
  )
{
  RW_WIDGET_TEST  *T;
  RW_HEXGRID      *Grid;
  UINT8           *B;
  UINTN           Visible;
  CHAR8           Cell[24];
  lv_area_t       Area = { 0 };
  CONST RW_INFO   *Info;

  RwViewOpenWidgetTest ();
  Pump (100);
  T    = RwWidgetTestGet ();
  Grid = T->Grid;
  B    = T->Bytes;
  Check ("widgets: test window opens with every widget",
    (BOOLEAN)(T->Window != NULL && Grid != NULL && T->Bits != NULL && T->Table != NULL && T->Tree != NULL &&
              T->Prop != NULL && T->Text != NULL && RwMdiGetActive () == T->Window));
  Check ("focus: the hex grid has the keyboard", (BOOLEAN)(RwFocusGet (T->Window) == RwHexGridWidget (Grid)));
  Check ("focus: six widgets in the window's focus chain", (BOOLEAN)(T->Window->FocusCount == 6));

  //
  // Hex grid: display.
  //
  Check ("hexgrid: page 0 in bytes, 00 and 07 at offsets 0 and 1",
    (BOOLEAN)(RwHexGridGetPage (Grid) == 0 && RwHexGridGetWidth (Grid) == 1 && CellIs (Grid, 0, "00") && CellIs (Grid, 1, "07")));
  Check ("hexgrid: an unreadable byte shows --", CellIs (Grid, RW_WIDGET_TEST_HOLE, "--"));
  lv_obj_update_layout (T->Window->Obj);
  lv_obj_get_coords (RwHexGridWidget (Grid)->Obj, &Area);
  Check ("hexgrid: the header row is not a cell", (BOOLEAN)(RwHexGridHitTest (Grid, Area.x1 + 8, Area.y1 + 6) == -1));
  Check ("hexgrid: the grid fits 1024x768 (under 700 px wide)", (BOOLEAN)(lv_area_get_width (&Area) < 700));

  //
  // Cursor and the bit field view that follows it.
  //
  Key (SCAN_RIGHT, 0, 0);
  Key (SCAN_DOWN, 0, 0);
  Check ("hexgrid: Right, Down moves the cursor to 11h", (BOOLEAN)(RwHexGridGetCursor (Grid) == 0x11));
  Check ("bitfield: follows the cursor (value 78h)", (BOOLEAN)(RwBitFieldGetValue (T->Bits) == 0x78));

  //
  // A dangerous write asks first; No is the default.
  //
  RwTestType ("ab");
  Check ("hexgrid: typing into a dangerous byte asks first", (BOOLEAN)(RwDialogIsOpen () && B[0x11] == 0x78 && T->Writes == 0));
  Key (SCAN_F4, 0, UEFI_GUI_KEY_MOD_CTRL);
  Check ("dialog: is modal (Ctrl+F4 does not close the window)", (BOOLEAN)(RwDialogIsOpen () && RwMdiCount () >= 1 && RwWidgetTestGet ()->Window != NULL));
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("dialog: Enter takes the default No, nothing written", (BOOLEAN)(!RwDialogIsOpen () && B[0x11] == 0x78 && T->Writes == 0));
  RwTestType ("ab");
  Key (SCAN_NULL, L'y', 0);
  Check ("dialog: Y writes the dangerous byte", (BOOLEAN)(!RwDialogIsOpen () && B[0x11] == 0xAB && T->Writes == 1));
  Check ("hexgrid: the written byte reads back and shows changed", (BOOLEAN)(CellIs (Grid, 0x11, "AB") && CellChanged (Grid, 0x11)));

  //
  // Go to, normal write with auto advance, cancelled edit.
  //
  CtrlKey (L'g');
  Check ("hexgrid: Ctrl+G asks for an address, starting at the cursor",
    (BOOLEAN)(RwDialogIsOpen () && (RwDialogGetText () != NULL) && AsciiStrCmp (RwDialogGetText (), "11") == 0));
  RwTestType ("20");
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("hexgrid: go to 20h", (BOOLEAN)(!RwDialogIsOpen () && RwHexGridGetCursor (Grid) == 0x20));
  RwTestType ("5A");
  Check ("hexgrid: two digits write a byte at once and move on", (BOOLEAN)(B[0x20] == 0x5A && T->Writes == 2 && RwHexGridGetCursor (Grid) == 0x21));
  RwTestType ("1");
  Check ("hexgrid: one digit starts an edit (1_)", (BOOLEAN)(RwHexGridIsEditing (Grid) && CellIs (Grid, 0x21, "1_")));
  Key (SCAN_ESC, 0, 0);
  Check ("hexgrid: Esc cancels the edit", (BOOLEAN)(!RwHexGridIsEditing (Grid) && T->Writes == 2));

  //
  // Word and dword views.
  //
  CtrlKey (L'w');
  Check ("hexgrid: Ctrl+W shows words, cursor on the word start",
    (BOOLEAN)(RwHexGridGetWidth (Grid) == 2 && RwHexGridGetCursor (Grid) == 0x20 && CellIs (Grid, 0x20, "E95A")));
  RwTestType ("1234");
  Check ("hexgrid: four digits write a word, little endian", (BOOLEAN)(B[0x20] == 0x34 && B[0x21] == 0x12 && RwHexGridGetCursor (Grid) == 0x22));
  CtrlKey (L'd');
  RwHexGridGetCellText (Grid, RW_WIDGET_TEST_HOLE, Cell, sizeof (Cell), NULL);
  Check ("hexgrid: Ctrl+D shows dwords; the unreadable byte stays --, its neighbours read",
    (BOOLEAN)(RwHexGridGetWidth (Grid) == 4 && AsciiStrLen (Cell) == 8 && Cell[6] == '-' && Cell[7] == '-' && Cell[0] != '-'));
  CtrlKey (L'b');
  Check ("hexgrid: Ctrl+B back to bytes", (BOOLEAN)(RwHexGridGetWidth (Grid) == 1));

  //
  // Pages.
  //
  Key (SCAN_PAGE_DOWN, 0, 0);
  Check ("hexgrid: PgDn shows page 100h", (BOOLEAN)(RwHexGridGetPage (Grid) == 0x100));
  Key (SCAN_PAGE_DOWN, 0, 0);
  Check ("hexgrid: PgDn stops at the end of the space", (BOOLEAN)(RwHexGridGetPage (Grid) == 0x100));
  Key (SCAN_PAGE_UP, 0, 0);
  Check ("hexgrid: PgUp back to page 0", (BOOLEAN)(RwHexGridGetPage (Grid) == 0));
  CtrlKey (L'g');
  RwTestType ("FFFF");
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("hexgrid: go to an address outside the space is refused", (BOOLEAN)(RwHexGridGetPage (Grid) == 0));

  //
  // Refresh: a change behind the grid's back turns red.
  //
  B[0x05] ^= 0xFF;
  Check ("command: Refresh is enabled for a window that can refresh", RwIsCommandEnabled (RwCmdRefresh));
  RwRunCommand (RwCmdRefresh);
  Pump (30);
  Check ("hexgrid: Refresh marks the changed byte, not its neighbours", (BOOLEAN)(CellChanged (Grid, 5) && !CellChanged (Grid, 4)));

  //
  // Bit field view edits through the grid.
  //
  RwHexGridGoTo (Grid, 0x08);
  Key (SCAN_NULL, CHAR_TAB, 0);
  Check ("focus: Tab moves to the bit field view", (BOOLEAN)(RwFocusGet (T->Window) == RwBitFieldWidget (T->Bits)));
  Key (SCAN_NULL, L' ', 0);
  Check ("bitfield: Space toggles bit 0 and writes it", (BOOLEAN)(B[0x08] == 0x39 && RwBitFieldGetValue (T->Bits) == 0x39));
  Key (SCAN_LEFT, 0, 0);
  Key (SCAN_NULL, L' ', 0);
  Check ("bitfield: Left, Space toggles bit 1", (BOOLEAN)(RwBitFieldGetCursor (T->Bits) == 1 && B[0x08] == 0x3B));

  //
  // Table.
  //
  Key (SCAN_NULL, CHAR_TAB, 0);
  Check ("focus: Tab moves to the table", (BOOLEAN)(RwFocusGet (T->Window) == &RwTableList (T->Table)->Base));
  Check ("table: one row per PCI device", (BOOLEAN)(RwTableRowCount (T->Table) >= 4 && AsciiStrCmp (RwTableGetCell (T->Table, 0, 1), "8086:29C0") == 0));
  Key (SCAN_DOWN, 0, 0);
  Key (SCAN_DOWN, 0, 0);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("table: Down, Down, Enter activates row 1", (BOOLEAN)(T->TableActivated == 1));
  Key (SCAN_END, 0, 0);
  Check ("table: End selects the last row", (BOOLEAN)(RwTableList (T->Table)->Selected == RwTableRowCount (T->Table) - 1));
  {
    CONST CHAR8  *Wide[] = { "00:1F.7 (a device name far wider than its column)", "1234:5678", "Class" };
    INT32        Before;

    Before = RwTableList (T->Table)->Columns[0].Width;
    RwTableSetRow (T->Table, 0, Wide, 0);
    Check ("table: a fixed column widens to its longest text", (BOOLEAN)(RwTableList (T->Table)->Columns[0].Width > Before));
    Check ("table: the header follows the wider column", (BOOLEAN)(RwVListColumnX (RwTableList (T->Table), 2) > RwTableList (T->Table)->BaseWidth[0] + RwTableList (T->Table)->BaseWidth[1]));
  }


  //
  // Tree.
  //
  Key (SCAN_NULL, CHAR_TAB, 0);
  Visible = RwTreeViewVisibleCount (T->Tree);
  Check ("tree: PCI tree with the root bridge expanded", (BOOLEAN)(Visible >= 5 && RwTreeViewGetSelected (T->Tree) == 0));
  Key (SCAN_LEFT, 0, 0);
  Check ("tree: Left collapses the root bridge", (BOOLEAN)(RwTreeViewVisibleCount (T->Tree) == 1));
  Key (SCAN_RIGHT, 0, 0);
  Check ("tree: Right expands it again", (BOOLEAN)(RwTreeViewVisibleCount (T->Tree) == Visible));
  Key (SCAN_DOWN, 0, 0);
  Check ("tree: Down selects the first device and reports it", (BOOLEAN)(RwTreeViewGetSelected (T->Tree) == 1 && T->TreeSelects >= 2));
  Check ("tree: select by tag", (BOOLEAN)(RwTreeViewSelectTag (T->Tree, 2) && RwTreeViewGetSelected (T->Tree) != RW_VLIST_NONE));

  //
  // Property table and text view.
  //
  Info = RwPropTableGetInfo (T->Prop);
  Check ("proptable: host bridge decode, starting with Header",
    (BOOLEAN)(Info->Count > 8 && AsciiStrCmp (Info->Rows[0].Name, "Header") == 0 && RwPropTableList (T->Prop)->RowCount == Info->Count));
  Key (SCAN_NULL, CHAR_TAB, 0);
  Key (SCAN_PAGE_DOWN, 0, 0);
  Check ("proptable: PgDn moves the selection a page", (BOOLEAN)(RwPropTableList (T->Prop)->Selected >= 3));
  Check ("textview: the same decode as text", (BOOLEAN)(RwTextViewLineCount (T->Text) == Info->Count));
  Key (SCAN_NULL, CHAR_TAB, UEFI_GUI_KEY_MOD_SHIFT);
  Check ("focus: Shift+Tab goes back", (BOOLEAN)(RwFocusGet (T->Window) == &RwTreeViewList (T->Tree)->Base));

  //
  // Dialogs.
  //
  mDone = FALSE;
  RwDialogMessage ("Message", "A message box.", DialogDone, NULL);
  Pump (30);
  Check ("dialog: message box opens", RwDialogIsOpen ());
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("dialog: Enter closes it with OK", (BOOLEAN)(!RwDialogIsOpen () && mDone && mAccepted));
  mDone = FALSE;
  RwDialogInputHex ("Input", "Value:", 0x1234, 8, DialogDone, NULL);
  RwTestType ("5");
  Check ("dialog: typing replaces the initial value", (BOOLEAN)(AsciiStrCmp (RwDialogGetText (), "5") == 0));
  Key (SCAN_NULL, CHAR_BACKSPACE, 0);
  Check ("dialog: Backspace deletes a digit", (BOOLEAN)(AsciiStrCmp (RwDialogGetText (), "") == 0));
  Key (SCAN_ESC, 0, 0);
  Check ("dialog: Esc cancels", (BOOLEAN)(!RwDialogIsOpen () && mDone && !mAccepted));

  //
  // Leave the window up on real data for the screenshot: the host bridge's
  // config space, cursor on the device ID.
  //
  RwFocusSet (RwHexGridWidget (Grid));
  if (T->HaveHostBridge) {
    RwHexGridSetSource (Grid, &T->HostBridge);
    RwHexGridGoTo (Grid, 0x02);
    CtrlKey (L'w');
  }

  Check ("hexgrid: switches to the host bridge's config space", (BOOLEAN)(!T->HaveHostBridge || CellIs (Grid, 0x02, "29C0")));
}
