/** @file
  CPUID window: every leaf and subleaf of one processor on the left (raw
  EAX..EDX), on the right the selected leaf decoded above the processor
  summary (vendor, brand, signature, features, caches, topology).
  Ctrl+Left / Ctrl+Right switch processors (through MP services, like the
  MSR window). CPUID only reads.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  (84 + 34 + 4 * 80)

STATIC RW_CPUID_VIEW  mCpuid;

STATIC
VOID
Show (
  IN INT32  Index
  )
{
  RW_INFO  Info;

  if ((Index < 0) || ((UINTN)Index >= mCpuid.List->Count)) {
    return;
  }

  mCpuid.Current = Index;
  RwInfoInit (&Info);
  RwCpuidDecodeLeaf (mCpuid.List, &mCpuid.List->Entries[Index], &Info);
  RwCpuidDecode (mCpuid.List, &Info);
  RwPropTableSetInfo (mCpuid.Decode, &Info);
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
  RwFocusSet (&RwPropTableList (mCpuid.Decode)->Base);
}

STATIC
VOID
UpdateHeading (
  EFI_STATUS  Status
  )
{
  CHAR8        Text[200];
  RW_CPU_INFO  Info;

  RwCpuGetInfo (mCpuid.CpuIndex, &Info);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "CPU %d of %d: CPUID not run (%r)", mCpuid.CpuIndex, mCpuid.CpuCount, Status);
  } else {
    AsciiSPrint (
      Text,
      sizeof (Text),
      "CPU %d of %d (APIC %X%a), %d leaves%a",
      mCpuid.CpuIndex,
      mCpuid.CpuCount,
      Info.ApicId,
      Info.IsBsp ? ", boot processor" : "",
      mCpuid.List->Count,
      (mCpuid.CpuCount > 1) ? "   Ctrl+Left/Right: CPU" : ""
      );
  }

  lv_label_set_text (mCpuid.Heading, Text);
}

STATIC
VOID
Fill (
  VOID
  )
{
  EFI_STATUS            Status;
  UINTN                 Index;
  UINTN                 Keep;
  CONST RW_CPUID_ENTRY  *E;
  CHAR8                 Leaf[12];
  CHAR8                 Sub[8];
  CHAR8                 Regs[4][12];
  CONST CHAR8           *Cells[6];

  //
  // Keep the selected leaf when switching processors.
  //
  Keep = 0;
  if ((mCpuid.Current >= 0) && ((UINTN)mCpuid.Current < mCpuid.List->Count)) {
    Keep = (UINTN)mCpuid.Current;
  }

  E = (mCpuid.List->Count > Keep) ? &mCpuid.List->Entries[Keep] : NULL;

  {
    UINT32  KeepLeaf;
    UINT32  KeepSub;

    KeepLeaf = (E != NULL) ? E->Leaf : 0;
    KeepSub  = (E != NULL) ? E->Sub : 0;
    Status   = RwCpuidCollect (mCpuid.CpuIndex, mCpuid.List);
    RwTableClear (mCpuid.Leaves);
    mCpuid.Current = -1;
    Keep           = 0;
    for (Index = 0; Index < mCpuid.List->Count; Index++) {
      E = &mCpuid.List->Entries[Index];
      AsciiSPrint (Leaf, sizeof (Leaf), "%08X", E->Leaf);
      AsciiSPrint (Sub, sizeof (Sub), "%X", E->Sub);
      AsciiSPrint (Regs[0], sizeof (Regs[0]), "%08X", E->Eax);
      AsciiSPrint (Regs[1], sizeof (Regs[1]), "%08X", E->Ebx);
      AsciiSPrint (Regs[2], sizeof (Regs[2]), "%08X", E->Ecx);
      AsciiSPrint (Regs[3], sizeof (Regs[3]), "%08X", E->Edx);
      Cells[0] = Leaf;
      Cells[1] = Sub;
      Cells[2] = Regs[0];
      Cells[3] = Regs[1];
      Cells[4] = Regs[2];
      Cells[5] = Regs[3];
      RwTableAddRow (mCpuid.Leaves, Cells, (INT32)Index, (E->Sub == 0) ? RwEmNormal : RwEmDim);
      if ((E->Leaf == KeepLeaf) && (E->Sub == KeepSub)) {
        Keep = Index;
      }
    }
  }

  UpdateHeading (Status);
  if (mCpuid.List->Count > 0) {
    RwVListSelect (RwTableList (mCpuid.Leaves), Keep);
  } else {
    RW_INFO  Info;

    RwInfoInit (&Info);
    RwPropTableSetInfo (mCpuid.Decode, &Info);
  }
}

VOID
RwCpuidViewSetCpu (
  IN UINTN  Cpu
  )
{
  CHAR8  Text[48];

  if ((mCpuid.Window == NULL) || (Cpu >= mCpuid.CpuCount)) {
    return;
  }

  mCpuid.CpuIndex = Cpu;
  Fill ();
  AsciiSPrint (Text, sizeof (Text), "CPUID of CPU %d.", Cpu);
  RwStatusSetText (Text);
}

STATIC
BOOLEAN
WindowKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if (((Key->ScanCode == SCAN_RIGHT) || (Key->ScanCode == SCAN_LEFT)) &&
      ((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0) && (mCpuid.CpuCount > 1))
  {
    if (Key->ScanCode == SCAN_RIGHT) {
      RwCpuidViewSetCpu ((mCpuid.CpuIndex + 1) % mCpuid.CpuCount);
    } else {
      RwCpuidViewSetCpu ((mCpuid.CpuIndex + mCpuid.CpuCount - 1) % mCpuid.CpuCount);
    }

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
  Fill ();
  RwStatusSetText ("CPUID read again.");
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mCpuid.List != NULL) {
    FreePool (mCpuid.List);
  }

  ZeroMem (&mCpuid, sizeof (mCpuid));
}

RW_MDI_WINDOW *
RwViewOpenCpuid (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = {
    { "Leaf", 84 }, { "Sub", 34 }, { "EAX", 80 }, { "EBX", 80 }, { "ECX", 80 }, { "EDX", 0 }
  };

  if (mCpuid.Window != NULL) {
    RwMdiActivate (mCpuid.Window);
    return mCpuid.Window;
  }

  Window = RwMdiCreate ("CPUID", RwCmdCpuid, 980, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mCpuid, sizeof (mCpuid));
  mCpuid.List = AllocateZeroPool (sizeof (RW_CPUID_LIST));
  if (mCpuid.List == NULL) {
    RwMdiClose (Window);
    return NULL;
  }

  mCpuid.Window     = Window;
  mCpuid.Current    = -1;
  mCpuid.CpuCount   = RwCpuCount ();
  mCpuid.CpuIndex   = RwCpuBsp ();
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  Left           = RwViewColumn (Window->Content, LIST_WIDTH);
  mCpuid.Heading = RwViewLabel (Left, "");
  mCpuid.Leaves  = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mCpuid.Leaves, OnSelect, OnActivate, NULL);
  RwTableList (mCpuid.Leaves)->ExtraKey = WindowKey;

  Right = RwViewColumn (Window->Content, 0);
  RwViewLabel (Right, "Leaf and processor decode (F5: read again)");
  mCpuid.Decode = RwPropTableCreate (RwViewPane (Right, 1), Window, FALSE);
  RwPropTableList (mCpuid.Decode)->ExtraKey = WindowKey;

  Fill ();
  RwMdiFitContent (Window, LIST_WIDTH + 4 + 460, 520);
  RwFocusSet (&RwTableList (mCpuid.Leaves)->Base);
  return Window;
}

RW_CPUID_VIEW *
RwCpuidViewGet (
  VOID
  )
{
  return &mCpuid;
}
