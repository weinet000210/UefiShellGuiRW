/** @file
  RwCoreTest: PCI on QEMU q35 (host bridge 8086:29C0 at 00:00.0, ICH9 LPC
  8086:2918 at 00:1F.0, ICH9 AHCI at 00:1F.2, ICH9 SMBus at 00:1F.3).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

STATIC
INTN
FindDevice (
  IN CONST RW_PCI_LIST  *List,
  IN UINT8              Bus,
  IN UINT8              Dev,
  IN UINT8              Func
  )
{
  UINTN  Index;

  for (Index = 0; Index < List->DeviceCount; Index++) {
    if ((List->Devices[Index].Bus == Bus) && (List->Devices[Index].Dev == Dev) && (List->Devices[Index].Func == Func)) {
      return (INTN)Index;
    }
  }

  return -1;
}

STATIC
BOOLEAN
HasRow (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if (AsciiStrCmp (Info->Rows[Index].Name, Name) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
TestPci (
  VOID
  )
{
  RW_PCI_LIST          List;
  EFI_STATUS           Status;
  INTN                 Host;
  INTN                 Lpc;
  INTN                 Sata;
  INTN                 Port;
  INTN                 Child;
  CONST RW_PCI_DEVICE  *E;
  RW_INFO              Info;
  RW_DATA_SOURCE       Ds;
  UINT64               Value;
  UINTN                *Order;
  UINTN                Count;
  UINTN                Index;
  CHAR8                Text[96];

  Status = RwPciScan (&List);
  CHECK (!EFI_ERROR (Status), "RwPciScan succeeds");
  CHECK ((List.RootCount >= 1) && (List.DeviceCount >= 4), "at least one root bridge and four devices");
  for (Index = 0; Index < List.DeviceCount; Index++) {
    RwPciDescribe (&List.Devices[Index], Text, sizeof (Text));
    TestLog ("    %a  depth %d", Text, List.Devices[Index].Depth);
  }

  Host = FindDevice (&List, 0, 0, 0);
  Lpc  = FindDevice (&List, 0, 0x1F, 0);
  Sata = FindDevice (&List, 0, 0x1F, 2);
  CHECK ((Host >= 0) && (List.Devices[Host].VendorId == 0x8086) && (List.Devices[Host].DeviceId == 0x29C0), "00:00.0 is the Q35 host bridge 8086:29C0");
  CHECK ((Lpc >= 0) && (List.Devices[Lpc].DeviceId == 0x2918) && (List.Devices[Lpc].BaseClass == 6) && (List.Devices[Lpc].SubClass == 1), "00:1F.0 is the ICH9 LPC bridge 8086:2918");
  CHECK ((Sata >= 0) && (List.Devices[Sata].BaseClass == 1) && (List.Devices[Sata].SubClass == 6), "00:1F.2 is a SATA controller");
  if (Host < 0) {
    RwPciFree (&List);
    return;
  }

  E = &List.Devices[Host];
  CHECK (AsciiStrCmp (RwPciClassName (6, 0), "Host Bridge") == 0, "RwPciClassName");
  TestLog ("    host bridge extended config space: %a", E->ExtConfig ? "yes" : "no (conventional PCI device)");
  CHECK ((E->Parent == RW_PCI_NO_PARENT) && (E->Depth == 0), "host bridge sits on the root bus");
  CHECK (RwPciRd (E, 0, 4) == 0x29C08086, "RwPciRd reads the ID register");

  RwInfoInit (&Info);
  RwPciDecode (E, &Info);
  CHECK (!Info.Oom && (Info.Count > 8), "RwPciDecode fills the info page");
  CHECK ((Info.Count > 0) && (Info.Rows[0].Kind == RwKindSection) && (AsciiStrCmp (Info.Rows[0].Name, "Header") == 0), "info page starts with Header");
  CHECK (HasRow (&Info, "Vendor:Device") && HasRow (&Info, "Command") && HasRow (&Info, "Base Address Registers"), "info page has the header fields");
  RwInfoFree (&Info);

  if (Lpc >= 0) {
    RwInfoInit (&Info);
    RwPciDecode (&List.Devices[Lpc], &Info);
    CHECK (!Info.Oom && HasRow (&Info, "Capabilities"), "LPC bridge decodes");
    RwInfoFree (&Info);
  }

  Order = AllocatePool (List.DeviceCount * sizeof (UINTN));
  Count = (Order != NULL) ? RwPciTreeOrder (&List, Order) : 0;
  CHECK (Count == List.DeviceCount, "tree order visits every device once");
  if (Order != NULL) {
    FreePool (Order);
  }

  RwInfoInit (&Info);
  RwPciDecodeTree (&List, &Info);
  CHECK (!Info.Oom && (Info.Count == List.DeviceCount + List.RootCount), "tree has a row per device and root bridge");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  RwPciDump (&List, &Info);
  CHECK (!Info.Oom && (Info.Count > List.DeviceCount * 8), "RwPciDump");
  TestLog ("    dump: %d rows", Info.Count);
  RwInfoFree (&Info);

  RwPciSourceInit (&Ds, E);
  TestLog ("    source \"%a\"", Ds.Name);
  CHECK (Ds.Limit == (E->ExtConfig ? 0xFFFU : 0xFFU), "config source covers 256 B or 4 KB as probed");
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 2, 2, &Value)) && (Value == 0x29C0), "config source reads the device ID");
  CHECK (RwDsIsDangerous (&Ds, 0x10, 4) && RwDsIsDangerous (&Ds, 0x30, 1), "BAR and ROM BAR writes are dangerous");
  CHECK (!RwDsIsDangerous (&Ds, 0x04, 2) && !RwDsIsDangerous (&Ds, 0x3C, 1), "command and interrupt line writes are not");
  CHECK (!EFI_ERROR (RwDsRead (&Ds, 0x3C, 1, &Value)), "config read of 3Ch");
  CHECK (!EFI_ERROR (RwDsWrite (&Ds, 0x3C, 1, Value)), "config write of 3Ch (same value)");

  //
  // CI starts the machine with a PCIe root port and a device behind it
  // (-device pcie-root-port,id=rp1 -device virtio-rng-pci,bus=rp1).
  //
  Port = -1;
  for (Index = 0; Index < List.DeviceCount; Index++) {
    if ((List.Devices[Index].PcieType == 4) && RwPciIsBridge (&List.Devices[Index])) {
      Port = (INTN)Index;
      break;
    }
  }

  if (Port < 0) {
    TestLog ("    no PCIe root port (run without -device pcie-root-port), PCIe checks skipped");
  } else {
    E = &List.Devices[Port];
    CHECK (E->ExtConfig, "root port has 4 KB config space (ECAM)");
    CHECK ((E->PcieCapPtr >= 0x40) && (E->MaxWidth != 0), "root port PCIe capability and link found");
    Child = -1;
    for (Index = 0; Index < List.DeviceCount; Index++) {
      if (List.Devices[Index].Bus == E->SecBus) {
        Child = (INTN)Index;
        break;
      }
    }

    CHECK ((Child >= 0) && (List.Devices[Child].Parent == (INT32)Port) && (List.Devices[Child].Depth == 1), "device behind the root port links to it in the tree");
    RwInfoInit (&Info);
    RwPciDecode (E, &Info);
    CHECK (HasRow (&Info, "LnkSta") && HasRow (&Info, "Bridge") && HasRow (&Info, "Extended Capabilities"), "root port decodes link, bridge windows and extended capabilities");
    RwInfoFree (&Info);
    RwPciSourceInit (&Ds, E);
    CHECK ((Ds.Limit == 0xFFF) && RwDsIsDangerous (&Ds, 0x19, 1) && RwDsIsDangerous (&Ds, 0x20, 4), "bus numbers and windows of a bridge are dangerous");
  }

  RwPciFree (&List);
  CHECK ((List.Devices == NULL) && (List.DeviceCount == 0), "RwPciFree resets");
}
