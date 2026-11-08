/** @file
  Monitor EDID window: every EDID the firmware publishes (EDID Active /
  Discovered on the graphics outputs, else the QEMU standard VGA EDID
  window) on the left, the decoded EDID with its raw bytes on the right.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  330

STATIC RW_EDID_VIEW  mEdid;

STATIC
VOID
Show (
  IN INT32  Index
  )
{
  RW_INFO  Info;
  RW_EDID  *E;
  CHAR8    Text[128];

  if ((Index < 0) || ((UINTN)Index >= mEdid.List->Count) || (Index == mEdid.Current)) {
    return;
  }

  mEdid.Current = Index;
  E             = &mEdid.List->Displays[Index];
  AsciiSPrint (Text, sizeof (Text), "%a   %d bytes   %a", E->Name, E->Size, E->Source);
  lv_label_set_text (mEdid.Heading, Text);
  RwInfoInit (&Info);
  RwEdidDecode (E->Data, E->Size, &Info);
  RwPropTableSetInfo (mEdid.Decode, &Info);
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
  Show (Tag);
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
  RwFocusSet (&RwPropTableList (mEdid.Decode)->Base);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mEdid.List != NULL) {
    FreePool (mEdid.List);
  }

  ZeroMem (&mEdid, sizeof (mEdid));
}

RW_MDI_WINDOW *
RwViewOpenEdid (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  UINTN                         Index;
  CONST CHAR8                   *Cells[2];
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Monitor", 140 }, { "Source", 0 } };

  if (mEdid.Window != NULL) {
    RwMdiActivate (mEdid.Window);
    return mEdid.Window;
  }

  Window = RwMdiCreate ("Monitor EDID", RwCmdEdid, 900, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mEdid, sizeof (mEdid));
  mEdid.List = AllocateZeroPool (sizeof (RW_EDID_LIST));
  if (mEdid.List == NULL) {
    RwMdiClose (Window);
    return NULL;
  }

  mEdid.Window    = Window;
  mEdid.Current   = -1;
  Window->OnClose = OnClose;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  RwEdidScan (mEdid.List);
  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, (mEdid.List->Count > 0) ? "Displays" : "No EDID published");
  mEdid.Displays = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mEdid.Displays, OnSelect, OnActivate, NULL);
  for (Index = 0; Index < mEdid.List->Count; Index++) {
    Cells[0] = mEdid.List->Displays[Index].Name;
    Cells[1] = mEdid.List->Displays[Index].Source;
    RwTableAddRow (mEdid.Displays, Cells, (INT32)Index, RwEmNormal);
  }

  Right         = RwViewColumn (Window->Content, 0);
  mEdid.Heading = RwViewLabel (Right, "");
  mEdid.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);
  if (mEdid.List->Count > 0) {
    RwVListSelect (RwTableList (mEdid.Displays), 0);
  }

  RwMdiFitContent (Window, LIST_WIDTH + 4 + 520, 520);
  RwFocusSet (&RwTableList (mEdid.Displays)->Base);
  return Window;
}

RW_EDID_VIEW *
RwEdidViewGet (
  VOID
  )
{
  return &mEdid;
}
