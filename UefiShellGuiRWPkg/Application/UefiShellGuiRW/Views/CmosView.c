/** @file
  CMOS window: CMOS / RTC RAM 00h-FFh as a hex grid (byte access), and the
  RTC registers decoded on the right. Register C is not read for the
  decode: reading it clears the interrupt flags.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define DECODE_WIDTH  300

STATIC RW_CMOS_VIEW  mCmos;

STATIC
UINT8
FromRtc (
  IN UINT8    Value,
  IN BOOLEAN  Binary
  )
{
  return Binary ? Value : (UINT8)((Value >> 4) * 10 + (Value & 0x0F));
}

STATIC
VOID
Field (
  IN RW_INFO      *Info,
  IN UINT32       Offset,
  IN UINT8        Emphasis,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Value
  )
{
  RwInfoSetReg (RwInfoField (Info, 1, Emphasis, Name, "%a", Value), Offset, 1);
}

STATIC
VOID
Decode (
  VOID
  )
{
  RW_INFO      Info;
  UINT8        RegA;
  UINT8        RegB;
  UINT8        RegD;
  BOOLEAN      Binary;
  UINT8        Hour;
  BOOLEAN      Pm;
  UINT8        Century;
  CHAR8        Text[64];
  STATIC CONST CHAR8  *Days[] = { "?", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

  RegA   = RwCmosRead8 (0x0A);
  RegB   = RwCmosRead8 (0x0B);
  RegD   = RwCmosRead8 (0x0D);
  Binary = (BOOLEAN)((RegB & BIT2) != 0);

  RwInfoInit (&Info);
  RwInfoSection (&Info, 0, "Real-time clock");

  Hour = RwCmosRead8 (0x04);
  Pm   = FALSE;
  if ((RegB & BIT1) == 0) {
    Pm   = (BOOLEAN)((Hour & BIT7) != 0);
    Hour = FromRtc (Hour & 0x7F, Binary);
  } else {
    Hour = FromRtc (Hour, Binary);
  }

  AsciiSPrint (
    Text,
    sizeof (Text),
    "%02d:%02d:%02d%a",
    Hour,
    FromRtc (RwCmosRead8 (0x02), Binary),
    FromRtc (RwCmosRead8 (0x00), Binary),
    ((RegB & BIT1) != 0) ? "" : (Pm ? " PM" : " AM")
    );
  Field (&Info, 0x00, RwEmStrong, "Time", Text);

  Century = RwCmosRead8 (0x32);
  if ((Century == 0x19) || (Century == 0x20)) {
    Century = FromRtc (Century, FALSE);       // BCD, the usual case
  } else if ((Century != 19) && (Century != 20)) {
    Century = 20;                             // not kept here
  }
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%02d%02d-%02d-%02d",
    Century,
    FromRtc (RwCmosRead8 (0x09), Binary),
    FromRtc (RwCmosRead8 (0x08), Binary),
    FromRtc (RwCmosRead8 (0x07), Binary)
    );
  Field (&Info, 0x07, RwEmStrong, "Date", Text);

  Hour = RwCmosRead8 (0x06);
  Field (&Info, 0x06, RwEmNormal, "Day of week", (Hour >= 1 && Hour <= 7) ? Days[Hour] : "not set");

  AsciiSPrint (
    Text,
    sizeof (Text),
    "%02X:%02X:%02X",
    RwCmosRead8 (0x05),
    RwCmosRead8 (0x03),
    RwCmosRead8 (0x01)
    );
  Field (&Info, 0x01, RwEmNormal, "Alarm (raw)", Text);

  RwInfoSection (&Info, 0, "Status registers");
  AsciiSPrint (Text, sizeof (Text), "%02X  %a, divider %d, rate %d", RegA, ((RegA & BIT7) != 0) ? "update in progress" : "idle", (RegA >> 4) & 7, RegA & 0x0F);
  Field (&Info, 0x0A, RwEmNormal, "Register A", Text);
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%02X  %a %a %a%a%a%a",
    RegB,
    Binary ? "binary" : "BCD",
    ((RegB & BIT1) != 0) ? "24h" : "12h",
    ((RegB & BIT7) != 0) ? "SET " : "",
    ((RegB & BIT6) != 0) ? "PIE " : "",
    ((RegB & BIT5) != 0) ? "AIE " : "",
    ((RegB & BIT4) != 0) ? "UIE" : ""
    );
  Field (&Info, 0x0B, ((RegB & BIT7) != 0) ? RwEmAlert : RwEmNormal, "Register B", Text);
  Field (&Info, 0x0C, RwEmDim, "Register C", "not read (read clears flags)");
  AsciiSPrint (Text, sizeof (Text), "%02X  %a", RegD, ((RegD & BIT7) != 0) ? "RAM and time valid" : "battery dead");
  Field (&Info, 0x0D, ((RegD & BIT7) != 0) ? RwEmNormal : RwEmAlert, "Register D", Text);

  RwInfoSection (&Info, 0, "BIOS");
  AsciiSPrint (Text, sizeof (Text), "%02X", RwCmosRead8 (0x0E));
  Field (&Info, 0x0E, RwEmNormal, "Diagnostic status", Text);
  AsciiSPrint (Text, sizeof (Text), "%02X", RwCmosRead8 (0x0F));
  Field (&Info, 0x0F, RwEmNormal, "Shutdown status", Text);
  AsciiSPrint (Text, sizeof (Text), "%02X", RwCmosRead8 (0x32));
  Field (&Info, 0x32, RwEmNormal, "Century", Text);

  RwPropTableSetInfo (mCmos.Rtc, &Info);
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mCmos.Pane.Grid);
  Decode ();
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mCmos, sizeof (mCmos));
}

RW_MDI_WINDOW *
RwViewOpenCmos (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Right;
  INT32          PaneW;
  INT32          PaneH;

  if (mCmos.Window != NULL) {
    RwMdiActivate (mCmos.Window);
    return mCmos.Window;
  }

  Window = RwMdiCreate ("CMOS", RwCmdCmos, 940, 460);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mCmos, sizeof (mCmos));
  mCmos.Window      = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  RwCmosSourceInit (&mCmos.Source);
  RwSpacePaneCreate (&mCmos.Pane, Window->Content, Window, &mCmos.Source);
  Right = RwViewColumn (Window->Content, DECODE_WIDTH);
  RwViewLabel (Right, "RTC");
  mCmos.Rtc = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);
  Decode ();

  RwSpacePaneSize (&mCmos.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, PaneW + 4 + DECODE_WIDTH, PaneH);
  RwFocusSet (RwHexGridWidget (mCmos.Pane.Grid));
  return Window;
}

RW_CMOS_VIEW *
RwCmosViewGet (
  VOID
  )
{
  return &mCmos;
}
