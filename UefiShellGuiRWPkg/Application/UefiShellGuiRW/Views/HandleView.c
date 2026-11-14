/** @file
  UEFI Handles window: the handle database as a tree. By handle, each
  handle with its protocols (device path text, image file, driver name) and
  who opened them; Ctrl+T switches to by protocol, each protocol with the
  handles it is installed on. Enter on a handle under a protocol jumps to
  that handle in the by-handle tree. Nothing is opened or changed.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

STATIC RW_HANDLE_VIEW   mHdl;
STATIC RW_VLIST_KEY_FN  mTreeKey;    ///< the tree's own keys (expand / collapse)

STATIC
VOID
UpdateHeading (
  VOID
  )
{
  CHAR8  Text[160];

  AsciiSPrint (
    Text,
    sizeof (Text),
    "%d handles, %d protocols, by %a   (Ctrl+T: by %a, F5: read again)",
    mHdl.Handles,
    mHdl.Protocols,
    mHdl.ByProtocol ? "protocol" : "handle",
    mHdl.ByProtocol ? "handle" : "protocol"
    );
  lv_label_set_text (mHdl.Heading, Text);
}

VOID
RwHandleViewSetMode (
  IN BOOLEAN  ByProtocol
  )
{
  RW_INFO  Info;

  if (mHdl.Window == NULL) {
    return;
  }

  mHdl.ByProtocol = ByProtocol;
  RwInfoInit (&Info);
  RwHandleDbDecode (ByProtocol, &Info, &mHdl.Handles, &mHdl.Protocols);
  //
  // Handles open one level (their protocols); openers stay folded.
  //
  RwTreeViewSetInfo (mHdl.Tree, &Info, ByProtocol ? 0 : 1);

  UpdateHeading ();
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
  INT32  Tag;

  //
  // A handle listed under a protocol: show it with all its protocols.
  //
  if (mHdl.ByProtocol && (Row->Level == 1) && (Row->Tag > 0)) {
    Tag = Row->Tag;
    RwHandleViewSetMode (FALSE);
    RwTreeViewSelectTag (mHdl.Tree, Tag);
  }
}

STATIC
BOOLEAN
WindowKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if (RwKeyIsCtrl (Key, 't')) {
    RwHandleViewSetMode ((BOOLEAN)!mHdl.ByProtocol);
    return TRUE;
  }

  return (mTreeKey != NULL) ? mTreeKey (List, Key) : FALSE;
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHandleViewSetMode (mHdl.ByProtocol);
  RwStatusSetText ("Handle database read again.");
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mHdl, sizeof (mHdl));
}

RW_MDI_WINDOW *
RwViewOpenHandles (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  lv_obj_t       *Column;

  if (mHdl.Window != NULL) {
    RwMdiActivate (mHdl.Window);
    return mHdl.Window;
  }

  Window = RwMdiCreate ("UEFI Handles", RwCmdHandles, 860, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mHdl, sizeof (mHdl));
  mHdl.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);

  Column       = RwViewColumn (Window->Content, 0);
  mHdl.Heading = RwViewLabel (Column, "");
  mHdl.Tree    = RwTreeViewCreate (RwViewPane (Column, 1), Window);
  RwTreeViewSetCallbacks (mHdl.Tree, NULL, OnActivate, NULL);
  mTreeKey                             = RwTreeViewList (mHdl.Tree)->ExtraKey;
  RwTreeViewList (mHdl.Tree)->ExtraKey = WindowKey;

  RwHandleViewSetMode (FALSE);
  RwFocusSet (&RwTreeViewList (mHdl.Tree)->Base);
  return Window;
}

RW_HANDLE_VIEW *
RwHandleViewGet (
  VOID
  )
{
  return &mHdl;
}
