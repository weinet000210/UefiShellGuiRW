/** @file
  ACPI Tables window: every table the firmware publishes on the left
  (signature, OEM table ID, length, checksum), the selected table on the
  right. FADT, MADT, MCFG, HPET and DMAR are decoded field by field; DSDT
  and SSDT show as ASL source (Ctrl+T switches to header + hex and back).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views.h"

#define LIST_WIDTH  300

STATIC RW_ACPI_VIEW  mAcpi;

STATIC
VOID
ShowPane (
  IN BOOLEAN  Asl
  )
{
  BOOLEAN  Focused;

  Focused = (BOOLEAN)(RwFocusGet (mAcpi.Window) == &RwTextViewList (mAcpi.Asl)->Base ||
                      RwFocusGet (mAcpi.Window) == &RwPropTableList (mAcpi.Decode)->Base);
  lv_obj_set_hidden (mAcpi.AslPane, (BOOLEAN)!Asl);
  lv_obj_set_hidden (mAcpi.DecodePane, Asl);
  if (Focused) {
    RwFocusSet (Asl ? &RwTextViewList (mAcpi.Asl)->Base : &RwPropTableList (mAcpi.Decode)->Base);
  }
}

STATIC
VOID
Show (
  IN INT32    Index,
  IN BOOLEAN  Force
  )
{
  RW_ACPI_TABLE  *Table;
  RW_INFO        Info;
  RW_STR         Text;
  CHAR8          Heading[96];
  BOOLEAN        Asl;

  if ((Index < 0) || ((UINTN)Index >= mAcpi.List.Count) || ((Index == mAcpi.Current) && !Force)) {
    return;
  }

  mAcpi.Current = Index;
  Table         = &mAcpi.List.Tables[Index];
  Asl           = (BOOLEAN)(RwAcpiIsAml (Table) && mAcpi.ShowAsl);
  AsciiSPrint (
    Heading,
    sizeof (Heading),
    "%a  %a  %a   %u bytes at %lXh%a",
    Table->Signature,
    Table->OemId,
    Table->OemTableId,
    Table->Length,
    Table->Address,
    RwAcpiIsAml (Table) ? (Asl ? "   (ASL, Ctrl+T: hex)" : "   (Ctrl+T: ASL)") : ""
    );
  lv_label_set_text (mAcpi.Heading, Heading);

  if (Asl) {
    RwStrInit (&Text);
    RwAmlDisassemble (&mAcpi.Aml, Table->Ptr, Table->Length, FALSE, &Text);
    RwTextViewSetText (mAcpi.Asl, RwStrDetach (&Text));
  } else {
    RwInfoInit (&Info);
    RwAcpiDecode (Table, &Info);
    RwPropTableSetInfo (mAcpi.Decode, &Info);
  }

  ShowPane (Asl);
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
  RwFocusSet (lv_obj_is_hidden (mAcpi.AslPane) ? &RwPropTableList (mAcpi.Decode)->Base : &RwTextViewList (mAcpi.Asl)->Base);
}

STATIC
BOOLEAN
WindowKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if (RwKeyIsCtrl (Key, 't')) {
    mAcpi.ShowAsl = (BOOLEAN)!mAcpi.ShowAsl;
    Show (mAcpi.Current, TRUE);
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
  Show (mAcpi.Current, TRUE);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  RwAmlFree (&mAcpi.Aml);
  RwAcpiFree (&mAcpi.List);
  ZeroMem (&mAcpi, sizeof (mAcpi));
}

RW_MDI_WINDOW *
RwViewOpenAcpi (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Left;
  lv_obj_t                      *Right;
  UINTN                         Index;
  RW_ACPI_TABLE                 *T;
  CHAR8                         Length[16];
  CONST CHAR8                   *Cells[4];
  STATIC CONST RW_VLIST_COLUMN  Columns[] = { { "Table", 50 }, { "OEM Table", 84 }, { "Length", 76 }, { "Sum", 0 } };

  if (mAcpi.Window != NULL) {
    RwMdiActivate (mAcpi.Window);
    return mAcpi.Window;
  }

  Window = RwMdiCreate ("ACPI Tables", RwCmdAcpi, 980, 560);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mAcpi, sizeof (mAcpi));
  mAcpi.Window      = Window;
  mAcpi.Current     = -1;
  mAcpi.ShowAsl     = TRUE;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column (Window->Content, 4, 0);

  RwAcpiScan (&mAcpi.List);
  RwAmlInit (&mAcpi.Aml);
  RwAmlPrepareTables (&mAcpi.Aml, &mAcpi.List);

  Left = RwViewColumn (Window->Content, LIST_WIDTH);
  RwViewLabel (Left, (mAcpi.List.Rsdp != NULL) ? (mAcpi.List.UsesXsdt ? "Tables (from XSDT)" : "Tables (from RSDT)") : "No ACPI tables published");
  mAcpi.Tables = RwTableCreate (RwViewPane (Left, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableSetCallbacks (mAcpi.Tables, OnSelect, OnActivate, NULL);
  RwTableList (mAcpi.Tables)->ExtraKey = WindowKey;
  for (Index = 0; Index < mAcpi.List.Count; Index++) {
    T = &mAcpi.List.Tables[Index];
    AsciiSPrint (Length, sizeof (Length), "%X", T->Length);
    Cells[0] = T->Signature;
    Cells[1] = T->OemTableId;
    Cells[2] = Length;
    Cells[3] = !T->HasHeader ? "-" : (T->ChecksumOk ? "OK" : "BAD");
    RwTableAddRow (mAcpi.Tables, Cells, (INT32)Index, (T->HasHeader && !T->ChecksumOk) ? RwEmAlert : (RwAcpiIsAml (T) ? RwEmStrong : RwEmNormal));
  }

  Right            = RwViewColumn (Window->Content, 0);
  mAcpi.Heading    = RwViewLabel (Right, "");
  mAcpi.DecodePane = RwViewPane (Right, 1);
  mAcpi.AslPane    = RwViewPane (Right, 1);
  mAcpi.Decode     = RwPropTableCreate (mAcpi.DecodePane, Window, TRUE);
  mAcpi.Asl        = RwTextViewCreate (mAcpi.AslPane, Window);
  RwPropTableList (mAcpi.Decode)->ExtraKey = WindowKey;
  RwTextViewList (mAcpi.Asl)->ExtraKey     = WindowKey;
  lv_obj_set_hidden (mAcpi.AslPane, TRUE);

  if (mAcpi.List.Count > 0) {
    RwVListSelect (RwTableList (mAcpi.Tables), 0);
  }

  RwMdiFitContent (Window, 960, 540);
  RwFocusSet (&RwTableList (mAcpi.Tables)->Base);
  return Window;
}

RW_ACPI_VIEW *
RwAcpiViewGet (
  VOID
  )
{
  return &mAcpi;
}
