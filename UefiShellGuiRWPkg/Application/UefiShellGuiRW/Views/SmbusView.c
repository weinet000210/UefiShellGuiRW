/** @file
  SMBus / SPD window: the devices that answer on the chipset SMBus (with a
  hint of what usually sits at each address and, for SPD EEPROMs, the
  module summary) on the left; on the right the SPD decode and a hex grid.

  The grid shows the SPD as read (all pages, read-only copy) for SPD
  EEPROMs and the device's 256 registers otherwise; Ctrl+T switches an SPD
  device to its raw registers. Every SMBus write asks first. When the
  firmware left the host controller off, E turns it on (after asking).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH    340
#define DECODE_HEIGHT 190

STATIC RW_SMBUS_VIEW  mSmb;

STATIC
VOID
UpdateHeading (
  VOID
  )
{
  CHAR8  Text[160];

  if (mSmb.BusStatus == EFI_NOT_FOUND) {
    AsciiStrCpyS (Text, sizeof (Text), "No supported SMBus host controller (Intel ICH/PCH or AMD SB/FCH).");
  } else if (mSmb.BusStatus == EFI_NOT_READY) {
    AsciiSPrint (Text, sizeof (Text), "%a\nDisabled by firmware: E to enable", mSmb.Bus.Name);
  } else {
    AsciiSPrint (Text, sizeof (Text), "%a\n%d device(s), Ctrl+T: SPD copy / raw registers", mSmb.Bus.Name, mSmb.Count);
  }

  lv_label_set_text (mSmb.Heading, Text);
}

STATIC
VOID
Show (
  IN INT32    Addr,
  IN BOOLEAN  Force
  )
{
  RW_INFO  Info;
  BOOLEAN  IsSpd;

  if ((Addr < 0) || ((Addr == mSmb.Current) && !Force)) {
    return;
  }

  mSmb.Current = Addr;
  IsSpd        = (BOOLEAN)((Addr >= 0x50) && (Addr <= 0x57));
  RwInfoInit (&Info);
  if (IsSpd && !EFI_ERROR (RwSpdRead (&mSmb.Bus, (UINT8)Addr, mSmb.Spd))) {
    RwSpdDecode (mSmb.Spd, &Info);
  } else {
    IsSpd = FALSE;
    RwInfoSection (&Info, 0, "SMBus device %02Xh", Addr);
    RwInfoField (&Info, 1, RwEmNormal, "Usually", "%a", (RwSmbusAddrHint ((UINT8)Addr)[0] != '\0') ? RwSmbusAddrHint ((UINT8)Addr) : "nothing typical");
    RwInfoText (&Info, 1, RwEmDim, "Registers are read with Read Byte Data; writes ask first.");
  }

  RwPropTableSetInfo (mSmb.Decode, &Info);
  if (IsSpd && mSmb.ShowSpd) {
    RwSpdSourceInit (&mSmb.Source, mSmb.Spd);
  } else {
    RwSmbusSourceInit (&mSmb.Source, &mSmb.Bus, (UINT8)Addr);
  }

  RwSpacePaneSetSource (&mSmb.Pane, &mSmb.Source);
}

STATIC
VOID
Rescan (
  VOID
  )
{
  UINTN        Addr;
  CHAR8        AddrText[8];
  CHAR8        What[96];
  CONST CHAR8  *Cells[2];
  INT32        First;

  RwTableClear (mSmb.Devices);
  mSmb.Count   = 0;
  mSmb.Current = -1;
  First        = -1;
  if (mSmb.BusStatus == EFI_SUCCESS) {
    mSmb.Count = RwSmbusScan (&mSmb.Bus, mSmb.Present);
    for (Addr = RW_SMBUS_FIRST_ADDR; Addr <= RW_SMBUS_LAST_ADDR; Addr++) {
      if (!mSmb.Present[Addr]) {
        continue;
      }

      AsciiSPrint (AddrText, sizeof (AddrText), "%02Xh", Addr);
      if ((Addr >= 0x50) && (Addr <= 0x57) && !EFI_ERROR (RwSpdRead (&mSmb.Bus, (UINT8)Addr, mSmb.Spd))) {
        RwSpdSummary (mSmb.Spd, What, sizeof (What));
      } else {
        AsciiStrCpyS (What, sizeof (What), RwSmbusAddrHint ((UINT8)Addr));
      }

      Cells[0] = AddrText;
      Cells[1] = What;
      RwTableAddRow (mSmb.Devices, Cells, (INT32)Addr, ((Addr >= 0x50) && (Addr <= 0x57)) ? RwEmStrong : RwEmNormal);
      if (First < 0) {
        First = (INT32)Addr;
      }
    }
  }

  UpdateHeading ();
  if (First >= 0) {
    RwVListSelect (RwTableList (mSmb.Devices), 0);
  }
}

STATIC
VOID
OnEnable (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (!Accepted || (mSmb.Window == NULL)) {
    return;
  }

  mSmb.BusStatus = RwSmbusEnable (&mSmb.Bus);
  RwStatusSetText (EFI_ERROR (mSmb.BusStatus) ? "The SMBus host controller stays unusable." : "SMBus host controller enabled.");
  Rescan ();
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
  Show (Tag, FALSE);
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
  RwFocusSet (RwHexGridWidget (mSmb.Pane.Grid));
}

STATIC
BOOLEAN
WindowKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if ((RwKeyIsChar (Key, L'e') || RwKeyIsChar (Key, L'E')) && (mSmb.BusStatus == EFI_NOT_READY)) {
    RwDialogConfirm (
      "Enable SMBus",
      "The firmware left the SMBus host controller off.\n\nTurn on its I/O decode and host enable bit?",
      RW_DIALOG_WARNING,
      OnEnable,
      NULL
      );
    return TRUE;
  }

  if (RwKeyIsCtrl (Key, 't') && (mSmb.Current >= 0)) {
    mSmb.ShowSpd = (BOOLEAN)!mSmb.ShowSpd;
    Show (mSmb.Current, TRUE);
    RwStatusSetText (mSmb.ShowSpd ? "Grid: SPD copy (all pages)." : "Grid: raw SMBus registers (writes ask first).");
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mSmb.BusStatus == EFI_SUCCESS) {
    Show (mSmb.Current, TRUE);
  }
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mSmb.Spd != NULL) {
    FreePool (mSmb.Spd);
  }

  ZeroMem (&mSmb, sizeof (mSmb));
}

RW_MDI_WINDOW *
RwViewOpenSmbus (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  lv_obj_t                      *DecodePane;
  INT32                         PaneW;
  INT32                         PaneH;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Addr", 52 }, { "Device", 0 } };

  if (mSmb.Window != NULL) {
    RwMdiActivate (mSmb.Window);
    return mSmb.Window;
  }

  Window = RwMdiCreate ("SMBus / SPD", RwCmdSmbus, 960, 600);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mSmb, sizeof (mSmb));
  mSmb.Spd = AllocateZeroPool (sizeof (RW_SPD));
  if (mSmb.Spd == NULL) {
    RwMdiClose (Window);
    return NULL;
  }

  mSmb.Window       = Window;
  mSmb.Current      = -1;
  mSmb.ShowSpd      = TRUE;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);
  mSmb.BusStatus = RwSmbusFind (&mSmb.Bus);

  Left         = RwViewColumn (Window->Content, LIST_WIDTH);
  mSmb.Heading = RwViewLabel (Left, "");
  mSmb.Devices = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mSmb.Devices, OnSelect, OnActivate, NULL);
  RwTableList (mSmb.Devices)->ExtraKey = WindowKey;

  Right      = RwViewColumn (Window->Content, 0);
  DecodePane = RwViewPane (Right, 0);
  lv_obj_set_height (DecodePane, DECODE_HEIGHT);
  mSmb.Decode = RwPropTableCreate (DecodePane, Window, TRUE);
  RwPropTableList (mSmb.Decode)->ExtraKey = WindowKey;

  //
  // The grid starts on an empty SPD copy until a device is selected.
  //
  RwSpdSourceInit (&mSmb.Source, mSmb.Spd);
  RwSpacePaneCreate (&mSmb.Pane, Right, Window, &mSmb.Source);
  lv_obj_set_width (mSmb.Pane.Column, LV_PCT (100));
  lv_obj_set_flex_grow (mSmb.Pane.Column, 1);

  Rescan ();
  RwSpacePaneSize (&mSmb.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, LIST_WIDTH + 4 + PaneW, PaneH + DECODE_HEIGHT + 8);
  RwFocusSet (&RwTableList (mSmb.Devices)->Base);
  return Window;
}

RW_SMBUS_VIEW *
RwSmbusViewGet (
  VOID
  )
{
  return &mSmb;
}
