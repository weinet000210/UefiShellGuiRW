/** @file
  I/O Space window: ports 0000h-FFFFh as a hex grid, 256 ports per page.
  Reads have side effects (a status register clears, a keyboard byte is
  taken), so the grid reads on open, on F5 and on Ctrl+R only.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

STATIC RW_IO_VIEW  mIo;

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mIo.Pane.Grid);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mIo, sizeof (mIo));
}

RW_MDI_WINDOW *
RwViewOpenIo (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  INT32          PaneW;
  INT32          PaneH;

  if (mIo.Window != NULL) {
    RwMdiActivate (mIo.Window);
    return mIo.Window;
  }

  Window = RwMdiCreate ("I/O Space", RwCmdIo, 640, 460);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mIo, sizeof (mIo));
  mIo.Window        = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  RwIoSourceInit (&mIo.Source);
  RwSpacePaneCreate (&mIo.Pane, Window->Content, Window, &mIo.Source);
  RwSpacePaneSize (&mIo.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, PaneW, PaneH);
  RwFocusSet (RwHexGridWidget (mIo.Pane.Grid));
  return Window;
}

RW_IO_VIEW *
RwIoViewGet (
  VOID
  )
{
  return &mIo;
}
