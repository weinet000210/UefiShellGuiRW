/** @file
  Memory window: the UEFI memory map on the left (Enter or double-click
  jumps there), physical memory / MMIO as a hex grid on the right. Ctrl+G
  in the grid goes to any address.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  270

STATIC RW_MEM_VIEW  mMem;

STATIC
VOID
OnActivate (
  IN RW_TABLE  *Table,
  IN UINTN     Row,
  IN INT32     Tag,
  IN VOID      *Context
  )
{
  if ((Tag >= 0) && ((UINTN)Tag < mMem.RangeCount)) {
    RwHexGridGoTo (mMem.Pane.Grid, mMem.Ranges[Tag].Start);
    RwFocusSet (RwHexGridWidget (mMem.Pane.Grid));
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwHexGridRefresh (mMem.Pane.Grid);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mMem.Ranges != NULL) {
    FreePool (mMem.Ranges);
  }

  ZeroMem (&mMem, sizeof (mMem));
}

STATIC
UINT8
RangeEmphasis (
  IN UINT32  Type
  )
{
  switch (Type) {
    case EfiConventionalMemory:
      return RwEmNormal;
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
      return RwEmAlert;           // writes here ask first
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
      return RwEmStrong;
    default:
      return RwEmDim;
  }
}

RW_MDI_WINDOW *
RwViewOpenMemory (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  UINTN                         Index;
  CHAR8                         Start[20];
  CHAR8                         Size[20];
  CONST CHAR8                   *Cells[3];
  UINT64                        Bytes;
  INT32                         PaneW;
  INT32                         PaneH;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Start", 140 }, { "Size", 56 }, { "Type", 0 } };

  if (mMem.Window != NULL) {
    RwMdiActivate (mMem.Window);
    return mMem.Window;
  }

  Window = RwMdiCreate ("Memory", RwCmdMemory, 980, 520);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mMem, sizeof (mMem));
  mMem.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, "Memory map (Enter: go to)");
  mMem.Map = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mMem.Map, NULL, OnActivate, NULL);
  RwMemMapGet (&mMem.Ranges, &mMem.RangeCount);
  for (Index = 0; Index < mMem.RangeCount; Index++) {
    AsciiSPrint (Start, sizeof (Start), "%016lX", mMem.Ranges[Index].Start);
    Bytes = LShiftU64 (mMem.Ranges[Index].Pages, EFI_PAGE_SHIFT);
    if (Bytes >= SIZE_1GB) {
      AsciiSPrint (Size, sizeof (Size), "%luG", RShiftU64 (Bytes, 30));
    } else if (Bytes >= SIZE_1MB) {
      AsciiSPrint (Size, sizeof (Size), "%luM", RShiftU64 (Bytes, 20));
    } else {
      AsciiSPrint (Size, sizeof (Size), "%luK", RShiftU64 (Bytes, 10));
    }

    Cells[0] = Start;
    Cells[1] = Size;
    Cells[2] = RwMemTypeName (mMem.Ranges[Index].Type);
    RwTableAddRow (mMem.Map, Cells, (INT32)Index, RangeEmphasis (mMem.Ranges[Index].Type));
  }

  RwMemSourceInit (&mMem.Source);
  RwSpacePaneCreate (&mMem.Pane, Window->Content, Window, &mMem.Source);
  RwSpacePaneSize (&mMem.Pane, &PaneW, &PaneH);
  RwMdiFitContent (Window, LIST_WIDTH + 4 + PaneW, PaneH + 40);
  RwFocusSet (RwHexGridWidget (mMem.Pane.Grid));
  return Window;
}

RW_MEM_VIEW *
RwMemViewGet (
  VOID
  )
{
  return &mMem;
}
