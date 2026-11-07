/** @file
  Index/Data window: registers 00h-FFh behind an index / data I/O port
  pair. Common pairs are listed on the left; "Custom..." asks for both
  ports. Every read writes the index port, so the grid reads on open, on F5
  and on Ctrl+R only.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  250
#define CUSTOM_TAG  (-2)

typedef struct {
  UINT16         IndexPort;
  UINT16         DataPort;
  CONST CHAR8    *Name;
} PORT_PAIR;

STATIC CONST PORT_PAIR  mPairs[] = {
  { 0x70,  0x71,  "CMOS / RTC"                   },
  { 0x72,  0x73,  "CMOS upper bank"              },
  { 0x2E,  0x2F,  "Super I/O (config mode off)"  },
  { 0x4E,  0x4F,  "Super I/O (config mode off)"  },
  { 0xC00, 0xC01, "Intel PIRQ routing"           },
  { 0xCD6, 0xCD7, "AMD PM / FCH"                 },
};

STATIC RW_INDEX_DATA_VIEW  mIdx;

VOID
RwIndexDataViewSetPorts (
  IN UINT16  IndexPort,
  IN UINT16  DataPort
  )
{
  UINTN  Row;

  if (mIdx.Window == NULL) {
    return;
  }

  mIdx.IndexPort = IndexPort;
  mIdx.DataPort  = DataPort;
  RwIndexDataSourceInit (&mIdx.Source, IndexPort, DataPort);
  RwSpacePaneSetSource (&mIdx.Pane, &mIdx.Source);

  for (Row = 0; Row < ARRAY_SIZE (mPairs); Row++) {
    if ((mPairs[Row].IndexPort == IndexPort) && (mPairs[Row].DataPort == DataPort)) {
      if (RwTableList (mIdx.Pairs)->Selected != Row) {
        RwVListSelect (RwTableList (mIdx.Pairs), Row);
      }

      break;
    }
  }
}

STATIC
VOID
OnDataPort (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted) {
    RwIndexDataViewSetPorts (mIdx.PendingIndex, (UINT16)Value);
  }
}

STATIC
VOID
OnIndexPort (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (!Accepted || (mIdx.Window == NULL)) {
    return;
  }

  mIdx.PendingIndex = (UINT16)Value;
  RwDialogInputHex ("Custom Index/Data", "Data port:", (UINT16)(Value + 1), 4, OnDataPort, NULL);
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
  if ((Tag >= 0) && ((UINTN)Tag < ARRAY_SIZE (mPairs))) {
    if ((mPairs[Tag].IndexPort != mIdx.IndexPort) || (mPairs[Tag].DataPort != mIdx.DataPort)) {
      RwIndexDataViewSetPorts (mPairs[Tag].IndexPort, mPairs[Tag].DataPort);
    }
  }
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
  if (Tag == CUSTOM_TAG) {
    RwDialogInputHex ("Custom Index/Data", "Index port:", mIdx.IndexPort, 4, OnIndexPort, NULL);
  } else {
    RwFocusSet (RwHexGridWidget (mIdx.Pane.Grid));
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mIdx.Pane.Grid);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  ZeroMem (&mIdx, sizeof (mIdx));
}

RW_MDI_WINDOW *
RwViewOpenIndexData (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  UINTN                         Index;
  CHAR8                         Ports[16];
  CONST CHAR8                   *Cells[2];
  INT32                         PaneW;
  INT32                         PaneH;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Ports", 84 }, { "Used for", 0 } };

  if (mIdx.Window != NULL) {
    RwMdiActivate (mIdx.Window);
    return mIdx.Window;
  }

  Window = RwMdiCreate ("Index/Data", RwCmdIndexData, 900, 460);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mIdx, sizeof (mIdx));
  mIdx.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, "Port pair");
  mIdx.Pairs = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mIdx.Pairs, OnSelect, OnActivate, NULL);
  for (Index = 0; Index < ARRAY_SIZE (mPairs); Index++) {
    AsciiSPrint (Ports, sizeof (Ports), "%Xh/%Xh", mPairs[Index].IndexPort, mPairs[Index].DataPort);
    Cells[0] = Ports;
    Cells[1] = mPairs[Index].Name;
    RwTableAddRow (mIdx.Pairs, Cells, (INT32)Index, RwEmNormal);
  }

  Cells[0] = "Custom...";
  Cells[1] = "any pair (Enter)";
  RwTableAddRow (mIdx.Pairs, Cells, CUSTOM_TAG, RwEmStrong);

  mIdx.IndexPort = mPairs[0].IndexPort;
  mIdx.DataPort  = mPairs[0].DataPort;
  RwIndexDataSourceInit (&mIdx.Source, mIdx.IndexPort, mIdx.DataPort);
  RwSpacePaneCreate (&mIdx.Pane, Window->Content, Window, &mIdx.Source);
  RwSpacePaneSize (&mIdx.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, LIST_WIDTH + 4 + PaneW, PaneH);
  RwVListSelect (RwTableList (mIdx.Pairs), 0);
  RwFocusSet (&RwTableList (mIdx.Pairs)->Base);
  return Window;
}

RW_INDEX_DATA_VIEW *
RwIndexDataViewGet (
  VOID
  )
{
  return &mIdx;
}
