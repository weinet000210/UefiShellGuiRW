/** @file
  Phase 7 self test: CPUID, drive health, UEFI Handles, UEFI System and the
  calculator, driven with keys. Everything here only reads the machine.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools/Tools.h"

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

#define CTRL  UEFI_GUI_KEY_MOD_CTRL

STATIC
BOOLEAN
FirstRowHas (
  IN RW_PROPTABLE  *Table,
  IN CONST CHAR8   *Text
  )
{
  CONST RW_INFO  *Info;

  Info = RwPropTableGetInfo (Table);
  return (BOOLEAN)((Info != NULL) && (Info->Count > 0) && (AsciiStrStr (Info->Rows[0].Name, Text) != NULL));
}

STATIC
INTN
FindInfoRow (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name,
  IN INT32          MinTag
  )
{
  UINTN  Index;

  for (Index = 0; (Info != NULL) && (Index < Info->Count); Index++) {
    if ((AsciiStrStr (Info->Rows[Index].Name, Name) != NULL) && (Info->Rows[Index].Tag >= MinTag)) {
      return (INTN)Index;
    }
  }

  return -1;
}

STATIC
VOID
TestCpuid (
  VOID
  )
{
  RW_CPUID_VIEW  *C;
  UINTN          Bsp;
  UINT32         Leaf;

  RwRunCommand (RwCmdCpuid);
  Pump (30);
  C = RwCpuidViewGet ();
  Check ("cpuid: window lists every leaf", (BOOLEAN)(C->Window != NULL && C->List->Count >= 8 && RwTableRowCount (C->Leaves) == C->List->Count));
  Check ("cpuid: leaf 0 decoded above the processor summary", FirstRowHas (C->Decode, "Leaf 00000000"));
  Key (SCAN_DOWN, 0, 0);
  Check ("cpuid: Down selects leaf 1 and decodes it", (BOOLEAN)(C->Current == 1 && FirstRowHas (C->Decode, "Leaf 00000001")));
  Bsp  = C->CpuIndex;
  Leaf = C->List->Entries[C->Current].Leaf;
  Key (SCAN_RIGHT, 0, CTRL);
  Check ("cpuid: Ctrl+Right shows the next CPU on the same leaf",
    (BOOLEAN)(C->CpuIndex != Bsp && C->Current >= 0 && C->List->Entries[C->Current].Leaf == Leaf &&
              AsciiStrStr (lv_label_get_text (C->Heading), "CPU 1 of 2") != NULL));
  Key (SCAN_LEFT, 0, CTRL);
  Check ("cpuid: Ctrl+Left goes back", (BOOLEAN)(C->CpuIndex == Bsp));
  RwSerialLog ("  cpuid: %d leaves on CPU %d\r\n", (UINT32)C->List->Count, (UINT32)C->CpuIndex);
}

STATIC
VOID
TestStorageHealth (
  VOID
  )
{
  RW_STORAGE_VIEW  *S;
  UINTN            Index;
  INTN             Ata;
  INTN             Ctrl;

  RwRunCommand (RwCmdStorage);
  Pump (30);
  S    = RwStorageViewGet ();
  Ata  = -1;
  Ctrl = -1;
  for (Index = 0; (S->List != NULL) && (Index < S->List->Count); Index++) {
    if ((S->List->Devs[Index].Kind == RwStorAta) && (Ata < 0)) {
      Ata = (INTN)Index;
    } else if ((S->List->Devs[Index].Kind == RwStorNvmeCtrl) && (Ctrl < 0)) {
      Ctrl = (INTN)Index;
    }
  }

  Check ("health: an ATA drive and an NVMe controller", (BOOLEAN)(Ata >= 0 && Ctrl >= 0));
  if ((Ata < 0) || (Ctrl < 0)) {
    return;
  }

  RwFocusSet (&RwTableList (S->Drives)->Base);
  RwVListSelect (RwTableList (S->Drives), (UINTN)Ata);
  Key (SCAN_NULL, L't', CTRL);
  Check ("health: Ctrl+T shows the SMART attributes", (BOOLEAN)(S->ShowHealth && FirstRowHas (S->Decode, "SMART attributes")));
  RwVListSelect (RwTableList (S->Drives), (UINTN)Ctrl);
  Check ("health: the NVMe controller shows its Health log", FirstRowHas (S->Decode, "Health log"));
  Key (SCAN_NULL, L't', CTRL);
  Check ("health: Ctrl+T goes back to identify", (BOOLEAN)(!S->ShowHealth && FirstRowHas (S->Decode, "NVMe controller")));
  RwRunCommand (RwCmdCloseAll);
  Pump (30);
}

STATIC
VOID
TestHandles (
  VOID
  )
{
  RW_HANDLE_VIEW     *H;
  CONST RW_INFO      *Info;
  UINTN              Sel;
  INT32              Tag;

  RwRunCommand (RwCmdHandles);
  Pump (30);
  H = RwHandleViewGet ();
  Check ("handles: database read", (BOOLEAN)(H->Window != NULL && H->Handles > 50 && H->Protocols > 20));
  Check ("handles: by handle, protocols shown, openers folded",
    (BOOLEAN)(RwTreeViewVisibleCount (H->Tree) > H->Handles && RwTreeViewVisibleCount (H->Tree) < RwTreeViewGetInfo (H->Tree)->Count));
  Key (SCAN_NULL, L't', CTRL);
  Check ("handles: Ctrl+T lists the protocols", (BOOLEAN)(H->ByProtocol && RwTreeViewVisibleCount (H->Tree) == H->Protocols));
  Key (SCAN_RIGHT, 0, 0);
  Key (SCAN_DOWN, 0, 0);
  Info = RwTreeViewGetInfo (H->Tree);
  Sel  = RwTreeViewGetSelected (H->Tree);
  Check ("handles: Right opens a protocol, Down reaches its first handle", (BOOLEAN)(Sel < Info->Count && Info->Rows[Sel].Level == 1 && Info->Rows[Sel].Tag > 0));
  Tag = (Sel < Info->Count) ? Info->Rows[Sel].Tag : -1;
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Info = RwTreeViewGetInfo (H->Tree);
  Sel  = RwTreeViewGetSelected (H->Tree);
  Check ("handles: Enter jumps to that handle in the by-handle tree",
    (BOOLEAN)(!H->ByProtocol && Sel < Info->Count && Info->Rows[Sel].Level == 0 && Info->Rows[Sel].Tag == Tag));
  RwSerialLog ("  handles: %d handles, %d protocols\r\n", (UINT32)H->Handles, (UINT32)H->Protocols);
}

STATIC
VOID
TestSystem (
  VOID
  )
{
  RW_SYSTEM_VIEW  *Sys;
  CONST RW_INFO   *Info;
  INTN            Row;
  UINT64          Address;
  RW_MDI_WINDOW   *Mem;

  RwRunCommand (RwCmdSystem);
  Pump (30);
  Sys  = RwSystemViewGet ();
  Info = RwPropTableGetInfo (Sys->Decode);
  Check ("system: system table and configuration tables", (BOOLEAN)(Sys->Window != NULL && FindInfoRow (Info, "Firmware vendor", RW_INFO_NO_TAG) >= 0 && FindInfoRow (Info, "EfiAcpi", RW_SYSTEM_TAG_CONFIG) >= 0));
  Check ("system: graphics output modes", (BOOLEAN)(FindInfoRow (Info, "Current mode", RW_INFO_NO_TAG) >= 0 && FindInfoRow (Info, "Mode 0", RW_INFO_NO_TAG) >= 0));
  Check ("system: TPM PCRs and event log", (BOOLEAN)(FindInfoRow (Info, "Manufacturer", RW_INFO_NO_TAG) >= 0 && FindInfoRow (Info, "Spec ID event", RW_INFO_NO_TAG) >= 0));
  Row = FindInfoRow (Info, "Frame buffer", RW_SYSTEM_TAG_FB);
  Check ("system: frame buffer row", (BOOLEAN)(Row >= 0 && RwSystemTagAddress (Info->Rows[Row].Tag, &Address)));
  if (Row < 0) {
    return;
  }

  RwFocusSet (&RwPropTableList (Sys->Decode)->Base);
  RwVListSelect (RwPropTableList (Sys->Decode), (UINTN)Row);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (30);
  Mem = RwMdiFind (RwCmdMemory);
  Check ("system: Enter opens the frame buffer in the Memory window",
    (BOOLEAN)(Mem != NULL && RwMdiGetActive () == Mem && RwHexGridGetCursor (RwMemViewGet ()->Pane.Grid) == Address));
  if (Mem != NULL) {
    RwMdiClose (Mem);
    Pump (30);
  }
}

STATIC
VOID
TestCalc (
  VOID
  )
{
  RW_CALC_VIEW  *C;
  UINT64        Before;

  Key (SCAN_NULL, L't', UEFI_GUI_KEY_MOD_ALT);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (30);
  C = RwCalcViewGet ();
  Check ("calc: Tools > Calculator opens it with the input focused", (BOOLEAN)(C->Window != NULL && RwMdiGetActive () == C->Window && RwFocusGet (C->Window) == C->Input));
  RwCalcViewSetText ("");
  RwTestType ("10+5");
  Check ("calc: typing evaluates as you go", (BOOLEAN)(C->Valid && C->Value == 0x15));
  RwTestType ("*2");
  Check ("calc: precedence (10+5*2 = 1A)", (BOOLEAN)(C->Valid && C->Value == 0x1A));
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("calc: Enter keeps the value as the new expression", (BOOLEAN)(AsciiStrCmp (lv_textarea_get_text (C->Area), "1A") == 0));
  Check ("calc: value in every base", FirstRowHas (C->Result, "Hex") && (AsciiStrCmp (RwPropTableGetInfo (C->Result)->Rows[1].Value, "26") == 0));
  RwCalcViewSetText ("1/0");
  Check ("calc: division by zero reported", (BOOLEAN)(!C->Valid && AsciiStrStr (lv_label_get_text (C->Message), "division by zero") != NULL));
  RwCalcViewSetText ("100n");
  Before = C->Value;
  RwFocusSet (RwBitFieldWidget (C->Bits));
  Key (SCAN_END, 0, 0);
  Key (SCAN_NULL, L' ', 0);
  Check ("calc: toggling a bit changes the value and the expression",
    (BOOLEAN)(C->Valid && C->Value == (Before ^ 1) && AsciiStrCmp (lv_textarea_get_text (C->Area), "65") == 0));
  RwCalcViewSetText ("DEADBEEF ^ FFFFFFFF");
}

/**
  Put a window at X with Width (-1 = to the right edge), from TopPct to
  BottomPct of the desktop height.
**/
STATIC
VOID
Place (
  IN RW_COMMAND_ID  Id,
  IN INT32          X,
  IN INT32          TopPct,
  IN INT32          Width,
  IN INT32          BottomPct
  )
{
  RW_MDI_WINDOW  *Window;
  INT32          DeskW;
  INT32          DeskH;
  INT32          Top;

  Window = RwMdiFind (Id);
  if (Window == NULL) {
    return;
  }

  DeskW = lv_obj_get_width (lv_obj_get_parent (Window->Obj));
  DeskH = lv_obj_get_height (lv_obj_get_parent (Window->Obj));
  Top   = DeskH * TopPct / 100;
  lv_obj_set_pos (Window->Obj, X, Top);
  lv_obj_set_size (Window->Obj, (Width < 0) ? DeskW - X : Width, DeskH * BottomPct / 100 - Top - 2);
}

VOID
RwSelfTestInfo (
  VOID
  )
{
  TestCpuid ();
  RwRunCommand (RwCmdCloseAll);
  Pump (30);
  TestStorageHealth ();
  TestHandles ();
  TestSystem ();
  TestCalc ();

  //
  // Screenshot layout: CPUID and the calculator on the left, UEFI System and
  // UEFI Handles on the right.
  //
  RwRunCommand (RwCmdCpuid);
  Pump (30);
  Place (RwCmdCpuid, 0, 0, 590, 42);
  Place (RwCmdCalc, 0, 42, 590, 100);
  Place (RwCmdSystem, 592, 0, -1, 50);
  Place (RwCmdHandles, 592, 50, -1, 100);
  RwMdiActivate (RwMdiFind (RwCmdCpuid));
  Pump (50);
}
