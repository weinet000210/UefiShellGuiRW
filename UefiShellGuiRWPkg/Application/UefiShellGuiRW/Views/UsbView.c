/** @file
  USB Devices window: controller -> port -> device tree on the left (hubs
  show their ports, new devices are marked until the hold time runs out,
  removed ones show on their port), the selected controller or device
  decoded on the right. F5 rescans.

  Only devices the firmware's USB bus driver enumerated are seen, and only
  standard read requests are sent (GET_DESCRIPTOR, hub GET_STATUS).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define TREE_WIDTH    420
#define DECODE_WIDTH  450

STATIC RW_USB_VIEW  mUsb;

STATIC
VOID
Show (
  IN INT32    Tag,
  IN BOOLEAN  Force
  )
{
  RW_INFO  Info;
  CHAR8    Text[160];
  UINTN    Hc;
  UINTN    Dev;

  if ((Tag < 0) || ((Tag == mUsb.CurrentTag) && !Force)) {
    return;
  }

  RwInfoInit (&Info);
  if (Tag >= RW_USB_HC_TAG_BASE) {
    Hc = (UINTN)(Tag - RW_USB_HC_TAG_BASE);
    if (Hc >= mUsb.Scan.HcCount) {
      return;
    }

    AsciiSPrint (Text, sizeof (Text), "%a   %a", mUsb.Scan.Hcs[Hc].Name, mUsb.Scan.Hcs[Hc].Pci);
    RwUsbDecodeController (&mUsb.Scan, Hc, &Info);
  } else {
    Dev = (UINTN)Tag;
    if (Dev >= mUsb.Scan.DevCount) {
      return;
    }

    AsciiSPrint (Text, sizeof (Text), "[%04X:%04X]  %a", mUsb.Scan.Devs[Dev].Vid, mUsb.Scan.Devs[Dev].Pid, mUsb.Scan.Devs[Dev].Product);
    RwUsbDecodeDevice (&mUsb.Scan, Dev, &Info);
  }

  mUsb.CurrentTag = Tag;
  lv_label_set_text (mUsb.Heading, Text);
  RwPropTableSetInfo (mUsb.Decode, &Info);
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
  Show (Row->Tag, FALSE);
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
  if (Row->Tag >= 0) {
    RwFocusSet (&RwPropTableList (mUsb.Decode)->Base);
  }
}

VOID
RwUsbViewRescan (
  VOID
  )
{
  RW_USB_TREE  Tree;
  RW_INFO      Info;
  RW_INFO      Empty;
  INT32        Keep;

  if (mUsb.Window == NULL) {
    return;
  }

  Keep = mUsb.CurrentTag;
  RwUsbScanFree (&mUsb.Scan);
  RwUsbScan (&mUsb.Scan);
  RwUsbHotplugUpdate (&mUsb.Hotplug, &mUsb.Scan, RwUsbNowSeconds (), RW_USB_HOTPLUG_SECONDS);

  RwInfoInit (&Info);
  ZeroMem (&Tree, sizeof (Tree));
  if (!EFI_ERROR (RwUsbBuildTree (&mUsb.Scan, &mUsb.Hotplug, &Tree))) {
    RwUsbTreeToInfo (&mUsb.Scan, &Tree, &Info);
    RwUsbTreeFree (&Tree);
  }

  if (Info.Count == 0) {
    RwInfoText (&Info, 0, RwEmDim, "No USB host controllers.");
  }

  //
  // Setting the tree selects its first row (the first controller); then
  // go back to what was shown before the rescan when it is still there.
  //
  mUsb.CurrentTag = -1;
  RwTreeViewSetInfo (mUsb.Tree, &Info, 16);
  if (Keep >= 0) {
    RwTreeViewSelectTag (mUsb.Tree, Keep);
  }

  if (mUsb.CurrentTag < 0) {
    lv_label_set_text (mUsb.Heading, "");
    RwInfoInit (&Empty);
    RwPropTableSetInfo (mUsb.Decode, &Empty);
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwUsbViewRescan ();
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwUsbScanFree (&mUsb.Scan);
  RwUsbHotplugFree (&mUsb.Hotplug);
  ZeroMem (&mUsb, sizeof (mUsb));
}

RW_MDI_WINDOW *
RwViewOpenUsb (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Left;
  lv_obj_t       *Right;

  if (mUsb.Window != NULL) {
    RwMdiActivate (mUsb.Window);
    return mUsb.Window;
  }

  Window = RwMdiCreate ("USB Devices", RwCmdUsb, TREE_WIDTH + DECODE_WIDTH + 20, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mUsb, sizeof (mUsb));
  mUsb.Window       = Window;
  mUsb.CurrentTag   = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);
  RwUsbHotplugInit (&mUsb.Hotplug);

  Left = RwViewColumn (Window->Content, TREE_WIDTH);
  RwViewLabel (Left, "Controllers, ports and devices (F5: rescan)");
  mUsb.Tree = RwTreeViewCreate (RwViewPane (Left, 1), Window);
  RwTreeViewSetCallbacks (mUsb.Tree, OnTreeSelect, OnTreeActivate, NULL);

  Right        = RwViewColumn (Window->Content, 0);
  mUsb.Heading = RwViewLabel (Right, "");
  mUsb.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);

  RwUsbViewRescan ();
  RwMdiFitContent (Window, TREE_WIDTH + 4 + DECODE_WIDTH, 540);
  RwFocusSet (&RwTreeViewList (mUsb.Tree)->Base);
  return Window;
}

RW_USB_VIEW *
RwUsbViewGet (
  VOID
  )
{
  return &mUsb;
}
