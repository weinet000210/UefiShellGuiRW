/** @file
  SMBIOS window: every structure on the left (type, handle, name), the
  selected structure decoded on the right (fields with their offsets, the
  string set, hex of the formatted area and strings). The heading shows the
  entry point.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  330

STATIC RW_SMBIOS_VIEW  mSmb;

STATIC
VOID
Show (
  IN INT32    Index,
  IN BOOLEAN  Force
  )
{
  RW_INFO  Info;
  CHAR8    Title[128];

  if (!mSmb.Loaded || (Index < 0) || ((UINTN)Index >= mSmb.Table.Count) || ((Index == mSmb.Current) && !Force)) {
    return;
  }

  mSmb.Current = Index;
  RwSmbiosStructTitle (&mSmb.Table.Structs[Index], Title, sizeof (Title));
  lv_label_set_text (mSmb.Heading, Title);
  RwInfoInit (&Info);
  RwSmbiosDecode (&mSmb.Table, (UINTN)Index, &Info);
  RwPropTableSetInfo (mSmb.Decode, &Info);
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
  RwFocusSet (&RwPropTableList (mSmb.Decode)->Base);
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  Show (mSmb.Current, TRUE);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwSmbiosFree (&mSmb.Table);
  ZeroMem (&mSmb, sizeof (mSmb));
}

RW_MDI_WINDOW *
RwViewOpenSmbios (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  UINTN                         Index;
  RW_SMBIOS_STRUCT              *S;
  CHAR8                         Type[8];
  CHAR8                         Handle[8];
  CHAR8                         Summary[200];
  CONST CHAR8                   *Cells[3];
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Type", 44 }, { "Handle", 56 }, { "Structure", 0 } };

  if (mSmb.Window != NULL) {
    RwMdiActivate (mSmb.Window);
    return mSmb.Window;
  }

  Window = RwMdiCreate ("SMBIOS", RwCmdSmbios, 940, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mSmb, sizeof (mSmb));
  mSmb.Window       = Window;
  mSmb.Current      = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  mSmb.Loaded = (BOOLEAN)!EFI_ERROR (RwSmbiosLoad (&mSmb.Table));
  if (mSmb.Loaded) {
    AsciiSPrint (Summary, sizeof (Summary), "SMBIOS %d.%d, %d structures", mSmb.Table.MajorVersion, mSmb.Table.MinorVersion, mSmb.Table.Count);
  } else {
    AsciiStrCpyS (Summary, sizeof (Summary), "No SMBIOS table published");
  }

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, Summary);
  mSmb.Structs = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mSmb.Structs, OnSelect, OnActivate, NULL);
  for (Index = 0; mSmb.Loaded && (Index < mSmb.Table.Count); Index++) {
    S = &mSmb.Table.Structs[Index];
    AsciiSPrint (Type, sizeof (Type), "%d", S->Type);
    AsciiSPrint (Handle, sizeof (Handle), "%04X", S->Handle);
    Cells[0] = Type;
    Cells[1] = Handle;
    Cells[2] = S->TypeName;
    RwTableAddRow (mSmb.Structs, Cells, (INT32)Index, (S->Type >= 126) ? RwEmDim : RwEmNormal);
  }

  Right        = RwViewColumn (Window->Content, 0);
  mSmb.Heading = RwViewLabel (Right, mSmb.Loaded ? mSmb.Table.Summary : "");
  mSmb.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);

  if (mSmb.Loaded && (mSmb.Table.Count > 0)) {
    RwVListSelect (RwTableList (mSmb.Structs), 0);
  }

  RwMdiFitContent (Window, 920, 540);
  RwFocusSet (&RwTableList (mSmb.Structs)->Base);
  return Window;
}

RW_SMBIOS_VIEW *
RwSmbiosViewGet (
  VOID
  )
{
  return &mSmb;
}
