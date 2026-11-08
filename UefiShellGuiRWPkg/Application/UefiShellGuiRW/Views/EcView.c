/** @file
  Embedded Controller window: ACPI EC RAM 00h-FFh through the standard
  command / data ports (62h / 66h) as a hex grid. Reads and writes go
  through the EC protocol (80h read, 81h write); without an EC the window
  only says so (no grid, so nothing waits on timeouts).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

STATIC RW_EC_VIEW  mEc;

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mEc.Present) {
    RwHexGridRefresh (mEc.Pane.Grid);
  }
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mEc, sizeof (mEc));
}

RW_MDI_WINDOW *
RwViewOpenEc (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  INT32          PaneW;
  INT32          PaneH;
  CHAR8          Text[160];
  lv_obj_t       *Column;

  if (mEc.Window != NULL) {
    RwMdiActivate (mEc.Window);
    return mEc.Window;
  }

  Window = RwMdiCreate ("Embedded Controller", RwCmdEc, 640, 460);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mEc, sizeof (mEc));
  mEc.Window        = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  mEc.Present = RwEcProbe (RW_EC_DEFAULT_DATA, RW_EC_DEFAULT_CMD, &mEc.Status);
  if (mEc.Present) {
    RwEcSourceInit (&mEc.Source, RW_EC_DEFAULT_DATA, RW_EC_DEFAULT_CMD);
    RwSpacePaneCreate (&mEc.Pane, Window->Content, Window, &mEc.Source);
    AsciiSPrint (Text, sizeof (Text), "EC RAM via 62h / 66h, status %02Xh. Reads and writes talk to the EC firmware.", mEc.Status);
    lv_label_set_text (mEc.Pane.Heading, Text);
    RwSpacePaneSize (&mEc.Pane, &PaneW, &PaneH);
    RwMdiFitContent (Window, PaneW, PaneH);
    RwFocusSet (RwHexGridWidget (mEc.Pane.Grid));
  } else {
    Column = RwViewColumn (Window->Content, 0);
    AsciiSPrint (
      Text,
      sizeof (Text),
      "No embedded controller answers on 62h / 66h (status %02Xh).\nDesktop boards and virtual machines usually have none.",
      mEc.Status
      );
    mEc.Note = RwViewLabel (Column, Text);
    RwMdiFitContent (Window, 480, 60);
  }

  return Window;
}

RW_EC_VIEW *
RwEcViewGet (
  VOID
  )
{
  return &mEc;
}
