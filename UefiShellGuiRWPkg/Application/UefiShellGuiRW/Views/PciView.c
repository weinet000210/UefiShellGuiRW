/** @file
  PCI Devices window: every function on the left, the selected function's
  configuration space (256 bytes, or 4 KB when the extended space is
  reachable) as a hex grid with its bit field view on the right.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  300

STATIC RW_PCI_VIEW  mPci;

STATIC
VOID
ShowDevice (
  IN UINTN  Index
  )
{
  if (Index >= mPci.List.DeviceCount) {
    return;
  }

  mPci.Current = Index;
  RwPciSourceInit (&mPci.Source, &mPci.List.Devices[Index]);
  RwSpacePaneSetSource (&mPci.Pane, &mPci.Source);
}

STATIC
VOID
OnSelect (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  if ((Tag >= 0) && ((UINTN)Tag != mPci.Current)) {
    ShowDevice ((UINTN)Tag);
  }
}

STATIC
VOID
OnActivate (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  RwFocusSet (RwHexGridWidget (mPci.Pane.Grid));
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mPci.Pane.Grid);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwPciFree (&mPci.List);
  ZeroMem (&mPci, sizeof (mPci));
}

RW_MDI_WINDOW *
RwViewOpenPci (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  UINTN                         Index;
  CHAR8                         Bdf[16];
  CHAR8                         Id[16];
  CONST CHAR8                   *Cells[3];
  RW_PCI_DEVICE                 *Dev;
  INT32                         PaneW;
  INT32                         PaneH;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Device", 72 }, { "ID", 82 }, { "Class", 0 } };

  if (mPci.Window != NULL) {
    RwMdiActivate (mPci.Window);
    return mPci.Window;
  }

  Window = RwMdiCreate ("PCI Devices", RwCmdPci, 960, 520);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mPci, sizeof (mPci));
  mPci.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwPciScan (&mPci.List);
  mPci.Devices = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mPci.Devices, OnSelect, OnActivate, NULL);
  for (Index = 0; Index < mPci.List.DeviceCount; Index++) {
    Dev = &mPci.List.Devices[Index];
    if (mPci.List.RootCount > 1) {
      AsciiSPrint (Bdf, sizeof (Bdf), "%X:%02X:%02X.%X", Dev->Segment, Dev->Bus, Dev->Dev, Dev->Func);
    } else {
      AsciiSPrint (Bdf, sizeof (Bdf), "%02X:%02X.%X", Dev->Bus, Dev->Dev, Dev->Func);
    }

    AsciiSPrint (Id, sizeof (Id), "%04X:%04X", Dev->VendorId, Dev->DeviceId);
    Cells[0] = Bdf;
    Cells[1] = Id;
    Cells[2] = RwPciClassName (Dev->BaseClass, Dev->SubClass);
    RwTableAddRow (mPci.Devices, Cells, (INT32)Index, RwPciIsBridge (Dev) ? RwEmStrong : RwEmNormal);
  }

  if (mPci.List.DeviceCount == 0) {
    //
    // No PCI at all: an empty read-only space keeps the pane meaningful.
    //
    AsciiStrCpyS (mPci.Source.Name, sizeof (mPci.Source.Name), "No PCI devices");
    mPci.Source.Limit      = 0xFF;
    mPci.Source.AddrDigits = 2;
    mPci.Source.WidthMask  = RW_DS_WIDTH_8;
  } else {
    RwPciSourceInit (&mPci.Source, &mPci.List.Devices[0]);
  }

  RwSpacePaneCreate (&mPci.Pane, Window->Content, Window, &mPci.Source);
  RwSpacePaneSize (&mPci.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, LIST_WIDTH + 4 + PaneW, PaneH + 60);
  if (mPci.List.DeviceCount > 0) {
    RwVListSelect (RwTableList (mPci.Devices), 0);
  }

  RwFocusSet (&RwTableList (mPci.Devices)->Base);
  return Window;
}

BOOLEAN
RwPciViewShow (
  IN UINT32  Segment,
  IN UINT8   Bus,
  IN UINT8   Dev,
  IN UINT8   Func,
  IN UINT32  Offset
  )
{
  UINTN          Index;
  RW_PCI_DEVICE  *D;

  if (mPci.Window == NULL) {
    if (RwViewOpenPci () == NULL) {
      return FALSE;
    }
  } else {
    if (mPci.Window->Minimized) {
      RwMdiSetMinimized (mPci.Window, FALSE);
    }

    RwMdiActivate (mPci.Window);
  }

  for (Index = 0; Index < mPci.List.DeviceCount; Index++) {
    D = &mPci.List.Devices[Index];
    if ((D->Segment == Segment) && (D->Bus == Bus) && (D->Dev == Dev) && (D->Func == Func)) {
      RwVListSelect (RwTableList (mPci.Devices), Index);
      RwVListScrollTo (RwTableList (mPci.Devices), Index);
      if (mPci.Current != Index) {
        ShowDevice (Index);
      }

      if (Offset <= mPci.Source.Limit) {
        RwHexGridGoTo (mPci.Pane.Grid, Offset);
      }

      RwFocusSet (RwHexGridWidget (mPci.Pane.Grid));
      return TRUE;
    }
  }

  return FALSE;
}

RW_PCI_VIEW *
RwPciViewGet (
  VOID
  )
{
  return &mPci;
}
