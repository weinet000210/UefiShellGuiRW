/** @file
  Phase 4 self test: PCIe Tree (with the jump into PCI Devices), ACPI
  Tables (decode, ASL, Ctrl+T), SMBIOS and USB Devices, driven with keys.
  Nothing is written.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views/Views.h"

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

STATIC
VOID
Enter (
  VOID
  )
{
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
}

STATIC
BOOLEAN
InfoHas (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Text
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if ((AsciiStrStr (Info->Rows[Index].Name, Text) != NULL) || (AsciiStrStr (Info->Rows[Index].Value, Text) != NULL)) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
TestPcieTree (
  VOID
  )
{
  RW_PCIE_TREE_VIEW  *V;
  RW_PCI_VIEW        *P;
  CONST RW_INFO      *Info;
  RW_PCI_DEVICE      *D;
  UINTN              Index;
  INT32              First;
  UINT32             Offset;

  RwRunCommand (RwCmdPcieTree);
  Pump (50);
  V = RwPcieTreeViewGet ();
  Check ("pcie tree: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("pcie tree: root bridge and every function shown", (BOOLEAN)(RwTreeViewVisibleCount (V->Tree) >= V->List.DeviceCount + 1));
  Info = RwPropTableGetInfo (V->Decode);
  Check ("pcie tree: first function decoded", (BOOLEAN)(V->Current == 0 && Info->Count > 10 && InfoHas (Info, "Vendor")));
  Check ("pcie tree: heading names the function", (BOOLEAN)(AsciiStrStr (lv_label_get_text (V->Heading), "00:00.0") != NULL));

  First = V->Current;
  Key (SCAN_DOWN, 0, 0);
  Pump (30);
  Check ("pcie tree: Down decodes the next function", (BOOLEAN)(V->Current >= 0 && V->Current != First));
  for (Index = 0; Index < V->List.DeviceCount; Index++) {
    if (V->List.Devices[Index].PcieType != RW_PCI_NO_PCIE) {
      break;
    }
  }

  if (Index < V->List.DeviceCount) {
    RwTreeViewSelectTag (V->Tree, (INT32)Index);
    Pump (30);
    Check ("pcie tree: a PCIe function shows its PCI Express capability", InfoHas (RwPropTableGetInfo (V->Decode), "Express"));
  } else {
    RwSerialLog ("    (no PCIe function on this machine: capability check skipped)\r\n");
  }

  //
  // Enter on a device row opens PCI Devices on it.
  //
  D = &V->List.Devices[V->Current];
  Enter ();
  Pump (50);
  P = RwPciViewGet ();
  Check (
    "pcie tree: Enter opens the function in PCI Devices",
    (BOOLEAN)(P->Window != NULL && RwMdiGetActive () == P->Window &&
              P->List.Devices[P->Current].Bus == D->Bus && P->List.Devices[P->Current].Dev == D->Dev &&
              P->List.Devices[P->Current].Func == D->Func && RwHexGridGetCursor (P->Pane.Grid) == 0)
    );

  //
  // Enter on a decoded field opens PCI Devices at its register.
  //
  RwMdiActivate (V->Window);
  RwFocusSet (&RwPropTableList (V->Decode)->Base);
  Info   = RwPropTableGetInfo (V->Decode);
  Offset = RW_INFO_NO_OFFSET;
  for (Index = 0; Index < Info->Count; Index++) {
    if ((Info->Rows[Index].Offset != RW_INFO_NO_OFFSET) && (Info->Rows[Index].Offset >= 4)) {
      Offset = Info->Rows[Index].Offset;
      RwVListSelect (RwPropTableList (V->Decode), Index);
      break;
    }
  }

  Enter ();
  Pump (50);
  Check (
    "pcie tree: Enter on a field opens PCI Devices at its offset",
    (BOOLEAN)(Offset != RW_INFO_NO_OFFSET && RwMdiGetActive () == P->Window && RwHexGridGetCursor (P->Pane.Grid) == Offset)
    );
}

STATIC
VOID
TestAcpi (
  VOID
  )
{
  RW_ACPI_VIEW   *V;
  RW_ACPI_TABLE  *Facp;
  RW_ACPI_TABLE  *Dsdt;
  BOOLEAN        AllOk;
  UINTN          Index;

  RwRunCommand (RwCmdAcpi);
  Pump (50);
  V = RwAcpiViewGet ();
  Check ("acpi: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  AllOk = TRUE;
  for (Index = 0; Index < V->List.Count; Index++) {
    AllOk &= V->List.Tables[Index].ChecksumOk;
  }

  Check ("acpi: every table listed, checksums good", (BOOLEAN)(V->List.Count >= 4 && RwTableRowCount (V->Tables) == V->List.Count && AllOk));
  Check ("acpi: the first table is shown", (BOOLEAN)(V->Current == 0 && RwPropTableGetInfo (V->Decode)->Count > 3));

  Facp = RwAcpiFindTable (&V->List, "FACP", 0);
  Dsdt = RwAcpiFindTable (&V->List, "DSDT", 0);
  Check ("acpi: FADT and DSDT present", (BOOLEAN)(Facp != NULL && Dsdt != NULL));
  if ((Facp == NULL) || (Dsdt == NULL)) {
    return;
  }

  RwVListSelect (RwTableList (V->Tables), (UINTN)(Facp - V->List.Tables));
  Pump (30);
  Check (
    "acpi: FADT is decoded field by field",
    (BOOLEAN)(lv_obj_is_hidden (V->AslPane) && !lv_obj_is_hidden (V->DecodePane) && InfoHas (RwPropTableGetInfo (V->Decode), "PM1a"))
    );

  RwVListSelect (RwTableList (V->Tables), (UINTN)(Dsdt - V->List.Tables));
  Pump (50);
  Check ("acpi: DSDT shows as ASL", (BOOLEAN)(!lv_obj_is_hidden (V->AslPane) && lv_obj_is_hidden (V->DecodePane) && RwTextViewLineCount (V->Asl) > 50));
  Key (SCAN_NULL, 't', UEFI_GUI_KEY_MOD_CTRL);
  Pump (30);
  Check ("acpi: Ctrl+T switches the DSDT to header + hex", (BOOLEAN)(lv_obj_is_hidden (V->AslPane) && RwPropTableGetInfo (V->Decode)->Count > 10));
  Key (SCAN_NULL, 't', UEFI_GUI_KEY_MOD_CTRL);
  Pump (30);
  Check ("acpi: Ctrl+T back to ASL", (BOOLEAN)!lv_obj_is_hidden (V->AslPane));
  Enter ();
  Pump (30);
  Check ("acpi: Enter moves the focus to the ASL text", (BOOLEAN)(RwFocusGet (V->Window) == &RwTextViewList (V->Asl)->Base));
  Key (SCAN_NULL, CHAR_TAB, 0);
  Check ("acpi: Tab skips the hidden decode pane", (BOOLEAN)(RwFocusGet (V->Window) == &RwTableList (V->Tables)->Base));
}

STATIC
VOID
TestSmbios (
  VOID
  )
{
  RW_SMBIOS_VIEW  *V;
  INTN            System;

  RwRunCommand (RwCmdSmbios);
  Pump (50);
  V = RwSmbiosViewGet ();
  Check ("smbios: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("smbios: table loaded, one row per structure", (BOOLEAN)(V->Loaded && V->Table.Count > 3 && RwTableRowCount (V->Structs) == V->Table.Count));
  Check ("smbios: first structure decoded", (BOOLEAN)(V->Current == 0 && RwPropTableGetInfo (V->Decode)->Count > 3));
  System = RwSmbiosFindType (&V->Table, 1, 0);
  Check ("smbios: has System Information", (BOOLEAN)(System >= 0));
  if (System < 0) {
    return;
  }

  RwVListSelect (RwTableList (V->Structs), (UINTN)System);
  Pump (30);
  Check (
    "smbios: System Information decoded with its strings",
    (BOOLEAN)(V->Current == (INT32)System && InfoHas (RwPropTableGetInfo (V->Decode), "Manufacturer") &&
              InfoHas (RwPropTableGetInfo (V->Decode), "QEMU"))
    );
  Key (SCAN_END, 0, 0);
  Pump (30);
  Check ("smbios: End reaches End-of-Table", (BOOLEAN)(V->Table.Structs[V->Current].Type == 127));
}

STATIC
VOID
TestUsb (
  VOID
  )
{
  RW_USB_VIEW  *V;
  UINTN        Index;
  UINTN        Count;

  RwRunCommand (RwCmdUsb);
  Pump (50);
  V = RwUsbViewGet ();
  Check ("usb: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("usb: xHCI controller found and shown", (BOOLEAN)(V->Scan.HcCount >= 1 && V->CurrentTag == RW_USB_HC_TAG_BASE && InfoHas (RwPropTableGetInfo (V->Decode), "Port")));
  for (Index = 0; Index < V->Scan.DevCount; Index++) {
    if (V->Scan.Devs[Index].Vid == 0x0627) {
      break;
    }
  }

  Check ("usb: QEMU keyboard / tablet enumerated", (BOOLEAN)(Index < V->Scan.DevCount));
  if (Index < V->Scan.DevCount) {
    RwTreeViewSelectTag (V->Tree, (INT32)Index);
    Pump (30);
    Check (
      "usb: device decoded (descriptors)",
      (BOOLEAN)(V->CurrentTag == (INT32)Index && AsciiStrStr (lv_label_get_text (V->Heading), "0627") != NULL &&
                InfoHas (RwPropTableGetInfo (V->Decode), "Interface"))
      );
  }

  Count = V->Scan.DevCount;
  Key (SCAN_F5, 0, 0);
  Pump (50);
  Check ("usb: F5 rescans, same devices, selection kept", (BOOLEAN)(V->Scan.DevCount == Count && V->CurrentTag == (INT32)Index));
}

VOID
RwSelfTestDecoders (
  VOID
  )
{
  RW_MDI_WINDOW  *Acpi;

  TestPcieTree ();
  TestAcpi ();
  TestSmbios ();
  TestUsb ();

  //
  // Screenshot layout: cascade, ACPI Tables (DSDT as ASL) on top.
  //
  RwRunCommand (RwCmdCascade);
  Pump (50);
  Acpi = RwMdiFind (RwCmdAcpi);
  if (Acpi != NULL) {
    RwMdiActivate (Acpi);
  }

  Pump (50);
}
