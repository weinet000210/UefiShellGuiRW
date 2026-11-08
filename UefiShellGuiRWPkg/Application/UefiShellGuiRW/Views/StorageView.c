/** @file
  ATA / NVMe Identify window: every drive the firmware drives (ATA and
  ATAPI through ATA pass-thru, NVMe controllers and their active namespaces
  through NVMe pass-thru) on the left, the decoded identify data with the
  raw bytes on the right. Only identify commands are sent.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  390

STATIC RW_STORAGE_VIEW  mStor;

STATIC
VOID
CapacityText (
  IN  UINT64  Bytes,
  OUT CHAR8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  if (Bytes == 0) {
    AsciiStrCpyS (Buffer, BufferSize, "");
  } else if (Bytes >= 1000000000ULL) {
    AsciiSPrint (Buffer, BufferSize, "%Ld GB", DivU64x32 (Bytes, 1000000000));
  } else {
    AsciiSPrint (Buffer, BufferSize, "%Ld MB", DivU64x32 (Bytes, 1000000));
  }
}

STATIC
VOID
Show (
  IN INT32  Index
  )
{
  RW_INFO         Info;
  RW_STORAGE_DEV  *D;
  CHAR8           Text[160];

  if ((Index < 0) || ((UINTN)Index >= mStor.List->Count) || (Index == mStor.Current)) {
    return;
  }

  mStor.Current = Index;
  D             = &mStor.List->Devs[Index];
  AsciiSPrint (Text, sizeof (Text), "%a   %a", RwStorageKindName (D->Kind), D->Model);
  lv_label_set_text (mStor.Heading, Text);
  RwInfoInit (&Info);
  RwStorageDecode (D, &Info);
  RwPropTableSetInfo (mStor.Decode, &Info);
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
  Show (Tag);
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
  RwFocusSet (&RwPropTableList (mStor.Decode)->Base);
}

STATIC
VOID
Fill (
  VOID
  )
{
  UINTN           Index;
  RW_STORAGE_DEV  *D;
  CHAR8           Model[64];
  CHAR8           Size[24];
  CONST CHAR8     *Cells[3];

  RwTableClear (mStor.Drives);
  mStor.Current = -1;
  for (Index = 0; Index < mStor.List->Count; Index++) {
    D = &mStor.List->Devs[Index];
    AsciiSPrint (Model, sizeof (Model), "%a%a", (D->Kind == RwStorNvmeNs) ? "  " : "", D->Model);
    CapacityText (D->Capacity, Size, sizeof (Size));
    Cells[0] = RwStorageKindName (D->Kind);
    Cells[1] = Model;
    Cells[2] = Size;
    RwTableAddRow (mStor.Drives, Cells, (INT32)Index, (D->Kind == RwStorNvmeNs) ? RwEmNormal : RwEmStrong);
  }

  if (mStor.List->Count > 0) {
    RwVListSelect (RwTableList (mStor.Drives), 0);
  } else {
    lv_label_set_text (mStor.Heading, "No ATA or NVMe drives found by the firmware.");
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwStorageFree (mStor.List);
  RwStorageScan (mStor.List);
  Fill ();
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mStor.List != NULL) {
    RwStorageFree (mStor.List);
    FreePool (mStor.List);
  }

  ZeroMem (&mStor, sizeof (mStor));
}

RW_MDI_WINDOW *
RwViewOpenStorage (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Type", 120 }, { "Model", 196 }, { "Size", 0 } };

  if (mStor.Window != NULL) {
    RwMdiActivate (mStor.Window);
    return mStor.Window;
  }

  Window = RwMdiCreate ("ATA / NVMe Identify", RwCmdStorage, 900, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mStor, sizeof (mStor));
  mStor.List = AllocateZeroPool (sizeof (RW_STORAGE_LIST));
  if (mStor.List == NULL) {
    RwMdiClose (Window);
    return NULL;
  }

  mStor.Window      = Window;
  mStor.Current     = -1;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, "Drives (F5: identify again)");
  mStor.Drives = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mStor.Drives, OnSelect, OnActivate, NULL);

  Right         = RwViewColumn (Window->Content, 0);
  mStor.Heading = RwViewLabel (Right, "");
  mStor.Decode  = RwPropTableCreate (RwViewPane (Right, 1), Window, TRUE);

  RwStorageScan (mStor.List);
  Fill ();
  RwMdiFitContent (Window, LIST_WIDTH + 4 + 480, 520);
  RwFocusSet (&RwTableList (mStor.Drives)->Base);
  return Window;
}

RW_STORAGE_VIEW *
RwStorageViewGet (
  VOID
  )
{
  return &mStor;
}
