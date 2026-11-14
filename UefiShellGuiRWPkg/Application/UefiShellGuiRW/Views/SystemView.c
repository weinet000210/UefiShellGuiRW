/** @file
  UEFI System window: system table, configuration tables, graphics output
  modes and the TPM (PCR values, TCG2 event log). Enter on a configuration
  table or a frame buffer opens the Memory window there. No graphics mode
  is set and the only TPM command sent is TPM2_PCR_Read.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

STATIC RW_SYSTEM_VIEW  mSys;

STATIC
VOID
Fill (
  VOID
  )
{
  RW_INFO  Info;

  RwInfoInit (&Info);
  RwSystemDecode (&Info);
  RwPropTableSetInfo (mSys.Decode, &Info);
}

STATIC
VOID
OnActivate (
  IN VOID               *View,
  IN CONST RW_INFO_ROW  *Row,
  IN UINTN              Index,
  IN VOID               *Context
  )
{
  UINT64  Address;
  CHAR8   Text[64];

  if ((Row->Tag < 0) || !RwSystemTagAddress (Row->Tag, &Address)) {
    return;
  }

  if (RwViewOpenMemory () != NULL) {
    RwHexGridGoTo (RwMemViewGet ()->Pane.Grid, Address);
    RwFocusSet (RwHexGridWidget (RwMemViewGet ()->Pane.Grid));
    AsciiSPrint (Text, sizeof (Text), "Memory at %016lX.", Address);
    RwStatusSetText (Text);
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  Fill ();
  RwStatusSetText ("System information read again.");
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mSys, sizeof (mSys));
}

RW_MDI_WINDOW *
RwViewOpenSystem (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Column;

  if (mSys.Window != NULL) {
    RwMdiActivate (mSys.Window);
    return mSys.Window;
  }

  Window = RwMdiCreate ("UEFI System", RwCmdSystem, 860, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mSys, sizeof (mSys));
  mSys.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);

  Column       = RwViewColumn (Window->Content, 0);
  mSys.Heading = RwViewLabel (Column, "System table, configuration tables, graphics output, TPM   (Enter on an address: memory, F5: read again)");
  mSys.Decode  = RwPropTableCreate (RwViewPane (Column, 1), Window, FALSE);
  RwPropTableSetCallbacks (mSys.Decode, NULL, OnActivate, NULL);
  Fill ();
  RwFocusSet (&RwPropTableList (mSys.Decode)->Base);
  return Window;
}

RW_SYSTEM_VIEW *
RwSystemViewGet (
  VOID
  )
{
  return &mSys;
}
