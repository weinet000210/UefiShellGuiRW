/** @file
  Widget test window, opened by the self test only: a hex grid over a test
  pattern (with a hole and a dangerous range), its bit field view, a table,
  and a tree, property table and text view filled from RwCoreLib (the PCI
  bridge tree and the host bridge's decoded config space).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../Widgets/RwWidgets.h"
#include "WidgetTest.h"

#define PATTERN_SIZE  0x200

STATIC RW_WIDGET_TEST  mTest;
STATIC UINT8           mPattern[PATTERN_SIZE];

//
// ---- Test pattern source: 512 bytes, 30h unreadable, 10h-13h dangerous ----
//

STATIC
EFI_STATUS
EFIAPI
PatternRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  if ((Address <= RW_WIDGET_TEST_HOLE) && (Address + Width > RW_WIDGET_TEST_HOLE)) {
    return EFI_DEVICE_ERROR;
  }

  *Value = 0;
  CopyMem (Value, &mPattern[Address], Width);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PatternWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  CopyMem (&mPattern[Address], &Value, Width);
  mTest.Writes++;
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
PatternDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  return (BOOLEAN)((Address <= 0x13) && (Address + Width > 0x10));
}

STATIC
VOID
InitPattern (
  VOID
  )
{
  UINTN        Index;
  CONST CHAR8  *Banner;

  for (Index = 0; Index < PATTERN_SIZE; Index++) {
    mPattern[Index] = (UINT8)(Index * 7 + (Index >> 4));
  }

  Banner = "UefiShellGuiRW  RW-Everything style hardware tool  ";
  CopyMem (&mPattern[0x40], Banner, AsciiStrLen (Banner));

  ZeroMem (&mTest.Pattern, sizeof (mTest.Pattern));
  AsciiStrCpyS (mTest.Pattern.Name, sizeof (mTest.Pattern.Name), "Test Pattern");
  mTest.Pattern.Base        = 0;
  mTest.Pattern.Limit       = PATTERN_SIZE - 1;
  mTest.Pattern.AddrDigits  = 4;
  mTest.Pattern.WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32;
  mTest.Pattern.SideEffects = TRUE;    // no auto refresh: the test controls reads
  mTest.Pattern.Read        = PatternRead;
  mTest.Pattern.Write       = PatternWrite;
  mTest.Pattern.IsDangerous = PatternDangerous;
  mTest.Bytes               = mPattern;
}

//
// ---- Companion wiring ------------------------------------------------------
//

STATIC
VOID
OnGridCursor (
  IN RW_HEXGRID  *Grid,
  IN UINT64      Address,
  IN UINTN       Width,
  IN UINT64      Value,
  IN BOOLEAN     Valid,
  IN VOID        *Context
  )
{
  CHAR8  Text[80];

  if (mTest.Bits != NULL) {
    RwBitFieldSetValue (mTest.Bits, Value, Width * 8, Valid);
  }

  if (Valid) {
    AsciiSPrint (Text, sizeof (Text), "%lXh = %0*lXh", Address, Width * 2, Value);
  } else {
    AsciiSPrint (Text, sizeof (Text), "%lXh: cannot be read", Address);
  }

  RwStatusSetText (Text);
}

STATIC
VOID
OnBitsChange (
  IN RW_BITFIELD  *View,
  IN UINT64       NewValue,
  IN VOID         *Context
  )
{
  RwHexGridWrite (mTest.Grid, RwHexGridGetCursor (mTest.Grid), RwHexGridGetWidth (mTest.Grid), NewValue);
}

STATIC
VOID
OnTableActivate (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  mTest.TableActivated = Tag;
}

STATIC
VOID
OnTreeSelect (
  IN VOID               *View,
  IN CONST RW_INFO_ROW  *Row,
  IN UINTN              Index,
  IN VOID               *Context
  )
{
  mTest.TreeSelects++;
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mTest.Grid);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mTest, sizeof (mTest));
}

STATIC
lv_obj_t *
Column (
  IN lv_obj_t  *Parent,
  IN INT32     Width
  )
{
  lv_obj_t  *Obj;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_height (Obj, LV_PCT (100));
  if (Width > 0) {
    lv_obj_set_width (Obj, Width);
  } else {
    lv_obj_set_flex_grow (Obj, 1);
  }

  lv_obj_set_flex_flow (Obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row (Obj, 4, 0);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Obj;
}

STATIC
lv_obj_t *
Pane (
  IN lv_obj_t  *Parent,
  IN INT32     Grow
  )
{
  lv_obj_t  *Obj;

  Obj = lv_obj_create (Parent);
  lv_obj_remove_style_all (Obj);
  lv_obj_set_width (Obj, LV_PCT (100));
  lv_obj_set_flex_grow (Obj, (uint8_t)Grow);
  lv_obj_set_scrollable (Obj, FALSE);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Obj;
}

RW_MDI_WINDOW *
RwViewOpenWidgetTest (
  VOID
  )
{
  RW_MDI_WINDOW                  *Window;
  lv_obj_t                       *Left;
  lv_obj_t                       *Right;
  RW_PCI_LIST                    Pci;
  RW_INFO                        Info;
  UINTN                          Index;
  CHAR8                          Bdf[16];
  CHAR8                          Id[16];
  CONST CHAR8                    *Cells[3];
  RW_STR                         Text;
  STATIC CONST RW_VLIST_COLUMN   Columns[] = { { "Device", 80 }, { "ID", 90 }, { "Class", 0 } };

  if (mTest.Window != NULL) {
    RwMdiActivate (mTest.Window);
    return mTest.Window;
  }

  InitPattern ();
  Window = RwMdiCreate ("Widget Test", RwCmdNone, 1010, 640);
  if (Window == NULL) {
    return NULL;
  }

  mTest.Window       = Window;
  Window->OnClose    = OnClose;
  Window->OnRefresh  = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  //
  // Left: hex grid, bit field view, table.
  //
  Left       = Column (Window->Content, 0);
  mTest.Grid = RwHexGridCreate (Left, Window, &mTest.Pattern);
  mTest.Bits = RwBitFieldCreate (Left, Window);
  RwBitFieldSetCallback (mTest.Bits, OnBitsChange, NULL);
  RwHexGridSetCallbacks (mTest.Grid, OnGridCursor, NULL, NULL);
  mTest.Table = RwTableCreate (Pane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mTest.Table, NULL, OnTableActivate, NULL);

  //
  // Right: PCI tree, host bridge decode, the decode as text.
  //
  Right      = Column (Window->Content, 330);
  mTest.Tree = RwTreeViewCreate (Pane (Right, 2), Window);
  mTest.Prop = RwPropTableCreate (Pane (Right, 3), Window, TRUE);
  mTest.Text = RwTextViewCreate (Pane (Right, 2), Window);
  RwTreeViewSetCallbacks (mTest.Tree, OnTreeSelect, NULL, NULL);

  if (!EFI_ERROR (RwPciScan (&Pci)) || (Pci.DeviceCount > 0)) {
    for (Index = 0; Index < Pci.DeviceCount; Index++) {
      AsciiSPrint (Bdf, sizeof (Bdf), "%02X:%02X.%X", Pci.Devices[Index].Bus, Pci.Devices[Index].Dev, Pci.Devices[Index].Func);
      AsciiSPrint (Id, sizeof (Id), "%04X:%04X", Pci.Devices[Index].VendorId, Pci.Devices[Index].DeviceId);
      Cells[0] = Bdf;
      Cells[1] = Id;
      Cells[2] = RwPciClassName (Pci.Devices[Index].BaseClass, Pci.Devices[Index].SubClass);
      RwTableAddRow (mTest.Table, Cells, (INT32)Index, RwEmNormal);
    }

    RwInfoInit (&Info);
    RwPciDecodeTree (&Pci, &Info);
    RwTreeViewSetInfo (mTest.Tree, &Info, 8);

    if (Pci.DeviceCount > 0) {
      RwInfoInit (&Info);
      RwPciDecode (&Pci.Devices[0], &Info);
      RwStrInit (&Text);
      RwInfoToText (&Info, &Text, FALSE);
      RwPropTableSetInfo (mTest.Prop, &Info);
      RwTextViewSetText (mTest.Text, RwStrDetach (&Text));
      RwPciSourceInit (&mTest.HostBridge, &Pci.Devices[0]);
      mTest.HaveHostBridge = TRUE;
    }

    RwPciFree (&Pci);
  }

  RwFocusSet (RwHexGridWidget (mTest.Grid));
  return Window;
}

RW_WIDGET_TEST *
RwWidgetTestGet (
  VOID
  )
{
  return &mTest;
}
