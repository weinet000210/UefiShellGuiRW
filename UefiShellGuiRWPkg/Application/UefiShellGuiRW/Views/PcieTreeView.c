/** @file
  PCIe Tree window: the bridge hierarchy of every root bridge on the left,
  the selected function decoded on the right (header, BARs, bridge windows,
  capabilities, extended capabilities, link state). Enter on a device opens
  its config space in the PCI Devices window; Enter on a decoded field opens
  it at that register.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define TREE_WIDTH    360
#define DECODE_WIDTH  470

STATIC RW_PCIE_TREE_VIEW  mTree;

STATIC
VOID
ShowDecode (
  IN INT32  Index
  )
{
  RW_INFO  Info;
  CHAR8    Text[RW_DS_NAME_MAX];

  if ((Index < 0) || ((UINTN)Index >= mTree.List.DeviceCount) || (Index == mTree.Current)) {
    return;
  }

  mTree.Current = Index;
  RwPciDescribe (&mTree.List.Devices[Index], Text, sizeof (Text));
  lv_label_set_text (mTree.Heading, Text);
  RwInfoInit (&Info);
  RwPciDecode (&mTree.List.Devices[Index], &Info);
  RwPropTableSetInfo (mTree.Decode, &Info);
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
  ShowDecode (Row->Tag);
}

STATIC
VOID
OpenInPci (
  IN INT32   Index,
  IN UINT32  Offset
  )
{
  RW_PCI_DEVICE  *D;

  if ((Index < 0) || ((UINTN)Index >= mTree.List.DeviceCount)) {
    return;
  }

  D = &mTree.List.Devices[Index];
  RwPciViewShow (D->Segment, D->Bus, D->Dev, D->Func, Offset);
}

STATIC
VOID
OnTreeActivate (
  IN VOID               *View,
  IN CONST RW_INFO_ROW  *Row,
  IN UINTN              Index,
  IN VOID               *Context
  )
{
  OpenInPci (Row->Tag, 0);
}

STATIC
VOID
OnDecodeActivate (
  IN VOID               *View,
  IN CONST RW_INFO_ROW  *Row,
  IN UINTN              Index,
  IN VOID               *Context
  )
{
  if (Row->Offset != RW_INFO_NO_OFFSET) {
    OpenInPci (mTree.Current, Row->Offset);
  }
}

STATIC
VOID
Rebuild (
  VOID
  )
{
  RW_INFO  Info;

  RwPciFree (&mTree.List);
  RwPciScan (&mTree.List);
  mTree.Current = -1;
  RwInfoInit (&Info);
  RwPciDecodeTree (&mTree.List, &Info);
  RwTreeViewSetInfo (mTree.Tree, &Info, 16);
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  INT32  Keep;
  RW_INFO  Info;

  //
  // Re-read the decode of the same function (link state, status bits).
  //
  Keep          = mTree.Current;
  mTree.Current = -1;
  if (Keep >= 0) {
    ShowDecode (Keep);
  } else {
    RwInfoInit (&Info);
    RwPropTableSetInfo (mTree.Decode, &Info);
  }
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwPciFree (&mTree.List);
  ZeroMem (&mTree, sizeof (mTree));
}

RW_MDI_WINDOW *
RwViewOpenPcieTree (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Left;
  lv_obj_t       *Right;
  UINTN          Index;

  if (mTree.Window != NULL) {
    RwMdiActivate (mTree.Window);
    return mTree.Window;
  }

  Window = RwMdiCreate ("PCIe Tree", RwCmdPcieTree, TREE_WIDTH + DECODE_WIDTH + 20, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mTree, sizeof (mTree));
  mTree.Window      = Window;
  mTree.Current     = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, TREE_WIDTH);
  RwViewLabel (Left, "Bridges and devices (Enter: config space)");
  mTree.Tree = RwTreeViewCreate (RwViewPane (Left, 1), Window);
  RwTreeViewSetCallbacks (mTree.Tree, OnTreeSelect, OnTreeActivate, NULL);

  Right         = RwViewColumn (Window->Content, 0);
  mTree.Heading = RwViewLabel (Right, "");
  mTree.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);
  RwPropTableSetCallbacks (mTree.Decode, NULL, OnDecodeActivate, NULL);

  Rebuild ();

  //
  // Start on the first device row (row 0 is the root bridge section).
  //
  for (Index = 0; Index < mTree.List.DeviceCount; Index++) {
    if (RwTreeViewSelectTag (mTree.Tree, (INT32)Index)) {
      break;
    }
  }

  RwMdiFitContent (Window, TREE_WIDTH + 4 + DECODE_WIDTH, 540);
  RwFocusSet (&RwTreeViewList (mTree.Tree)->Base);
  return Window;
}

RW_PCIE_TREE_VIEW *
RwPcieTreeViewGet (
  VOID
  )
{
  return &mTree;
}
