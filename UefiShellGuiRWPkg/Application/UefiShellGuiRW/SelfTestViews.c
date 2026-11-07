/** @file
  Phase 3 self test: opens the PCI, Memory, I/O, Index/Data, CMOS and MSR
  windows through their commands and drives them with keys. Writes only
  touch memory the test allocated, spare CMOS bytes (38h, 39h) and IA32_PAT
  with its own value; dangerous writes are opened and then cancelled.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views/Views.h"

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

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
CellIsByte (
  IN RW_HEXGRID  *Grid,
  IN UINTN       Offset,
  IN UINT8       Value
  )
{
  CHAR8  Text[4];

  AsciiSPrint (Text, sizeof (Text), "%02X", Value);
  return CellIs (Grid, Offset, Text);
}

STATIC
VOID
CtrlKey (
  IN CHAR16  Letter
  )
{
  Key (SCAN_NULL, Letter, UEFI_GUI_KEY_MOD_CTRL);
}

STATIC
VOID
Enter (
  VOID
  )
{
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
}

STATIC
BOOLEAN
Focused (
  IN RW_MDI_WINDOW  *Window,
  IN RW_WIDGET      *Widget
  )
{
  return (BOOLEAN)(RwFocusGet (Window) == Widget);
}

STATIC
VOID
TestPci (
  VOID
  )
{
  RW_PCI_VIEW  *V;
  RW_HEXGRID   *Grid;
  UINT8        Bar;

  RwRunCommand (RwCmdPci);
  Pump (50);
  V = RwPciViewGet ();
  Check ("pci: command opens the PCI Devices window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Grid = V->Pane.Grid;
  Check ("pci: one table row per function", (BOOLEAN)(V->List.DeviceCount > 1 && RwTableRowCount (V->Devices) == V->List.DeviceCount));
  Check ("pci: the device list has the focus", Focused (V->Window, &RwTableList (V->Devices)->Base));
  Check ("pci: grid shows the first function's vendor ID", CellIsByte (Grid, 0, (UINT8)V->List.Devices[0].VendorId));
  Check ("pci: heading names the function", (BOOLEAN)(AsciiStrStr (lv_label_get_text (V->Pane.Heading), "00:00.0") != NULL));

  Key (SCAN_DOWN, 0, 0);
  Pump (30);
  Check ("pci: Down selects the next function", (BOOLEAN)(V->Current == 1));
  Check ("pci: grid follows the selection", CellIsByte (Grid, 2, (UINT8)V->List.Devices[1].DeviceId));
  Check (
    "pci: config space is 4 KB exactly when extended config is reachable",
    (BOOLEAN)(V->Source.Limit == (V->List.Devices[1].ExtConfig ? 0xFFF : 0xFF))
    );

  Enter ();
  Pump (30);
  Check ("pci: Enter moves the focus to the grid", Focused (V->Window, RwHexGridWidget (Grid)));

  //
  // BAR0 is dangerous: typing a value asks, cancelling leaves it alone.
  //
  RwHexGridSetWidth (Grid, 1);
  RwHexGridGoTo (Grid, 0x10);
  Bar = RwPciRd (&V->List.Devices[1], 0x10, 1) & 0xFF;
  RwTestType ("FF");
  Pump (30);
  Check ("pci: a BAR write asks first", (BOOLEAN)(RwDialogIsOpen () && ((UINT8)RwPciRd (&V->List.Devices[1], 0x10, 1) == Bar)));
  RwDialogCancel ();
  Pump (30);
  Check ("pci: cancelled BAR write leaves the BAR", (BOOLEAN)(!RwDialogIsOpen () && ((UINT8)RwPciRd (&V->List.Devices[1], 0x10, 1) == Bar)));

  Key (SCAN_NULL, CHAR_TAB, UEFI_GUI_KEY_MOD_SHIFT);    // grid -> list
  Key (SCAN_HOME, 0, 0);
  Pump (30);
  Check ("pci: Shift+Tab and Home return to the first function", (BOOLEAN)(V->Current == 0));
}

STATIC
VOID
TestMemory (
  VOID
  )
{
  RW_MEM_VIEW  *V;
  RW_HEXGRID   *Grid;
  UINT8        *Buffer;
  UINTN        Index;
  UINTN        Row;
  BOOLEAN      HaveRam;
  CHAR8        Hex[20];

  RwRunCommand (RwCmdMemory);
  Pump (50);
  V = RwMemViewGet ();
  Check ("memory: command opens the Memory window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Grid    = V->Pane.Grid;
  HaveRam = FALSE;
  Row     = 0;
  for (Index = 0; Index < RwTableRowCount (V->Map); Index++) {
    if ((AsciiStrCmp (RwTableGetCell (V->Map, Index, 2), "Conventional") == 0) && (V->Ranges[Index].Start >= SIZE_1MB)) {
      HaveRam = TRUE;
      Row     = Index;
      break;
    }
  }

  Check ("memory: the map lists conventional memory above 1 MB", (BOOLEAN)(RwTableRowCount (V->Map) == V->RangeCount && HaveRam));
  Check ("memory: the grid has the focus, at address 0", (BOOLEAN)(Focused (V->Window, RwHexGridWidget (Grid)) && RwHexGridGetPage (Grid) == 0));

  //
  // Map row -> grid.
  //
  RwFocusSet (&RwTableList (V->Map)->Base);
  RwVListSelect (RwTableList (V->Map), Row);
  Enter ();
  Pump (30);
  Check ("memory: Enter on a map row jumps there", (BOOLEAN)(RwHexGridGetPage (Grid) == (V->Ranges[Row].Start & ~(UINT64)0xFF) && Focused (V->Window, RwHexGridWidget (Grid))));

  //
  // A page of our own: read, Ctrl+G, typed writes.
  //
  Buffer = AllocatePages (1);
  if (Buffer == NULL) {
    Check ("memory: test page allocated", FALSE);
    return;
  }

  for (Index = 0; Index < 256; Index++) {
    Buffer[Index] = (UINT8)(Index ^ 0x5C);
  }

  CtrlKey ('g');
  Pump (30);
  AsciiSPrint (Hex, sizeof (Hex), "%lX", (UINT64)(UINTN)Buffer);
  RwTestType (Hex);
  Enter ();
  Pump (50);
  Check ("memory: Ctrl+G goes to the test page", (BOOLEAN)(!RwDialogIsOpen () && RwHexGridGetPage (Grid) == (UINT64)(UINTN)Buffer));
  RwHexGridSetWidth (Grid, 1);
  Check ("memory: the grid shows the test page", (BOOLEAN)(CellIsByte (Grid, 0, 0x5C) && CellIsByte (Grid, 0x41, 0x41 ^ 0x5C)));
  RwHexGridGoTo (Grid, (UINT64)(UINTN)Buffer + 4);
  RwTestType ("A5");
  Pump (30);
  Check ("memory: typing writes memory", (BOOLEAN)(!RwDialogIsOpen () && Buffer[4] == 0xA5 && CellIs (Grid, 4, "A5")));
  CtrlKey ('d');
  RwHexGridGoTo (Grid, (UINT64)(UINTN)Buffer + 8);
  RwTestType ("12345678");
  Pump (30);
  Check ("memory: dword write", (BOOLEAN)(*(UINT32 *)(Buffer + 8) == 0x12345678 && CellIs (Grid, 8, "12345678")));
  Buffer[0x20] = 0xEE;
  RwHexGridRefresh (Grid);
  AsciiSPrint (Hex, sizeof (Hex), "%08X", *(UINT32 *)(Buffer + 0x20));
  {
    CHAR8    Cell[24];
    BOOLEAN  Changed;

    RwHexGridGetCellText (Grid, 0x20, Cell, sizeof (Cell), &Changed);
    Check ("memory: refresh shows an outside change in red", (BOOLEAN)(AsciiStrCmp (Cell, Hex) == 0 && Changed));
  }

  //
  // Leave the grid somewhere harmless before the page goes away.
  //
  RwHexGridGoTo (Grid, 0);
  CtrlKey ('b');
  FreePages (Buffer, 1);
}

STATIC
VOID
TestIo (
  VOID
  )
{
  RW_IO_VIEW  *V;
  RW_HEXGRID  *Grid;
  CHAR8       Cell[24];

  RwRunCommand (RwCmdIo);
  Pump (50);
  V = RwIoViewGet ();
  Check ("io: command opens the I/O Space window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Grid = V->Pane.Grid;
  Check ("io: 64 K ports, no auto refresh (reads have side effects)", (BOOLEAN)(V->Source.Limit == 0xFFFF && !RwHexGridGetAutoRefresh (Grid)));
  RwHexGridGetCellText (Grid, 0x64, Cell, sizeof (Cell), NULL);
  Check ("io: port 64h reads", (BOOLEAN)(AsciiStrLen (Cell) == 2 && AsciiStrCmp (Cell, "--") != 0));

  RwHexGridGoTo (Grid, 0xCF9);
  Check ("io: go to CF9h", (BOOLEAN)(RwHexGridGetPage (Grid) == 0xC00 && RwHexGridGetCursor (Grid) == 0xCF9));
  RwTestType ("06");
  Pump (30);
  Check ("io: a CF9h write asks first", RwDialogIsOpen ());
  RwDialogCancel ();
  Pump (30);
  Check ("io: cancelled, the machine is still here", (BOOLEAN)(!RwDialogIsOpen () && !RwHexGridIsEditing (Grid)));
  RwHexGridGoTo (Grid, 0);
}

STATIC
VOID
TestIndexData (
  VOID
  )
{
  RW_INDEX_DATA_VIEW  *V;
  RW_HEXGRID          *Grid;
  UINT8               Saved;

  RwRunCommand (RwCmdIndexData);
  Pump (50);
  V = RwIndexDataViewGet ();
  Check ("index/data: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Grid = V->Pane.Grid;
  Check ("index/data: starts on 70h/71h", (BOOLEAN)(V->IndexPort == 0x70 && V->DataPort == 0x71 && AsciiStrStr (V->Source.Name, "0070h/0071h") != NULL));
  Check ("index/data: 70h/71h register D matches CMOS", CellIsByte (Grid, 0x0D, RwCmosRead8 (0x0D)));
  Key (SCAN_DOWN, 0, 0);
  Pump (30);
  Check ("index/data: Down picks 72h/73h", (BOOLEAN)(V->IndexPort == 0x72 && V->DataPort == 0x73));

  Key (SCAN_END, 0, 0);
  Enter ();
  Pump (30);
  Check ("index/data: Custom asks for the index port", (BOOLEAN)(RwDialogIsOpen () && RwDialogGetText () != NULL));
  RwTestType ("70");
  Enter ();
  Pump (30);
  Check ("index/data: then the data port, index + 1 proposed", (BOOLEAN)(RwDialogIsOpen () && AsciiStrCmp (RwDialogGetText (), "71") == 0));
  Enter ();
  Pump (30);
  Check (
    "index/data: custom 70h/71h selects the matching row",
    (BOOLEAN)(!RwDialogIsOpen () && V->IndexPort == 0x70 && V->DataPort == 0x71 && RwTableList (V->Pairs)->Selected == 0)
    );

  Saved = RwCmosRead8 (0x38);
  RwFocusSet (RwHexGridWidget (Grid));
  RwHexGridGoTo (Grid, 0x38);
  RwTestType ("5A");
  Pump (30);
  Check ("index/data: typing writes through the pair", (BOOLEAN)(RwCmosRead8 (0x38) == 0x5A && CellIs (Grid, 0x38, "5A")));
  RwCmosWrite8 (0x38, Saved);
}

STATIC
VOID
TestCmos (
  VOID
  )
{
  RW_CMOS_VIEW       *V;
  RW_HEXGRID         *Grid;
  CONST RW_INFO      *Info;
  UINT8              Saved;

  RwRunCommand (RwCmdCmos);
  Pump (50);
  V = RwCmosViewGet ();
  Check ("cmos: command opens the CMOS window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Grid = V->Pane.Grid;
  Info = RwPropTableGetInfo (V->Rtc);
  Check ("cmos: RTC decode lists time and date", (BOOLEAN)(Info->Count > 8 && AsciiStrCmp (Info->Rows[1].Name, "Time") == 0 && AsciiStrCmp (Info->Rows[2].Name, "Date") == 0));
  Check ("cmos: register D cell matches", CellIsByte (Grid, 0x0D, RwCmosRead8 (0x0D)));
  Check ("cmos: byte access only", (BOOLEAN)(RwHexGridGetWidth (Grid) == 1));
  CtrlKey ('w');
  Check ("cmos: Ctrl+W keeps byte width", (BOOLEAN)(RwHexGridGetWidth (Grid) == 1));

  Saved = RwCmosRead8 (0x39);
  RwHexGridGoTo (Grid, 0x39);
  RwTestType ("3C");
  Pump (30);
  Check ("cmos: typing writes CMOS", (BOOLEAN)(RwCmosRead8 (0x39) == 0x3C && CellIs (Grid, 0x39, "3C")));
  RwCmosWrite8 (0x39, Saved);
  Key (SCAN_F5, 0, 0);
  Pump (30);
  Check ("cmos: F5 re-reads grid and decode", (BOOLEAN)(CellIsByte (Grid, 0x39, Saved) && RwPropTableGetInfo (V->Rtc)->Count > 8));
}

STATIC
VOID
TestMsr (
  VOID
  )
{
  RW_MSR_VIEW  *V;
  UINTN        Row;
  UINTN        Count;
  UINT64       Pat;
  UINT64       Apic;
  BOOLEAN      Found;
  CHAR8        Hex[20];

  RwRunCommand (RwCmdMsr);
  Pump (50);
  V = RwMsrViewGet ();
  Check ("msr: command opens the CPU MSR window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  RwMsrKnownList (&Count);
  Check ("msr: #GP guard installed, known MSRs listed", (BOOLEAN)(V->Guard && V->Count == Count && RwTableRowCount (V->Table) == Count));
  Found = FALSE;
  for (Row = 0; Row < V->Count; Row++) {
    if (V->Entries[Row].Index == 0x1B) {
      Found = (BOOLEAN)(V->Entries[Row].Valid && (V->Entries[Row].Value & BIT11) != 0);
      Apic  = V->Entries[Row].Value;
      AsciiSPrint (Hex, sizeof (Hex), "%016lX", Apic);
      Found = (BOOLEAN)(Found && AsciiStrCmp (RwTableGetCell (V->Table, Row, 2), Hex) == 0);
    }
  }

  Check ("msr: IA32_APIC_BASE row shows the enabled APIC", Found);
  Check ("msr: bit view shows the selected MSR (64 bits)", (BOOLEAN)(RwBitFieldGetValue (V->Bits) == V->Entries[0].Value));

  //
  // Ins adds an MSR that is not listed (IA32_VMX_BASIC, 480h); asking for
  // a listed one (IA32_PAT) selects its row. Enter + value + Y writes
  // IA32_PAT back unchanged.
  //
  Key (SCAN_INSERT, 0, 0);
  Pump (30);
  Check ("msr: Ins asks for an MSR index", RwDialogIsOpen ());
  RwTestType ("480");
  Enter ();
  Pump (30);
  Row = V->Count - 1;
  Check (
    "msr: a new MSR is appended and selected",
    (BOOLEAN)(!RwDialogIsOpen () && V->Count == Count + 1 && V->Entries[Row].Index == 0x480 && RwTableList (V->Table)->Selected == Row)
    );
  Key (SCAN_INSERT, 0, 0);
  Pump (30);
  RwTestType ("277");
  Enter ();
  Pump (30);
  Row = RwTableList (V->Table)->Selected;
  Check (
    "msr: a listed MSR is selected, not added twice",
    (BOOLEAN)(!RwDialogIsOpen () && V->Count == Count + 1 && Row < V->Count && V->Entries[Row].Index == 0x277 && V->Entries[Row].Valid)
    );
  Pat = V->Entries[Row].Value;

  Enter ();
  Pump (30);
  Check ("msr: Enter asks for the new value", (BOOLEAN)(RwDialogIsOpen () && RwDialogGetText () != NULL));
  AsciiSPrint (Hex, sizeof (Hex), "%lX", Pat);
  RwTestType (Hex);
  Enter ();
  Pump (30);
  Check ("msr: every MSR write asks first", RwDialogIsOpen ());
  Enter ();
  Pump (30);
  Check ("msr: Enter takes the default No", (BOOLEAN)(!RwDialogIsOpen ()));

  RwMsrViewWrite (0x277, Pat);
  Pump (30);
  Key (SCAN_NULL, L'y', 0);
  Pump (30);
  Check ("msr: Y writes IA32_PAT, read back unchanged", (BOOLEAN)(!RwDialogIsOpen () && V->Entries[Row].Valid && V->Entries[Row].Value == Pat));

  Key (SCAN_F5, 0, 0);
  Pump (30);
  Check ("msr: F5 re-reads every MSR", (BOOLEAN)(V->Count == Count + 1 && V->Entries[Row].Value == Pat));
  RwVListSelect (RwTableList (V->Table), 0);
}

VOID
RwSelfTestViews (
  VOID
  )
{
  RW_MDI_WINDOW  *Pci;

  TestPci ();
  TestMemory ();
  TestIo ();
  TestIndexData ();
  TestCmos ();
  TestMsr ();

  Check ("views: six module windows open", (BOOLEAN)(RwMdiCount () == 6));

  //
  // Screenshot layout: cascade, PCI Devices on top.
  //
  RwRunCommand (RwCmdCascade);
  Pump (50);
  Check ("views: cascade keeps the PCI window wide enough for its grid", (BOOLEAN)(RwMdiFind (RwCmdPci) != NULL && lv_obj_get_width (RwMdiFind (RwCmdPci)->Obj) == RwMdiFind (RwCmdPci)->PrefWidth));
  Pci = RwMdiFind (RwCmdPci);
  if (Pci != NULL) {
    RwMdiActivate (Pci);
  }

  Pump (50);
}
