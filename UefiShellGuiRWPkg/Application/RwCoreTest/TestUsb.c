/** @file
  RwCoreTest: USB module.

  A QEMU q35 machine has a USB controller only when it is started with one,
  so the live part only checks that the scan, tree and decoders succeed and
  logs what was found. Decoding, scan comparison, hot-plug tracking and the
  tree are checked on synthetic data.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

//
// Configuration set: configuration, interface (HID), HID, endpoint 81h
// interrupt, and an unknown descriptor type 0x99.
//
STATIC CONST UINT8  mCfgSet[] = {
  0x09, 0x02, 0x26, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,       // config: total 38, self/bus=bus, remote wakeup, 100 mA
  0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,       // interface 0 alt 0, HID boot mouse
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3F, 0x00,       // HID 1.11, report 63 bytes
  0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,                   // EP 1 IN interrupt, 8 bytes, 10
  0x04, 0x99, 0xDE, 0xAD                                      // unknown type
};

STATIC CONST UINT8  mDevDesc[18] = {
  0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
  0x27, 0x06, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x01
};

STATIC
CONST RW_INFO_ROW *
FindRow (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name,
  IN UINTN          Nth
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if (AsciiStrCmp (Info->Rows[Index].Name, Name) == 0) {
      if (Nth == 0) {
        return &Info->Rows[Index];
      }

      Nth--;
    }
  }

  return NULL;
}

STATIC
BOOLEAN
RowIs (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name,
  IN UINTN          Nth,
  IN CONST CHAR8    *Value
  )
{
  CONST RW_INFO_ROW  *Row;

  Row = FindRow (Info, Name, Nth);
  if (Row == NULL) {
    TestLog ("    row %a not found", Name);
    return FALSE;
  }

  if (AsciiStrCmp (Row->Value, Value) != 0) {
    TestLog ("    row %a = \"%a\", expected \"%a\"", Name, Row->Value, Value);
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
LogInfo (
  IN CONST RW_INFO  *Info
  )
{
  RW_STR  Text;
  CHAR8   *Line;
  CHAR8   *Next;

  RwStrInit (&Text);
  RwInfoToText (Info, &Text, FALSE);
  for (Line = Text.Buf; (Line != NULL) && (*Line != '\0'); Line = Next) {
    for (Next = Line; (*Next != '\0') && (*Next != '\n'); Next++) {
    }

    if (*Next == '\n') {
      *Next++ = '\0';
    }

    TestLog ("    %a", Line);
  }

  RwStrFree (&Text);
}

STATIC
VOID
TestUsbDecoders (
  VOID
  )
{
  RW_INFO            Info;
  CONST RW_INFO_ROW  *Row;

  RwInfoInit (&Info);
  RwUsbDecodeConfig (NULL, mCfgSet, sizeof (mCfgSet), FALSE, &Info);
  CHECK (FindRow (&Info, "Configuration Descriptor", 0) != NULL, "USB config: configuration section");
  CHECK (RowIs (&Info, "wTotalLength", 0, "0026h"), "USB config: wTotalLength");
  CHECK (RowIs (&Info, "bmAttributes", 0, "A0h  Bus Powered, Remote Wakeup"), "USB config: bmAttributes");
  CHECK (RowIs (&Info, "bMaxPower", 0, "32h  (100 mA)"), "USB config: bMaxPower 2 mA units");
  CHECK (FindRow (&Info, "Interface Descriptor  (Interface 0, Alternate 0)", 0) != NULL, "USB config: interface section");
  CHECK (RowIs (&Info, "bInterfaceClass", 0, "03h  HID"), "USB config: interface class HID");
  CHECK (RowIs (&Info, "bcdHID", 0, "0111h"), "USB config: bcdHID");
  CHECK (RowIs (&Info, "wDescriptorLength", 0, "003Fh  (63 bytes)"), "USB config: HID report length");
  CHECK (FindRow (&Info, "Endpoint Descriptor  (EP 1 IN)", 0) != NULL, "USB config: endpoint section");
  CHECK (RowIs (&Info, "bEndpointAddress", 0, "81h  EP 1 IN"), "USB config: bEndpointAddress");
  CHECK (RowIs (&Info, "bmAttributes", 1, "03h  Interrupt"), "USB config: endpoint transfer type");
  CHECK (RowIs (&Info, "wMaxPacketSize", 0, "0008h  (8 bytes)"), "USB config: wMaxPacketSize");
  Row = FindRow (&Info, "bEndpointAddress", 0);
  CHECK ((Row != NULL) && (Row->Offset == 29) && (Row->Size == 1), "USB config: endpoint row offset 29");
  CHECK (FindRow (&Info, "Descriptor Type 99h  (4 bytes)", 0) != NULL, "USB config: unknown descriptor as hex");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  RwUsbDecodeConfig (NULL, mCfgSet, 9, TRUE, &Info);
  CHECK (RowIs (&Info, "bMaxPower", 0, "32h  (400 mA)"), "USB config: SuperSpeed bMaxPower 8 mA units");
  CHECK (FindRow (&Info, "Interface Descriptor  (Interface 0, Alternate 0)", 0) == NULL, "USB config: stops at Total");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  RwUsbDecodeDeviceDescriptor (NULL, mDevDesc, &Info);
  CHECK (RowIs (&Info, "bcdUSB", 0, "0200h  (USB 2.00)"), "USB device descriptor: bcdUSB");
  CHECK (RowIs (&Info, "idVendor", 0, "0627h"), "USB device descriptor: idVendor");
  CHECK (RowIs (&Info, "bMaxPacketSize0", 0, "40h  (64 bytes)"), "USB device descriptor: bMaxPacketSize0");
  CHECK (RowIs (&Info, "iProduct", 0, "02h"), "USB device descriptor: string index without UsbIo");
  RwInfoFree (&Info);

  CHECK (AsciiStrCmp (RwUsbClassName (0x08), "Mass Storage") == 0, "USB class name 08h");
  CHECK (AsciiStrCmp (RwUsbSpeedShort (EFI_USB_SPEED_HIGH), "HS") == 0, "USB speed short HS");
  CHECK (RwUsbSpeedFromRootStatus (USB_PORT_STAT_CONNECTION | USB_PORT_STAT_SUPER_SPEED) == EFI_USB_SPEED_SUPER, "USB root status SuperSpeed");
  CHECK (RwUsbSpeedFromHubStatus (FALSE, BIT0 | BIT9) == EFI_USB_SPEED_LOW, "USB hub status low speed");
}

/**
  Fill a synthetic scan: one controller with 2 root ports, devices given by
  VID list on root ports 0.. (Count entries, Vid 0 = no device on that port).
**/
STATIC
VOID
FakeScan (
  OUT RW_USB_SCAN  *Scan,
  OUT RW_USB_HC    *Hc,
  OUT RW_USB_DEV   *Devs,
  IN  CONST UINT16 *Vids,
  IN  UINTN        Count
  )
{
  UINTN  Index;

  ZeroMem (Scan, sizeof (*Scan));
  ZeroMem (Hc, sizeof (*Hc));
  Hc->Handle = (EFI_HANDLE)(UINTN)0x1000;
  Hc->Ports  = 2;
  Hc->MaxSpeed = EFI_USB_SPEED_HIGH;
  AsciiStrCpyS (Hc->Name, sizeof (Hc->Name), "EHCI Host Controller");
  Scan->Hcs     = Hc;
  Scan->HcCount = 1;
  Scan->Devs    = Devs;
  for (Index = 0; Index < Count; Index++) {
    if (Vids[Index] == 0) {
      continue;
    }

    ZeroMem (&Devs[Scan->DevCount], sizeof (RW_USB_DEV));
    Devs[Scan->DevCount].Hc      = 0;
    Devs[Scan->DevCount].Parent  = RW_USB_NO_INDEX;
    Devs[Scan->DevCount].Depth   = 1;
    Devs[Scan->DevCount].Port[0] = (UINT8)Index;
    Devs[Scan->DevCount].Vid     = Vids[Index];
    Devs[Scan->DevCount].Pid     = 0x0001;
    Devs[Scan->DevCount].Speed   = EFI_USB_SPEED_HIGH;
    Devs[Scan->DevCount].IfClass = 0x08;
    AsciiStrCpyS (Devs[Scan->DevCount].Product, sizeof (Devs[0].Product), "Fake Disk");
    Scan->DevCount++;
  }
}

STATIC
VOID
TestUsbTracking (
  VOID
  )
{
  STATIC CONST UINT16  OldVids[] = { 0x1111, 0x2222 };
  STATIC CONST UINT16  NewVids[] = { 0x3333, 0x0000 };
  RW_USB_HC            OldHc;
  RW_USB_HC            NewHc;
  RW_USB_DEV           OldDevs[2];
  RW_USB_DEV           NewDevs[2];
  RW_USB_SCAN          Old;
  RW_USB_SCAN          New;
  RW_USB_CHANGES       Changes;
  RW_USB_HOTPLUG       Tracker;
  RW_USB_TREE          Tree;
  CONST RW_USB_TRACK   *Gone;
  UINTN                Index;
  UINTN                Ports;
  BOOLEAN              ParentsOk;
  BOOLEAN              SawRemoved;

  FakeScan (&Old, &OldHc, OldDevs, OldVids, 2);
  FakeScan (&New, &NewHc, NewDevs, NewVids, 2);

  CHECK (!EFI_ERROR (RwUsbCompareScans (&Old, &Old, &Changes)) && (Changes.NewCount == 0) && (Changes.GoneCount == 0), "USB compare: same scan has no changes");
  RwUsbChangesFree (&Changes);

  CHECK (!EFI_ERROR (RwUsbCompareScans (&Old, &New, &Changes)), "USB compare: succeeds");
  CHECK ((Changes.NewCount == 1) && (Changes.New[0] == 0), "USB compare: port 1 device replaced = new");
  CHECK (Changes.GoneCount == 2, "USB compare: both old devices gone (VID changed, port emptied)");
  RwUsbChangesFree (&Changes);

  RwUsbHotplugInit (&Tracker);
  CHECK (!EFI_ERROR (RwUsbHotplugUpdate (&Tracker, &Old, 100, RW_USB_HOTPLUG_SECONDS)), "USB hotplug: first scan");
  CHECK ((Tracker.NewCount == 0) && (Tracker.GoneCount == 0), "USB hotplug: first scan marks nothing");
  RwUsbHotplugUpdate (&Tracker, &New, 103, RW_USB_HOTPLUG_SECONDS);
  CHECK (RwUsbHotplugIsNew (&Tracker, &New, 0), "USB hotplug: new device marked");
  Gone = RwUsbHotplugGoneAt (&Tracker, &New, 0, NULL, 0, 1);
  CHECK ((Gone != NULL) && (Gone->Vid == 0x2222), "USB hotplug: removed device on port 2");

  CHECK (!EFI_ERROR (RwUsbBuildTree (&New, &Tracker, &Tree)), "USB tree: synthetic build");
  ParentsOk  = (BOOLEAN)((Tree.Count > 0) && (Tree.Nodes[0].Kind == RwUsbNodeController) && (Tree.Nodes[0].Parent == -1));
  SawRemoved = FALSE;
  Ports      = 0;
  for (Index = 1; Index < Tree.Count; Index++) {
    if ((Tree.Nodes[Index].Parent < 0) || ((UINTN)Tree.Nodes[Index].Parent >= Index)) {
      ParentsOk = FALSE;
    }

    if (Tree.Nodes[Index].Kind == RwUsbNodePort) {
      Ports++;
      if (Tree.Nodes[Index].State == RwUsbPortRemoved) {
        SawRemoved = TRUE;
      }
    }
  }

  CHECK (ParentsOk, "USB tree: controller first, parents precede children");
  CHECK (Ports == 2, "USB tree: two root ports");
  CHECK ((Tree.Count == 4) && (Tree.Nodes[2].Kind == RwUsbNodeDevice) && Tree.Nodes[2].IsNew, "USB tree: device under port 1 marked new");
  CHECK (SawRemoved, "USB tree: port 2 shows the removed device");
  RwUsbTreeFree (&Tree);

  RwUsbHotplugUpdate (&Tracker, &New, 103 + RW_USB_HOTPLUG_SECONDS, RW_USB_HOTPLUG_SECONDS);
  CHECK ((Tracker.NewCount == 0) && (Tracker.GoneCount == 0), "USB hotplug: marks age out");
  RwUsbHotplugFree (&Tracker);
}

STATIC
VOID
TestUsbLive (
  VOID
  )
{
  RW_USB_SCAN  Scan;
  RW_USB_TREE  Tree;
  RW_INFO      Info;
  EFI_STATUS   Status;
  UINTN        Index;
  UINTN        Controllers;

  Status = RwUsbScan (&Scan);
  CHECK (!EFI_ERROR (Status), "USB scan succeeds");
  if (EFI_ERROR (Status)) {
    return;
  }

  TestLog ("  USB: %d host controller(s), %d device(s)", (UINT32)Scan.HcCount, (UINT32)Scan.DevCount);
  Status = RwUsbBuildTree (&Scan, NULL, &Tree);
  CHECK (!EFI_ERROR (Status), "USB tree builds");
  Controllers = 0;
  for (Index = 0; Index < Tree.Count; Index++) {
    if (Tree.Nodes[Index].Kind == RwUsbNodeController) {
      Controllers++;
    }
  }

  CHECK (Controllers == Scan.HcCount, "USB tree has one node per controller");

  RwInfoInit (&Info);
  RwUsbTreeToInfo (&Scan, &Tree, &Info);
  CHECK (Info.Count > 0, "USB tree as info has rows");
  LogInfo (&Info);
  RwInfoFree (&Info);
  RwUsbTreeFree (&Tree);

  for (Index = 0; Index < Scan.HcCount; Index++) {
    RwInfoInit (&Info);
    RwUsbDecodeController (&Scan, Index, &Info);
    CHECK (FindRow (&Info, "Host Controller", 0) != NULL, "USB controller decode");
    TestLog ("  USB controller %d: %a %a, %d ports", (UINT32)Index, Scan.Hcs[Index].Name, Scan.Hcs[Index].Pci, Scan.Hcs[Index].Ports);
    RwInfoFree (&Info);
  }

  for (Index = 0; Index < Scan.DevCount; Index++) {
    RwInfoInit (&Info);
    RwUsbDecodeDevice (&Scan, Index, &Info);
    CHECK (FindRow (&Info, "Device Descriptor", 0) != NULL, "USB device decode");
    TestLog (
      "  USB device %d: [%04X:%04X] %a, %a, %d rows",
      (UINT32)Index,
      Scan.Devs[Index].Vid,
      Scan.Devs[Index].Pid,
      Scan.Devs[Index].Product,
      RwUsbSpeedName (Scan.Devs[Index].Speed),
      (UINT32)Info.Count
      );
    RwInfoFree (&Info);
  }

  RwUsbScanFree (&Scan);
}

VOID
TestUsb (
  VOID
  )
{
  TestLog ("USB");
  TestUsbDecoders ();
  TestUsbTracking ();
  TestUsbLive ();
}
