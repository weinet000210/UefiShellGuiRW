/** @file
  CPU MSR window (boot processor): the known MSRs with their values, the
  selected one's 64 bits below. Enter or double-click writes a new value,
  Ins adds any MSR by index, a click on a bit toggles it. Every MSR write
  asks first (warning, default No). A #GP guard turns unsupported MSRs into
  "#GP" rows instead of a hang.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

STATIC RW_MSR_VIEW  mMsr;

STATIC
VOID
ReadEntry (
  IN RW_MSR_ENTRY  *Entry
  )
{
  Entry->Valid = (BOOLEAN)(mMsr.Guard && !EFI_ERROR (RwMsrRead (Entry->Index, &Entry->Value)));
  if (!Entry->Valid) {
    Entry->Value = 0;
  }
}

STATIC
VOID
RowCells (
  IN  RW_MSR_ENTRY  *Entry,
  OUT CHAR8         *Index,
  OUT CHAR8         *Value,
  OUT CONST CHAR8   **Cells
  )
{
  CONST CHAR8  *Name;

  AsciiSPrint (Index, 12, "%08X", Entry->Index);
  if (Entry->Valid) {
    AsciiSPrint (Value, 20, "%016lX", Entry->Value);
  } else {
    AsciiStrCpyS (Value, 20, "#GP");
  }

  Name     = RwMsrName (Entry->Index);
  Cells[0] = Index;
  Cells[1] = (Name != NULL) ? Name : "";
  Cells[2] = Value;
}

STATIC
VOID
UpdateBits (
  VOID
  )
{
  UINTN  Row;

  Row = RwTableList (mMsr.Table)->Selected;
  if (Row < mMsr.Count) {
    RwBitFieldSetValue (mMsr.Bits, mMsr.Entries[Row].Value, 64, mMsr.Entries[Row].Valid);
  } else {
    RwBitFieldSetValue (mMsr.Bits, 0, 64, FALSE);
  }
}

STATIC
VOID
UpdateRow (
  IN UINTN  Row
  )
{
  CHAR8        Index[12];
  CHAR8        Value[20];
  CONST CHAR8  *Cells[3];

  RowCells (&mMsr.Entries[Row], Index, Value, Cells);
  RwTableSetRow (mMsr.Table, Row, Cells, mMsr.Entries[Row].Valid ? RwEmNormal : RwEmDim);
}

VOID
RwMsrViewReadAll (
  VOID
  )
{
  UINTN  Row;

  if (mMsr.Window == NULL) {
    return;
  }

  for (Row = 0; Row < mMsr.Count; Row++) {
    ReadEntry (&mMsr.Entries[Row]);
    UpdateRow (Row);
  }

  UpdateBits ();
}

UINTN
RwMsrViewAdd (
  IN UINT32  Index
  )
{
  UINTN         Row;
  CHAR8         IndexText[12];
  CHAR8         Value[20];
  CONST CHAR8   *Cells[3];
  RW_MSR_ENTRY  *Entry;

  if (mMsr.Window == NULL) {
    return MAX_UINTN;
  }

  for (Row = 0; Row < mMsr.Count; Row++) {
    if (mMsr.Entries[Row].Index == Index) {
      ReadEntry (&mMsr.Entries[Row]);
      UpdateRow (Row);
      RwVListSelect (RwTableList (mMsr.Table), Row);
      return Row;
    }
  }

  if (mMsr.Count == RW_MSR_VIEW_MAX) {
    RwStatusSetText ("The MSR list is full.");
    return MAX_UINTN;
  }

  Entry        = &mMsr.Entries[mMsr.Count];
  Entry->Index = Index;
  ReadEntry (Entry);
  RowCells (Entry, IndexText, Value, Cells);
  RwTableAddRow (mMsr.Table, Cells, (INT32)mMsr.Count, Entry->Valid ? RwEmNormal : RwEmDim);
  Row = mMsr.Count++;
  RwVListSelect (RwTableList (mMsr.Table), Row);
  RwStatusSetText (Entry->Valid ? "MSR added." : "That MSR raised #GP: not implemented on this CPU.");
  return Row;
}

STATIC
VOID
OnConfirm (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  EFI_STATUS  Status;
  UINTN       Row;
  CHAR8       Text[96];

  if (mMsr.Window == NULL) {
    return;
  }

  if (!Accepted) {
    UpdateBits ();       // undo a toggled bit
    RwStatusSetText ("MSR not written.");
    return;
  }

  Status = mMsr.Guard ? RwMsrWrite (mMsr.PendingIndex, mMsr.PendingValue) : EFI_UNSUPPORTED;
  for (Row = 0; Row < mMsr.Count; Row++) {
    if (mMsr.Entries[Row].Index == mMsr.PendingIndex) {
      ReadEntry (&mMsr.Entries[Row]);
      UpdateRow (Row);
      break;
    }
  }

  UpdateBits ();
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "WRMSR %08X raised #GP: value rejected.", mMsr.PendingIndex);
  } else if (Row < mMsr.Count) {
    AsciiSPrint (Text, sizeof (Text), "Wrote MSR %08X, read back %016lX.", mMsr.PendingIndex, mMsr.Entries[Row].Value);
  } else {
    AsciiSPrint (Text, sizeof (Text), "Wrote MSR %08X.", mMsr.PendingIndex);
  }

  RwStatusSetText (Text);
}

VOID
RwMsrViewWrite (
  IN UINT32  Index,
  IN UINT64  Value
  )
{
  CHAR8        Text[200];
  CONST CHAR8  *Name;

  mMsr.PendingIndex = Index;
  mMsr.PendingValue = Value;
  Name              = RwMsrName (Index);
  AsciiSPrint (
    Text,
    sizeof (Text),
    "Write %016lX to MSR %08X%a%a%a?\n\nA wrong value can hang or reset the machine.",
    Value,
    Index,
    (Name != NULL) ? " (" : "",
    (Name != NULL) ? Name : "",
    (Name != NULL) ? ")" : ""
    );
  RwDialogConfirm ("Write MSR", Text, RW_DIALOG_WARNING, OnConfirm, NULL);
}

STATIC
VOID
OnValue (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted && (mMsr.Window != NULL)) {
    RwMsrViewWrite (mMsr.PendingIndex, Value);
  }
}

STATIC
VOID
OnAddIndex (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted) {
    RwMsrViewAdd ((UINT32)Value);
  }
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
  UpdateBits ();
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
  CHAR8  Prompt[80];

  if (Row >= mMsr.Count) {
    return;
  }

  mMsr.PendingIndex = mMsr.Entries[Row].Index;
  AsciiSPrint (Prompt, sizeof (Prompt), "New value of MSR %08X:", mMsr.PendingIndex);
  RwDialogInputHex ("Write MSR", Prompt, mMsr.Entries[Row].Value, 16, OnValue, NULL);
}

STATIC
BOOLEAN
ListKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if ((Key->ScanCode == SCAN_INSERT) && ((Key->Modifiers & (UEFI_GUI_KEY_MOD_CTRL | UEFI_GUI_KEY_MOD_ALT)) == 0)) {
    RwDialogInputHex ("Add MSR", "MSR index:", 0, 8, OnAddIndex, NULL);
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
OnBits (
  IN RW_BITFIELD  *View,
  IN UINT64       NewValue,
  IN VOID         *Context
  )
{
  UINTN  Row;

  Row = RwTableList (mMsr.Table)->Selected;
  if ((Row < mMsr.Count) && mMsr.Entries[Row].Valid) {
    RwMsrViewWrite (mMsr.Entries[Row].Index, NewValue);
  }
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwMsrViewReadAll ();
  RwStatusSetText ("Re-read all MSRs.");
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mMsr.Guard) {
    RwMsrGuardRemove ();
  }

  ZeroMem (&mMsr, sizeof (mMsr));
}

RW_MDI_WINDOW *
RwViewOpenMsr (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Column;
  CONST RW_MSR_KNOWN            *Known;
  UINTN                         KnownCount;
  UINTN                         Index;
  CHAR8                         Brand[64];
  CHAR8                         Text[128];
  CHAR8                         IndexText[12];
  CHAR8                         Value[20];
  CONST CHAR8                   *Cells[3];
  INT32                         BitsW;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Index", 84 }, { "Name", 236 }, { "Value", 0 } };

  if (mMsr.Window != NULL) {
    RwMdiActivate (mMsr.Window);
    return mMsr.Window;
  }

  Window = RwMdiCreate ("CPU MSR", RwCmdMsr, 600, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mMsr, sizeof (mMsr));
  mMsr.Window       = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  mMsr.Guard = (BOOLEAN)!EFI_ERROR (RwMsrGuardInstall ());

  Column = RwViewColumn (Window->Content, 0);
  RwCpuBrand (Brand, sizeof (Brand));
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%a   (boot processor)%a",
    (Brand[0] != '\0') ? Brand : "CPU",
    mMsr.Guard ? "" : "   MSR access unavailable"
    );
  mMsr.Cpu   = RwViewLabel (Column, Text);
  mMsr.Table = RwTableCreate (RwViewPane (Column, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mMsr.Table, OnSelect, OnActivate, NULL);
  RwTableList (mMsr.Table)->ExtraKey = ListKey;
  RwViewLabel (Column, "Enter: write   Ins: add MSR   F5: re-read");
  mMsr.Bits = RwBitFieldCreate (Column, Window);
  RwBitFieldSetCallback (mMsr.Bits, OnBits, NULL);

  Known = RwMsrKnownList (&KnownCount);
  for (Index = 0; (Index < KnownCount) && (mMsr.Count < RW_MSR_VIEW_MAX); Index++) {
    mMsr.Entries[mMsr.Count].Index = Known[Index].Index;
    ReadEntry (&mMsr.Entries[mMsr.Count]);
    RowCells (&mMsr.Entries[mMsr.Count], IndexText, Value, Cells);
    RwTableAddRow (mMsr.Table, Cells, (INT32)mMsr.Count, mMsr.Entries[mMsr.Count].Valid ? RwEmNormal : RwEmDim);
    mMsr.Count++;
  }

  RwBitFieldSetValue (mMsr.Bits, 0, 64, FALSE);
  if (mMsr.Count > 0) {
    RwVListSelect (RwTableList (mMsr.Table), 0);
  }

  lv_obj_update_layout (RwBitFieldWidget (mMsr.Bits)->Obj);
  BitsW = lv_obj_get_width (RwBitFieldWidget (mMsr.Bits)->Obj);
  RwMdiFitContent (Window, MAX (BitsW, 84 + 236 + 160), 520);
  RwFocusSet (&RwTableList (mMsr.Table)->Base);
  return Window;
}

RW_MSR_VIEW *
RwMsrViewGet (
  VOID
  )
{
  return &mMsr;
}
