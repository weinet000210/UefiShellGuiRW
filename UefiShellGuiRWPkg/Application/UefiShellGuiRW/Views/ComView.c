/** @file
  COM Port window: COM1-COM4 (3F8h, 2F8h, 3E8h, 2E8h) with whether a 16550
  answers and whether the firmware's serial console sits on it, the
  selected port's line settings and line / modem status on the right, and
  the internal loopback self-test (L): MCR loopback, nothing goes out on the
  wire, the line settings are restored afterwards.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  330

STATIC RW_COM_VIEW  mCom;
STATIC UINTN        mPendingPort;

STATIC
VOID
Decode (
  IN UINTN  Index
  )
{
  RW_INFO           Info;
  RW_UART_SAVE      Save;
  UINT16            Base;
  UINT16            Divisor;
  CHAR8             Text[96];
  STATIC CONST CHAR8  Parity[] = { 'N', 'O', 'N', 'E', 'N', 'M', 'N', 'S' };

  Base = RwUartStdBase (Index);
  AsciiSPrint (Text, sizeof (Text), "COM%d at %03Xh%a", Index + 1, Base, mCom.Firmware[Index] ? "   firmware serial console" : "");
  lv_label_set_text (mCom.Heading, Text);
  RwInfoInit (&Info);
  if (!mCom.Present[Index]) {
    RwInfoText (&Info, 0, RwEmDim, "No 16550 UART answers at this address.");
    RwPropTableSetInfo (mCom.Decode, &Info);
    return;
  }

  //
  // RwUartSave reads the divisor latch (DLAB briefly set) and restores LCR.
  //
  RwUartSave (Base, &Save);
  Divisor = (UINT16)(Save.Dll | (Save.Dlm << 8));
  RwInfoSection (&Info, 0, "Line settings");
  if (Divisor != 0) {
    RwInfoField (&Info, 1, RwEmStrong, "Baud rate", "%d (divisor %d)", RW_UART_CLOCK / Divisor, Divisor);
  } else {
    RwInfoField (&Info, 1, RwEmAlert, "Baud rate", "divisor 0 (not programmed)");
  }

  RwInfoField (
    &Info,
    1,
    RwEmStrong,
    "Format",
    "%d%c%a",
    5 + (Save.Lcr & 0x03),
    ((Save.Lcr & BIT3) != 0) ? Parity[(Save.Lcr >> 3) & 0x07] : 'N',
    ((Save.Lcr & BIT2) != 0) ? (((Save.Lcr & 0x03) == 0) ? "1.5" : "2") : "1"
    );
  RwInfoField (&Info, 1, RwEmNormal, "LCR", "%02Xh%a", Save.Lcr, ((Save.Lcr & BIT6) != 0) ? "  break" : "");
  RwInfoField (&Info, 1, RwEmNormal, "IER", "%02Xh", Save.Ier);
  RwInfoField (
    &Info,
    1,
    RwEmNormal,
    "MCR",
    "%02Xh  %a%a%a%a%a",
    Save.Mcr,
    ((Save.Mcr & BIT0) != 0) ? "DTR " : "",
    ((Save.Mcr & BIT1) != 0) ? "RTS " : "",
    ((Save.Mcr & BIT2) != 0) ? "OUT1 " : "",
    ((Save.Mcr & BIT3) != 0) ? "OUT2 " : "",
    ((Save.Mcr & BIT4) != 0) ? "LOOP" : ""
    );
  RwInfoField (&Info, 1, RwEmNormal, "FIFO", Save.Fifo ? "enabled" : "off");
  RwUartDecodeStatus (Base, NULL, &Info);
  RwInfoSection (&Info, 0, "Loopback test (L)");
  if (mCom.HaveResult[Index]) {
    RwInfoText (&Info, 1, mCom.Result[Index].Pass ? RwEmStrong : RwEmAlert, "%a", mCom.Result[Index].Summary);
  } else {
    RwInfoText (&Info, 1, RwEmDim, "Not run. Internal loopback: nothing is sent on the wire.");
  }

  RwPropTableSetInfo (mCom.Decode, &Info);
}

STATIC
VOID
RunLoopback (
  IN UINTN  Index
  )
{
  RW_UART  Uart;
  CHAR8    Text[160];

  if (EFI_ERROR (RwUartOpen (&Uart, RwUartStdBase (Index), RW_UART_DEFAULT_BAUD, TRUE))) {
    RwStatusSetText ("Could not open the port.");
    return;
  }

  RwUartLoopbackTest (Uart.Base, &mCom.Result[Index]);
  RwUartClose (&Uart);
  mCom.HaveResult[Index] = TRUE;
  AsciiSPrint (Text, sizeof (Text), "COM%d: %a", Index + 1, mCom.Result[Index].Summary);
  RwStatusSetText (Text);
  if (mCom.Current == (INT32)Index) {
    Decode (Index);
  }
}

STATIC
VOID
OnConfirm (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted && (mCom.Window != NULL)) {
    RunLoopback (mPendingPort);
  }
}

VOID
RwComViewLoopback (
  IN UINTN  Index
  )
{
  CHAR8  Text[220];

  if ((mCom.Window == NULL) || (Index >= RW_UART_STD_COUNT)) {
    return;
  }

  if (!mCom.Present[Index]) {
    RwDialogMessage ("Loopback test", "No 16550 UART at this address.", NULL, NULL);
    return;
  }

  mPendingPort = Index;
  AsciiSPrint (
    Text,
    sizeof (Text),
    "Run the internal loopback test on COM%d?%a\n\nThe port is put in loopback mode for a moment and its settings are restored afterwards.",
    Index + 1,
    mCom.Firmware[Index] ? "\n\nThe firmware serial console on this port is paused while the test runs; output sent meanwhile is lost." : ""
    );
  RwDialogConfirm ("Loopback test", Text, mCom.Firmware[Index] ? RW_DIALOG_WARNING : 0, OnConfirm, NULL);
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
  if ((Tag >= 0) && (Tag != mCom.Current)) {
    mCom.Current = Tag;
    Decode ((UINTN)Tag);
  }
}

STATIC
BOOLEAN
ListKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if ((RwKeyIsChar (Key, L'l') || RwKeyIsChar (Key, L'L')) && (mCom.Current >= 0)) {
    RwComViewLoopback ((UINTN)mCom.Current);
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
  if (mCom.Current >= 0) {
    Decode ((UINTN)mCom.Current);
  }
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mCom, sizeof (mCom));
}

RW_MDI_WINDOW *
RwViewOpenCom (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  UINTN                         Index;
  CHAR8                         Name[8];
  CHAR8                         Base[8];
  CONST CHAR8                   *Cells[4];
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Port", 56 }, { "Base", 56 }, { "UART", 64 }, { "Firmware", 0 } };

  if (mCom.Window != NULL) {
    RwMdiActivate (mCom.Window);
    return mCom.Window;
  }

  Window = RwMdiCreate ("COM Ports", RwCmdComPort, 820, 440);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mCom, sizeof (mCom));
  mCom.Window       = Window;
  mCom.Current      = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, "Standard ports (L: loopback test)");
  mCom.Ports = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mCom.Ports, OnSelect, NULL, NULL);
  RwTableList (mCom.Ports)->ExtraKey = ListKey;

  Right        = RwViewColumn (Window->Content, 0);
  mCom.Heading = RwViewLabel (Right, "");
  mCom.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, FALSE);

  for (Index = 0; Index < RW_UART_STD_COUNT; Index++) {
    mCom.Present[Index]  = RwUartPresent (RwUartStdBase (Index));
    mCom.Firmware[Index] = (BOOLEAN)(RwUartFirmwareHandle (RwUartStdBase (Index)) != NULL);
    AsciiSPrint (Name, sizeof (Name), "COM%d", Index + 1);
    AsciiSPrint (Base, sizeof (Base), "%03Xh", RwUartStdBase (Index));
    Cells[0] = Name;
    Cells[1] = Base;
    Cells[2] = mCom.Present[Index] ? "16550" : "none";
    Cells[3] = mCom.Firmware[Index] ? "Serial console" : "";
    RwTableAddRow (mCom.Ports, Cells, (INT32)Index, mCom.Present[Index] ? RwEmNormal : RwEmDim);
  }

  RwVListSelect (RwTableList (mCom.Ports), 0);
  RwMdiFitContent (Window, LIST_WIDTH + 4 + 460, 400);
  RwFocusSet (&RwTableList (mCom.Ports)->Base);
  return Window;
}

RW_COM_VIEW *
RwComViewGet (
  VOID
  )
{
  return &mCom;
}
