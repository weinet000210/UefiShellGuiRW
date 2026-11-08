/** @file
  Super I/O window: the chips found on 2Eh / 4Eh with their logical devices
  (function, active, base addresses, IRQ) on the left, the selected logical
  device's configuration registers 00h-FFh as a hex grid on the right
  (enter / LDN select / exit around every access).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define TREE_WIDTH  330

STATIC RW_SIO_VIEW  mSio;

STATIC
VOID
Show (
  IN INT32  Tag
  )
{
  UINTN  Chip;
  UINT8  Ldn;
  CHAR8  Text[96];
  CHAR8  Desc[64];

  if ((Tag < 0) || (Tag == mSio.CurrentTag)) {
    return;
  }

  Chip = (UINTN)Tag / 256;
  Ldn  = (UINT8)(Tag % 256);
  if (Chip >= mSio.List.ChipCount) {
    return;
  }

  mSio.CurrentTag = Tag;
  RwSioSourceInit (&mSio.Source, &mSio.List.Chips[Chip].Chip, Ldn);
  RwSpacePaneSetSource (&mSio.Pane, &mSio.Source);
  RwSioDescribe (&mSio.List.Chips[Chip].Chip, Desc, sizeof (Desc));
  if (Ldn == RW_SIO_GLOBAL) {
    AsciiSPrint (Text, sizeof (Text), "%a   global registers", Desc);
  } else {
    AsciiSPrint (Text, sizeof (Text), "%a   LDN %02Xh %a", Desc, Ldn, RwSioLdnName (&mSio.List.Chips[Chip].Chip, Ldn));
  }

  lv_label_set_text (mSio.Pane.Heading, Text);
}

STATIC
VOID
OnSelect (
  IN VOID               *View,
  IN CONST RW_INFO_ROW  *Row,
  IN UINTN              Index,
  IN VOID               *Context
  )
{
  Show (Row->Tag);
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
  if (Row->Tag >= 0) {
    RwFocusSet (RwHexGridWidget (mSio.Pane.Grid));
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mSio.Found) {
    RwHexGridRefresh (mSio.Pane.Grid);
  }
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mSio, sizeof (mSio));
}

RW_MDI_WINDOW *
RwViewOpenSio (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Left;
  lv_obj_t       *Column;
  RW_INFO        Info;
  INT32          PaneW;
  INT32          PaneH;

  if (mSio.Window != NULL) {
    RwMdiActivate (mSio.Window);
    return mSio.Window;
  }

  Window = RwMdiCreate ("Super I/O", RwCmdSuperIo, 900, 480);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mSio, sizeof (mSio));
  mSio.Window       = Window;
  mSio.CurrentTag   = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);
  mSio.Found = (BOOLEAN)!EFI_ERROR (RwSioScan (&mSio.List));

  if (!mSio.Found) {
    Column    = RwViewColumn (Window->Content, 0);
    mSio.Note = RwViewLabel (
                  Column,
                  "No supported Super I/O chip answers on 2Eh or 4Eh\n(Nuvoton / Winbond, Fintek, ITE, SMSC, ASPEED)."
                  );
    RwMdiFitContent (Window, 480, 60);
    return Window;
  }

  Left = RwViewColumn (Window->Content, TREE_WIDTH);
  RwViewLabel (Left, "Chips and logical devices");
  mSio.Tree = RwTreeViewCreate (RwViewPane (Left, 1), Window);
  RwTreeViewSetCallbacks (mSio.Tree, OnSelect, OnActivate, NULL);

  //
  // Start on the first chip's global registers.
  //
  RwSioSourceInit (&mSio.Source, &mSio.List.Chips[0].Chip, RW_SIO_GLOBAL);
  RwSpacePaneCreate (&mSio.Pane, Window->Content, Window, &mSio.Source);
  RwInfoInit (&Info);
  RwSioDecode (&mSio.List, &Info);
  RwTreeViewSetInfo (mSio.Tree, &Info, 4);
  RwSpacePaneSize (&mSio.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, TREE_WIDTH + 4 + PaneW, PaneH);
  RwFocusSet (&RwTreeViewList (mSio.Tree)->Base);
  return Window;
}

RW_SIO_VIEW *
RwSioViewGet (
  VOID
  )
{
  return &mSio;
}
