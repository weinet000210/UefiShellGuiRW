/** @file
  RwCoreLib USB module: host controllers, the devices the firmware's USB bus
  driver enumerated, port status and speeds, string descriptors, the
  controller -> port -> device tree, descriptor decoding and hot-plug
  tracking. Ported from uefirrw UsbView.c.

  Only standard read requests (GET_DESCRIPTOR, hub GET_STATUS) are issued.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/UsbIo.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/PciIo.h>
#include <Protocol/DevicePath.h>
#include <IndustryStandard/Pci.h>

STATIC CONST CHAR8  *mSpeedNames[] = { "Full Speed (12 Mbps)", "Low Speed (1.5 Mbps)", "High Speed (480 Mbps)", "SuperSpeed (5 Gbps)" };
STATIC CONST CHAR8  *mSpeedShort[] = { "FS", "LS", "HS", "SS" };
STATIC CONST CHAR8  *mXferType[]   = { "Control", "Isochronous", "Bulk", "Interrupt" };

//
// ---- Names ---------------------------------------------------------------------
//

CONST CHAR8 *
RwUsbClassName (
  IN UINT8  Class
  )
{
  switch (Class) {
    case 0x00: return "Defined at Interface level";
    case 0x01: return "Audio";
    case 0x02: return "Communications (CDC)";
    case 0x03: return "HID";
    case 0x05: return "Physical";
    case 0x06: return "Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC Data";
    case 0x0B: return "Smart Card";
    case 0x0D: return "Content Security";
    case 0x0E: return "Video";
    case 0x0F: return "Personal Healthcare";
    case 0x10: return "Audio/Video";
    case 0x11: return "Billboard";
    case 0x12: return "USB Type-C Bridge";
    case 0xDC: return "Diagnostic Device";
    case 0xE0: return "Wireless Controller";
    case 0xEF: return "Miscellaneous";
    case 0xFE: return "Application Specific";
    case 0xFF: return "Vendor Specific";
    default:   return "Unknown";
  }
}

CONST CHAR8 *
RwUsbSpeedName (
  IN UINT8  Speed
  )
{
  return (Speed < ARRAY_SIZE (mSpeedNames)) ? mSpeedNames[Speed] : "Unknown";
}

CONST CHAR8 *
RwUsbSpeedShort (
  IN UINT8  Speed
  )
{
  return (Speed < ARRAY_SIZE (mSpeedShort)) ? mSpeedShort[Speed] : "--";
}

UINT8
RwUsbSpeedFromRootStatus (
  IN UINT16  PortStatus
  )
{
  if (PortStatus & USB_PORT_STAT_SUPER_SPEED) {
    return EFI_USB_SPEED_SUPER;
  }

  if (PortStatus & USB_PORT_STAT_HIGH_SPEED) {
    return EFI_USB_SPEED_HIGH;
  }

  if (PortStatus & USB_PORT_STAT_LOW_SPEED) {
    return EFI_USB_SPEED_LOW;
  }

  return EFI_USB_SPEED_FULL;
}

UINT8
RwUsbSpeedFromHubStatus (
  IN BOOLEAN  SsHub,
  IN UINT16   PortStatus
  )
{
  if (SsHub) {
    return EFI_USB_SPEED_SUPER;
  }

  if (PortStatus & BIT10) {
    return EFI_USB_SPEED_HIGH;
  }

  if (PortStatus & BIT9) {
    return EFI_USB_SPEED_LOW;
  }

  return EFI_USB_SPEED_FULL;
}

//
// ---- USB requests --------------------------------------------------------------
//

STATIC
EFI_STATUS
UsbIn (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  IN  UINT8                RequestType,
  IN  UINT8                Request,
  IN  UINT16               Value,
  IN  UINT16               Index,
  OUT VOID                 *Data,
  IN  UINT16               Length
  )
{
  EFI_USB_DEVICE_REQUEST  Req;
  UINT32                  UsbStatus;

  Req.RequestType = RequestType;
  Req.Request     = Request;
  Req.Value       = Value;
  Req.Index       = Index;
  Req.Length      = Length;
  return UsbIo->UsbControlTransfer (UsbIo, &Req, EfiUsbDataIn, RW_USB_TIMEOUT_MS, Data, Length, &UsbStatus);
}

EFI_STATUS
RwUsbHubPortStatus (
  IN  EFI_USB_IO_PROTOCOL  *HubIo,
  IN  UINT8                Port,
  OUT UINT16               *Status
  )
{
  UINT16      Buf[2];
  EFI_STATUS  Rc;

  if ((HubIo == NULL) || (Status == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Rc = UsbIn (HubIo, 0xA3, 0x00, 0, (UINT16)(Port + 1), Buf, sizeof (Buf));
  if (!EFI_ERROR (Rc)) {
    *Status = Buf[0];
  }

  return Rc;
}

EFI_STATUS
RwUsbRootPortStatus (
  IN  RW_USB_HC  *Hc,
  IN  UINT8      Port,
  OUT UINT16     *Status
  )
{
  EFI_USB_PORT_STATUS  Root;
  EFI_STATUS           Rc;

  if ((Hc == NULL) || (Hc->Hc == NULL) || (Status == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Rc = Hc->Hc->GetRootHubPortStatus (Hc->Hc, Port, &Root);
  if (!EFI_ERROR (Rc)) {
    *Status = Root.PortStatus;
  }

  return Rc;
}

BOOLEAN
RwUsbGetString (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  IN  UINT8                Index,
  OUT CHAR8                *Buf,
  IN  UINTN                BufSize
  )
{
  UINT16      *Langs;
  UINT16      LangBytes;
  CHAR16      *Str;
  EFI_STATUS  Status;

  if ((Buf == NULL) || (BufSize == 0)) {
    return FALSE;
  }

  Buf[0] = '\0';
  if ((Index == 0) || (UsbIo == NULL)) {
    return FALSE;
  }

  Status = UsbIo->UsbGetSupportedLanguages (UsbIo, &Langs, &LangBytes);
  if (EFI_ERROR (Status) || (LangBytes < 2) || (Langs == NULL)) {
    return FALSE;
  }

  Str    = NULL;
  Status = UsbIo->UsbGetStringDescriptor (UsbIo, Langs[0], Index, &Str);
  if (EFI_ERROR (Status) || (Str == NULL)) {
    return FALSE;
  }

  RwUnicodeToAscii (Str, Buf, BufSize);
  FreePool (Str);
  return TRUE;
}

//
// ---- Discovery -----------------------------------------------------------------
//

STATIC
VOID
ScanControllers (
  IN OUT RW_USB_SCAN  *Scan
  )
{
  EFI_HANDLE           *Handles;
  UINTN                Count;
  UINTN                Index;
  RW_USB_HC            *H;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT8                ClassCode[3];
  UINT16               Ids[2];
  UINT8                MaxSpeed;
  UINT8                Ports;
  UINT8                Is64;
  CONST CHAR8          *Kind;

  Scan->HcCount = 0;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiUsb2HcProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (Index = 0; (Index < Count) && (Scan->HcCount < RW_USB_MAX_HC); Index++) {
    H = &Scan->Hcs[Scan->HcCount];
    ZeroMem (H, sizeof (*H));
    H->Handle = Handles[Index];
    if (EFI_ERROR (gBS->HandleProtocol (H->Handle, &gEfiUsb2HcProtocolGuid, (VOID **)&H->Hc))) {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (H->Handle, &gEfiDevicePathProtocolGuid, (VOID **)&H->Dp))) {
      continue;
    }

    H->DpSize = GetDevicePathSize (H->Dp) - END_DEVICE_PATH_LENGTH;
    if (!EFI_ERROR (H->Hc->GetCapability (H->Hc, &MaxSpeed, &Ports, &Is64))) {
      H->MaxSpeed = MaxSpeed;
      H->Ports    = Ports;
    }

    Kind = "USB Host Controller";
    if (!EFI_ERROR (gBS->HandleProtocol (H->Handle, &gEfiPciIoProtocolGuid, (VOID **)&PciIo))) {
      ZeroMem (ClassCode, sizeof (ClassCode));
      ZeroMem (Ids, sizeof (Ids));
      PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_CLASSCODE_OFFSET, 3, ClassCode);
      PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0, 2, Ids);
      H->ProgIf   = ClassCode[0];
      H->VendorId = Ids[0];
      H->DeviceId = Ids[1];
      switch (ClassCode[0]) {
        case 0x00: Kind = "UHCI Host Controller";
          break;
        case 0x10: Kind = "OHCI Host Controller";
          break;
        case 0x20: Kind = "EHCI Host Controller";
          break;
        case 0x30: Kind = "xHCI Host Controller";
          break;
        default:
          break;
      }

      if (!EFI_ERROR (PciIo->GetLocation (PciIo, &H->Seg, &H->Bus, &H->Dev, &H->Fn))) {
        H->HasPci = TRUE;
        AsciiSPrint (
          H->Pci,
          sizeof (H->Pci),
          "PCI %04X:%02X:%02X.%X  [%04X:%04X]",
          (UINT32)H->Seg,
          (UINT32)H->Bus,
          (UINT32)H->Dev,
          (UINT32)H->Fn,
          Ids[0],
          Ids[1]
          );
      }
    }

    AsciiStrCpyS (H->Name, sizeof (H->Name), Kind);
    Scan->HcCount++;
  }

  FreePool (Handles);
}

STATIC
VOID
ScanDevices (
  IN OUT RW_USB_SCAN  *Scan
  )
{
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  UINTN                         HcIdx;
  UINTN                         Other;
  EFI_DEVICE_PATH_PROTOCOL      *Dp;
  EFI_DEVICE_PATH_PROTOCOL      *Node;
  EFI_USB_IO_PROTOCOL           *UsbIo;
  RW_USB_DEV                    Tmp;
  RW_USB_DEV                    *D;
  UINT8                         Hub[8];
  EFI_USB_INTERFACE_DESCRIPTOR  IfDesc;

  Scan->DevCount = 0;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)))
    {
      continue;
    }

    //
    // Which controller is this device under?
    //
    for (HcIdx = 0; HcIdx < Scan->HcCount; HcIdx++) {
      if ((GetDevicePathSize (Dp) > Scan->Hcs[HcIdx].DpSize) &&
          (CompareMem (Dp, Scan->Hcs[HcIdx].Dp, Scan->Hcs[HcIdx].DpSize) == 0))
      {
        break;
      }
    }

    if (HcIdx == Scan->HcCount) {
      continue;
    }

    ZeroMem (&Tmp, sizeof (Tmp));
    Tmp.Hc     = HcIdx;
    Tmp.Parent = RW_USB_NO_INDEX;
    Node       = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Dp + Scan->Hcs[HcIdx].DpSize);
    while (!IsDevicePathEnd (Node) &&
           (DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
           (DevicePathSubType (Node) == MSG_USB_DP) &&
           (Tmp.Depth < RW_USB_MAX_DEPTH))
    {
      Tmp.Port[Tmp.Depth++] = ((USB_DEVICE_PATH *)Node)->ParentPortNumber;
      Node                  = NextDevicePathNode (Node);
    }

    if (Tmp.Depth == 0) {
      continue;
    }

    //
    // One USB I/O handle exists per interface: merge them into one device.
    //
    for (Other = 0; Other < Scan->DevCount; Other++) {
      if ((Scan->Devs[Other].Hc == Tmp.Hc) && (Scan->Devs[Other].Depth == Tmp.Depth) &&
          (CompareMem (Scan->Devs[Other].Port, Tmp.Port, Tmp.Depth) == 0))
      {
        break;
      }
    }

    if (Other < Scan->DevCount) {
      Scan->Devs[Other].Interfaces++;
      continue;
    }

    if (Scan->DevCount == RW_USB_MAX_DEV) {
      break;
    }

    D             = &Scan->Devs[Scan->DevCount];
    *D            = Tmp;
    D->Handle     = Handles[Index];
    D->UsbIo      = UsbIo;
    D->Dp         = Dp;
    D->Interfaces = 1;
    D->Speed      = RW_USB_SPEED_UNKNOWN;
    UsbIo->UsbGetDeviceDescriptor (UsbIo, (EFI_USB_DEVICE_DESCRIPTOR *)D->Desc);
    D->Vid     = ReadUnaligned16 ((UINT16 *)&D->Desc[8]);
    D->Pid     = ReadUnaligned16 ((UINT16 *)&D->Desc[10]);
    D->IfClass = D->Desc[4];
    if (!EFI_ERROR (UsbIo->UsbGetInterfaceDescriptor (UsbIo, &IfDesc))) {
      if (D->Desc[4] == 0x00) {
        D->IfClass = IfDesc.InterfaceClass;
      }
    }

    D->IsHub = (BOOLEAN)(D->Desc[4] == 0x09);
    if (D->IsHub) {
      D->SsHub = (BOOLEAN)(ReadUnaligned16 ((UINT16 *)&D->Desc[2]) >= 0x0300);
      ZeroMem (Hub, sizeof (Hub));
      if (!EFI_ERROR (UsbIn (UsbIo, 0xA0, 0x06, (UINT16)((D->SsHub ? 0x2A : 0x29) << 8), 0, Hub, sizeof (Hub)))) {
        D->HubPorts = Hub[2];
      }
    }

    if (!RwUsbGetString (UsbIo, D->Desc[15], D->Product, sizeof (D->Product))) {
      AsciiStrCpyS (D->Product, sizeof (D->Product), RwUsbClassName (D->IfClass));
    }

    Scan->DevCount++;
  }

  FreePool (Handles);
}

/**
  Parents, and device speeds from the port each device hangs off.
**/
STATIC
VOID
ResolveSpeeds (
  IN OUT RW_USB_SCAN  *Scan
  )
{
  UINTN       Index;
  UINTN       Parent;
  RW_USB_DEV  *D;
  RW_USB_DEV  *P;
  UINT16      Status;

  for (Index = 0; Index < Scan->DevCount; Index++) {
    D = &Scan->Devs[Index];
    if (D->Depth == 1) {
      if (!EFI_ERROR (RwUsbRootPortStatus (&Scan->Hcs[D->Hc], D->Port[0], &Status))) {
        D->Speed = RwUsbSpeedFromRootStatus (Status);
      }

      continue;
    }

    for (Parent = 0; Parent < Scan->DevCount; Parent++) {
      P = &Scan->Devs[Parent];
      if ((P->Hc == D->Hc) && (P->Depth == D->Depth - 1) &&
          (CompareMem (P->Port, D->Port, D->Depth - 1) == 0))
      {
        D->Parent = (INTN)Parent;
        if (P->IsHub && !EFI_ERROR (RwUsbHubPortStatus (P->UsbIo, D->Port[D->Depth - 1], &Status))) {
          D->Speed = RwUsbSpeedFromHubStatus (P->SsHub, Status);
        }

        break;
      }
    }
  }
}

VOID
RwUsbScanFree (
  IN OUT RW_USB_SCAN  *Scan
  )
{
  if (Scan == NULL) {
    return;
  }

  if (Scan->Hcs != NULL) {
    FreePool (Scan->Hcs);
  }

  if (Scan->Devs != NULL) {
    FreePool (Scan->Devs);
  }

  ZeroMem (Scan, sizeof (*Scan));
}

EFI_STATUS
RwUsbScan (
  OUT RW_USB_SCAN  *Scan
  )
{
  if (Scan == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Scan, sizeof (*Scan));
  Scan->Hcs  = AllocateZeroPool (RW_USB_MAX_HC * sizeof (RW_USB_HC));
  Scan->Devs = AllocateZeroPool (RW_USB_MAX_DEV * sizeof (RW_USB_DEV));
  if ((Scan->Hcs == NULL) || (Scan->Devs == NULL)) {
    RwUsbScanFree (Scan);
    return EFI_OUT_OF_RESOURCES;
  }

  ScanControllers (Scan);
  ScanDevices (Scan);
  ResolveSpeeds (Scan);
  return EFI_SUCCESS;
}

INTN
RwUsbFindChild (
  IN CONST RW_USB_SCAN  *Scan,
  IN UINTN              Hc,
  IN CONST UINT8        *Chain  OPTIONAL,
  IN UINTN              Depth,
  IN UINT8              Port
  )
{
  UINTN             Index;
  CONST RW_USB_DEV  *D;

  if ((Scan == NULL) || (Depth >= RW_USB_MAX_DEPTH) || ((Depth > 0) && (Chain == NULL))) {
    return RW_USB_NO_INDEX;
  }

  for (Index = 0; Index < Scan->DevCount; Index++) {
    D = &Scan->Devs[Index];
    if ((D->Hc == Hc) && (D->Depth == Depth + 1) &&
        ((Depth == 0) || (CompareMem (D->Port, Chain, Depth) == 0)) &&
        (D->Port[Depth] == Port))
    {
      return (INTN)Index;
    }
  }

  return RW_USB_NO_INDEX;
}

//
// ---- Hot-plug tracking --------------------------------------------------------
//
// A scan is compared with the previous one by position (controller handle,
// port chain) and VID:PID.
//

UINT64
RwUsbNowSeconds (
  VOID
  )
{
  EFI_TIME  T;

  if (EFI_ERROR (gRT->GetTime (&T, NULL))) {
    return 0;
  }

  return ((((UINT64)T.Year * 12 + T.Month) * 31 + T.Day) * 24 + T.Hour) * 3600 + (UINT64)T.Minute * 60 + T.Second;
}

VOID
RwUsbTrackFromDev (
  IN  CONST RW_USB_SCAN  *Scan,
  IN  UINTN              DevIndex,
  OUT RW_USB_TRACK       *Track
  )
{
  CONST RW_USB_DEV  *D;

  ZeroMem (Track, sizeof (*Track));
  if ((Scan == NULL) || (DevIndex >= Scan->DevCount)) {
    return;
  }

  D               = &Scan->Devs[DevIndex];
  Track->HcHandle = Scan->Hcs[D->Hc].Handle;
  Track->Depth    = D->Depth;
  CopyMem (Track->Port, D->Port, sizeof (Track->Port));
  Track->Vid = D->Vid;
  Track->Pid = D->Pid;
  AsciiSPrint (Track->Label, sizeof (Track->Label), "[%04X:%04X] %a", Track->Vid, Track->Pid, D->Product);
}

BOOLEAN
RwUsbSameDevice (
  IN CONST RW_USB_TRACK  *A,
  IN CONST RW_USB_TRACK  *B
  )
{
  return (BOOLEAN)((A->HcHandle == B->HcHandle) && (A->Depth == B->Depth) && (A->Depth <= RW_USB_MAX_DEPTH) &&
                   (CompareMem (A->Port, B->Port, A->Depth) == 0) && (A->Vid == B->Vid) && (A->Pid == B->Pid));
}

STATIC
BOOLEAN
TrackIn (
  IN CONST RW_USB_TRACK  *T,
  IN CONST RW_USB_TRACK  *List,
  IN UINTN               Count
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (RwUsbSameDevice (T, &List[Index])) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Tracking keys of every device of a scan (FreePool). NULL when the scan is
  empty or out of memory (Count tells which).
**/
STATIC
RW_USB_TRACK *
TracksOf (
  IN  CONST RW_USB_SCAN  *Scan,
  OUT EFI_STATUS         *Status
  )
{
  RW_USB_TRACK  *Tracks;
  UINTN         Index;

  *Status = EFI_SUCCESS;
  if ((Scan == NULL) || (Scan->DevCount == 0)) {
    return NULL;
  }

  Tracks = AllocateZeroPool (Scan->DevCount * sizeof (RW_USB_TRACK));
  if (Tracks == NULL) {
    *Status = EFI_OUT_OF_RESOURCES;
    return NULL;
  }

  for (Index = 0; Index < Scan->DevCount; Index++) {
    RwUsbTrackFromDev (Scan, Index, &Tracks[Index]);
  }

  return Tracks;
}

VOID
RwUsbChangesFree (
  IN OUT RW_USB_CHANGES  *Changes
  )
{
  if (Changes == NULL) {
    return;
  }

  if (Changes->New != NULL) {
    FreePool (Changes->New);
  }

  if (Changes->Gone != NULL) {
    FreePool (Changes->Gone);
  }

  ZeroMem (Changes, sizeof (*Changes));
}

EFI_STATUS
RwUsbCompareScans (
  IN  CONST RW_USB_SCAN  *Old,
  IN  CONST RW_USB_SCAN  *New,
  OUT RW_USB_CHANGES     *Changes
  )
{
  RW_USB_TRACK  *OldT;
  RW_USB_TRACK  *NewT;
  UINTN         OldCount;
  UINTN         NewCount;
  UINTN         Index;
  EFI_STATUS    S1;
  EFI_STATUS    S2;

  if ((Old == NULL) || (New == NULL) || (Changes == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Changes, sizeof (*Changes));
  OldCount = Old->DevCount;
  NewCount = New->DevCount;
  OldT     = TracksOf (Old, &S1);
  NewT     = TracksOf (New, &S2);
  if (EFI_ERROR (S1) || EFI_ERROR (S2)) {
    goto Oom;
  }

  if (NewCount > 0) {
    Changes->New = AllocateZeroPool (NewCount * sizeof (UINTN));
    if (Changes->New == NULL) {
      goto Oom;
    }
  }

  if (OldCount > 0) {
    Changes->Gone = AllocateZeroPool (OldCount * sizeof (RW_USB_TRACK));
    if (Changes->Gone == NULL) {
      goto Oom;
    }
  }

  for (Index = 0; Index < NewCount; Index++) {
    if (!TrackIn (&NewT[Index], OldT, OldCount)) {
      Changes->New[Changes->NewCount++] = Index;
    }
  }

  for (Index = 0; Index < OldCount; Index++) {
    if (!TrackIn (&OldT[Index], NewT, NewCount)) {
      Changes->Gone[Changes->GoneCount++] = OldT[Index];
    }
  }

  if (OldT != NULL) {
    FreePool (OldT);
  }

  if (NewT != NULL) {
    FreePool (NewT);
  }

  return EFI_SUCCESS;

Oom:
  if (OldT != NULL) {
    FreePool (OldT);
  }

  if (NewT != NULL) {
    FreePool (NewT);
  }

  RwUsbChangesFree (Changes);
  return EFI_OUT_OF_RESOURCES;
}

VOID
RwUsbHotplugInit (
  OUT RW_USB_HOTPLUG  *Tracker
  )
{
  ZeroMem (Tracker, sizeof (*Tracker));
}

VOID
RwUsbHotplugFree (
  IN OUT RW_USB_HOTPLUG  *Tracker
  )
{
  if (Tracker == NULL) {
    return;
  }

  if (Tracker->Prev != NULL) {
    FreePool (Tracker->Prev);
  }

  if (Tracker->New != NULL) {
    FreePool (Tracker->New);
  }

  if (Tracker->Gone != NULL) {
    FreePool (Tracker->Gone);
  }

  ZeroMem (Tracker, sizeof (*Tracker));
}

EFI_STATUS
RwUsbHotplugUpdate (
  IN OUT RW_USB_HOTPLUG     *Tracker,
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINT64             Now,
  IN     UINT64             HoldSeconds
  )
{
  RW_USB_TRACK  *Cur;
  UINTN         CurCount;
  UINTN         Index;
  UINTN         Keep;
  EFI_STATUS    Status;

  if ((Tracker == NULL) || (Scan == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Tracker->Prev == NULL) {
    Tracker->Prev = AllocateZeroPool (RW_USB_MAX_DEV * sizeof (RW_USB_TRACK));
    Tracker->New  = AllocateZeroPool (RW_USB_MAX_DEV * sizeof (RW_USB_TRACK));
    Tracker->Gone = AllocateZeroPool (RW_USB_MAX_DEV * sizeof (RW_USB_TRACK));
    if ((Tracker->Prev == NULL) || (Tracker->New == NULL) || (Tracker->Gone == NULL)) {
      RwUsbHotplugFree (Tracker);
      return EFI_OUT_OF_RESOURCES;
    }
  }

  CurCount = MIN (Scan->DevCount, RW_USB_MAX_DEV);
  Cur      = TracksOf (Scan, &Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Tracker->HavePrev) {
    for (Index = 0; Index < CurCount; Index++) {
      if (!TrackIn (&Cur[Index], Tracker->Prev, Tracker->PrevCount) && (Tracker->NewCount < RW_USB_MAX_DEV)) {
        Tracker->New[Tracker->NewCount]       = Cur[Index];
        Tracker->New[Tracker->NewCount].Since = Now;
        Tracker->NewCount++;
      }
    }

    for (Index = 0; Index < Tracker->PrevCount; Index++) {
      if (!TrackIn (&Tracker->Prev[Index], Cur, CurCount) && (Tracker->GoneCount < RW_USB_MAX_DEV)) {
        Tracker->Gone[Tracker->GoneCount]       = Tracker->Prev[Index];
        Tracker->Gone[Tracker->GoneCount].Since = Now;
        Tracker->GoneCount++;
      }
    }
  }

  //
  // Drop marks older than HoldSeconds, a "new" mark whose device left, and
  // a "removed" mark whose device came back.
  //
  for (Index = 0, Keep = 0; Index < Tracker->NewCount; Index++) {
    if (TrackIn (&Tracker->New[Index], Cur, CurCount) && (Now - Tracker->New[Index].Since < HoldSeconds)) {
      Tracker->New[Keep++] = Tracker->New[Index];
    }
  }

  Tracker->NewCount = Keep;
  for (Index = 0, Keep = 0; Index < Tracker->GoneCount; Index++) {
    if (!TrackIn (&Tracker->Gone[Index], Cur, CurCount) && (Now - Tracker->Gone[Index].Since < HoldSeconds)) {
      Tracker->Gone[Keep++] = Tracker->Gone[Index];
    }
  }

  Tracker->GoneCount = Keep;
  if (CurCount > 0) {
    CopyMem (Tracker->Prev, Cur, CurCount * sizeof (RW_USB_TRACK));
  }

  Tracker->PrevCount = CurCount;
  Tracker->HavePrev  = TRUE;
  if (Cur != NULL) {
    FreePool (Cur);
  }

  return EFI_SUCCESS;
}

BOOLEAN
RwUsbHotplugIsNew (
  IN CONST RW_USB_HOTPLUG  *Tracker,
  IN CONST RW_USB_SCAN     *Scan,
  IN UINTN                 DevIndex
  )
{
  RW_USB_TRACK  T;

  if ((Tracker == NULL) || (Scan == NULL) || (DevIndex >= Scan->DevCount)) {
    return FALSE;
  }

  RwUsbTrackFromDev (Scan, DevIndex, &T);
  return TrackIn (&T, Tracker->New, Tracker->NewCount);
}

CONST RW_USB_TRACK *
RwUsbHotplugGoneAt (
  IN CONST RW_USB_HOTPLUG  *Tracker,
  IN CONST RW_USB_SCAN     *Scan,
  IN UINTN                 Hc,
  IN CONST UINT8           *Chain  OPTIONAL,
  IN UINTN                 Depth,
  IN UINT8                 Port
  )
{
  UINTN               Index;
  CONST RW_USB_TRACK  *G;

  if ((Tracker == NULL) || (Scan == NULL) || (Hc >= Scan->HcCount) || (Depth >= RW_USB_MAX_DEPTH) ||
      ((Depth > 0) && (Chain == NULL)))
  {
    return NULL;
  }

  for (Index = 0; Index < Tracker->GoneCount; Index++) {
    G = &Tracker->Gone[Index];
    if ((G->HcHandle == Scan->Hcs[Hc].Handle) && (G->Depth == Depth + 1) &&
        ((Depth == 0) || (CompareMem (G->Port, Chain, Depth) == 0)) &&
        (G->Port[Depth] == Port))
    {
      return G;
    }
  }

  return NULL;
}

//
// ---- Tree ------------------------------------------------------------------------
//

STATIC
RW_USB_NODE *
TreeAdd (
  IN OUT RW_USB_TREE  *Tree,
  IN     UINT8        Kind,
  IN     INT32        Parent,
  IN     UINT8        Level,
  IN     INT32        Hc
  )
{
  RW_USB_NODE  *Grown;
  UINTN        NewCap;
  RW_USB_NODE  *N;

  if (Tree->Oom) {
    return NULL;
  }

  if (Tree->Count == Tree->Capacity) {
    NewCap = (Tree->Capacity == 0) ? 64 : Tree->Capacity * 2;
    Grown  = ReallocatePool (Tree->Capacity * sizeof (RW_USB_NODE), NewCap * sizeof (RW_USB_NODE), Tree->Nodes);
    if (Grown == NULL) {
      Tree->Oom = TRUE;
      return NULL;
    }

    Tree->Nodes    = Grown;
    Tree->Capacity = NewCap;
  }

  N = &Tree->Nodes[Tree->Count++];
  ZeroMem (N, sizeof (*N));
  N->Kind   = Kind;
  N->Parent = Parent;
  N->Level  = Level;
  N->Hc     = Hc;
  N->Dev    = -1;
  return N;
}

STATIC
VOID
AddPorts (
  IN OUT RW_USB_TREE           *Tree,
  IN     CONST RW_USB_SCAN     *Scan,
  IN     CONST RW_USB_HOTPLUG  *Tracker  OPTIONAL,
  IN     UINTN                 Hc,
  IN     CONST UINT8           *Chain    OPTIONAL,
  IN     UINTN                 Depth,
  IN     UINTN                 PortCount,
  IN     INTN                  HubDev,
  IN     INT32                 ParentNode,
  IN     UINT8                 Level
  )
{
  UINTN               Port;
  UINTN               Last;
  INTN                Child;
  CONST RW_USB_DEV    *D;
  UINT16              Status;
  BOOLEAN             Connected;
  BOOLEAN             Read;
  CONST RW_USB_TRACK  *Gone;
  RW_USB_NODE         *N;
  INT32               PortNode;
  BOOLEAN             IsNew;

  //
  // When a hub would not report its port count, cover every port a child
  // device is known to sit on.
  //
  Last = PortCount;
  for (Port = 0; Port < 256; Port++) {
    if ((RwUsbFindChild (Scan, Hc, Chain, Depth, (UINT8)Port) >= 0) && (Port + 1 > Last)) {
      Last = Port + 1;
    }
  }

  for (Port = 0; Port < Last; Port++) {
    PortNode = (INT32)Tree->Count;
    N        = TreeAdd (Tree, RwUsbNodePort, ParentNode, Level, (INT32)Hc);
    if (N == NULL) {
      return;
    }

    N->PortNumber = (UINT8)(Port + 1);
    AsciiSPrint (N->Name, sizeof (N->Name), "Port %d", (UINT32)(Port + 1));

    Child = RwUsbFindChild (Scan, Hc, Chain, Depth, (UINT8)Port);
    if (Child >= 0) {
      D            = &Scan->Devs[Child];
      IsNew        = RwUsbHotplugIsNew (Tracker, Scan, (UINTN)Child);
      N->State     = RwUsbPortDevice;
      N->Dev       = (INT32)Child;
      N->IsNew     = IsNew;
      N->Emphasis  = IsNew ? RwEmStrong : RwEmNormal;
      AsciiSPrint (N->Value, sizeof (N->Value), "[%04X:%04X] %a", D->Vid, D->Pid, D->Product);

      N = TreeAdd (Tree, RwUsbNodeDevice, PortNode, (UINT8)(Level + 1), (INT32)Hc);
      if (N == NULL) {
        return;
      }

      N->Dev        = (INT32)Child;
      N->PortNumber = (UINT8)(Port + 1);
      N->IsNew      = IsNew;
      N->Emphasis   = IsNew ? RwEmStrong : RwEmNormal;
      AsciiSPrint (N->Name, sizeof (N->Name), "[%04X:%04X]", D->Vid, D->Pid);
      AsciiSPrint (
        N->Value,
        sizeof (N->Value),
        "%a  (%a, %a)%a",
        D->Product,
        D->IsHub ? "Hub" : RwUsbClassName (D->IfClass),
        RwUsbSpeedShort (D->Speed),
        IsNew ? "  [new]" : ""
        );
      if (D->IsHub && (D->Depth < RW_USB_MAX_DEPTH)) {
        AddPorts (Tree, Scan, Tracker, Hc, D->Port, D->Depth, D->HubPorts, Child, (INT32)(Tree->Count - 1), (UINT8)(Level + 2));
      }

      continue;
    }

    Gone = RwUsbHotplugGoneAt (Tracker, Scan, Hc, Chain, Depth, (UINT8)Port);
    if (Gone != NULL) {
      N->State    = RwUsbPortRemoved;
      N->Emphasis = RwEmAlert;
      AsciiSPrint (N->Value, sizeof (N->Value), "%a  [removed]", Gone->Label);
      continue;
    }

    Connected = FALSE;
    Read      = FALSE;
    if (HubDev < 0) {
      Read = (BOOLEAN)!EFI_ERROR (RwUsbRootPortStatus (&Scan->Hcs[Hc], (UINT8)Port, &Status));
      if (Read) {
        Connected = (BOOLEAN)((Status & USB_PORT_STAT_CONNECTION) != 0);
      }
    } else {
      Read = (BOOLEAN)!EFI_ERROR (RwUsbHubPortStatus (Scan->Devs[HubDev].UsbIo, (UINT8)Port, &Status));
      if (Read) {
        Connected = (BOOLEAN)((Status & BIT0) != 0);
      }
    }

    N->State    = Connected ? RwUsbPortConnected : (Read ? RwUsbPortEmpty : RwUsbPortUnknown);
    N->Emphasis = Connected ? RwEmAlert : RwEmDim;
    AsciiStrCpyS (
      N->Value,
      sizeof (N->Value),
      Connected ? "Device connected (not enumerated by firmware)" : "No device connected"
      );
  }
}

VOID
RwUsbTreeFree (
  IN OUT RW_USB_TREE  *Tree
  )
{
  if (Tree == NULL) {
    return;
  }

  if (Tree->Nodes != NULL) {
    FreePool (Tree->Nodes);
  }

  ZeroMem (Tree, sizeof (*Tree));
}

EFI_STATUS
RwUsbBuildTree (
  IN  CONST RW_USB_SCAN     *Scan,
  IN  CONST RW_USB_HOTPLUG  *Tracker  OPTIONAL,
  OUT RW_USB_TREE           *Tree
  )
{
  UINTN        Hc;
  RW_USB_NODE  *N;

  if ((Scan == NULL) || (Tree == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Tree, sizeof (*Tree));
  for (Hc = 0; Hc < Scan->HcCount; Hc++) {
    N = TreeAdd (Tree, RwUsbNodeController, -1, 0, (INT32)Hc);
    if (N == NULL) {
      break;
    }

    AsciiStrCpyS (N->Name, sizeof (N->Name), Scan->Hcs[Hc].Name);
    AsciiSPrint (
      N->Value,
      sizeof (N->Value),
      "%a%aRoot Hub  (%d ports, max %a)",
      Scan->Hcs[Hc].Pci,
      (Scan->Hcs[Hc].Pci[0] != '\0') ? "  " : "",
      Scan->Hcs[Hc].Ports,
      RwUsbSpeedName (Scan->Hcs[Hc].MaxSpeed)
      );
    AddPorts (Tree, Scan, Tracker, Hc, NULL, 0, Scan->Hcs[Hc].Ports, RW_USB_NO_INDEX, (INT32)(Tree->Count - 1), 1);
  }

  return Tree->Oom ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

VOID
RwUsbTreeToInfo (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     CONST RW_USB_TREE  *Tree,
  IN OUT RW_INFO            *Info
  )
{
  UINTN              Index;
  CONST RW_USB_NODE  *N;
  CONST RW_USB_NODE  *Dn;
  CONST RW_USB_DEV   *D;
  RW_INFO_ROW        *Row;
  UINT8              Level;

  if ((Scan == NULL) || (Tree == NULL) || (Info == NULL)) {
    return;
  }

  if (Scan->HcCount == 0) {
    RwInfoText (Info, 0, RwEmAlert, "No USB host controller (EFI_USB2_HC_PROTOCOL) found.");
    return;
  }

  for (Index = 0; Index < Tree->Count; Index++) {
    N = &Tree->Nodes[Index];
    switch (N->Kind) {
      case RwUsbNodeController:
        Row = RwInfoSection (Info, 0, "%a  %a", Scan->Hcs[N->Hc].Name, Scan->Hcs[N->Hc].Pci);
        RwInfoSetTag (Row, RW_USB_HC_TAG_BASE + N->Hc);
        RwInfoText (
          Info,
          1,
          RwEmStrong,
          "Root Hub  (%d ports, max %a)",
          Scan->Hcs[N->Hc].Ports,
          RwUsbSpeedName (Scan->Hcs[N->Hc].MaxSpeed)
          );
        break;

      case RwUsbNodePort:
        //
        // Root ports 4 spaces in, each hub tier 4 more (uefirrw layout).
        //
        Level = (UINT8)(N->Level + 1);
        if ((N->State == RwUsbPortDevice) && (N->Dev >= 0) && (Index + 1 < Tree->Count)) {
          Dn  = &Tree->Nodes[Index + 1];
          D   = &Scan->Devs[N->Dev];
          Row = RwInfoText (
                  Info,
                  Level,
                  Dn->Emphasis,
                  "Port %d: [%04X:%04X] %a  (%a, %a)%a",
                  N->PortNumber,
                  D->Vid,
                  D->Pid,
                  D->Product,
                  D->IsHub ? "Hub" : RwUsbClassName (D->IfClass),
                  RwUsbSpeedShort (D->Speed),
                  Dn->IsNew ? "  [new]" : ""
                  );
          RwInfoSetTag (Row, N->Dev);
        } else {
          RwInfoText (Info, Level, N->Emphasis, "Port %d: %a", N->PortNumber, N->Value);
        }

        break;

      default:
        //
        // Device nodes are folded into their port's row.
        //
        break;
    }
  }
}

//
// ---- Descriptor decoding ---------------------------------------------------------
//

STATIC
VOID
AddStrField (
  IN OUT RW_INFO              *Info,
  IN     EFI_USB_IO_PROTOCOL  *UsbIo  OPTIONAL,
  IN     CONST CHAR8          *Name,
  IN     UINT8                Index,
  IN     UINT32               Offset
  )
{
  CHAR8        Str[128];
  RW_INFO_ROW  *Row;

  if ((Index != 0) && (UsbIo != NULL) && RwUsbGetString (UsbIo, Index, Str, sizeof (Str))) {
    Row = RwInfoField (Info, 1, RwEmNormal, Name, "%02Xh  \"%a\"", Index, Str);
  } else {
    Row = RwInfoField (Info, 1, RwEmNormal, Name, "%02Xh", Index);
  }

  RwInfoSetReg (Row, Offset, 1);
}

STATIC
VOID
Field8 (
  IN OUT RW_INFO      *Info,
  IN     CONST CHAR8  *Name,
  IN     CONST UINT8  *Base,
  IN     UINT32       Offset
  )
{
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, Name, "%02Xh", Base[Offset]), Offset, 1);
}

STATIC
VOID
Field16 (
  IN OUT RW_INFO      *Info,
  IN     CONST CHAR8  *Name,
  IN     CONST UINT8  *Base,
  IN     UINT32       Offset
  )
{
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, Name, "%04Xh", ReadUnaligned16 ((CONST UINT16 *)&Base[Offset])), Offset, 2);
}

VOID
RwUsbDecodeDeviceDescriptor (
  IN     EFI_USB_IO_PROTOCOL  *UsbIo  OPTIONAL,
  IN     CONST UINT8          *Dd,
  IN OUT RW_INFO              *Info
  )
{
  UINT16       Bcd;
  RW_INFO_ROW  *Row;

  if ((Dd == NULL) || (Info == NULL)) {
    return;
  }

  Bcd = ReadUnaligned16 ((CONST UINT16 *)&Dd[2]);
  RwInfoSetReg (RwInfoSection (Info, 0, "Device Descriptor"), 0, 18);
  Field8 (Info, "bLength", Dd, 0);
  Field8 (Info, "bDescriptorType", Dd, 1);
  Row = RwInfoField (Info, 1, RwEmNormal, "bcdUSB", "%04Xh  (USB %x.%02x)", Bcd, Bcd >> 8, Bcd & 0xFF);
  RwInfoSetReg (Row, 2, 2);
  Row = RwInfoField (Info, 1, RwEmNormal, "bDeviceClass", "%02Xh  %a", Dd[4], RwUsbClassName (Dd[4]));
  RwInfoSetReg (Row, 4, 1);
  Field8 (Info, "bDeviceSubClass", Dd, 5);
  Field8 (Info, "bDeviceProtocol", Dd, 6);
  Row = RwInfoField (
          Info,
          1,
          RwEmNormal,
          "bMaxPacketSize0",
          "%02Xh  (%d bytes)",
          Dd[7],
          (Bcd >= 0x0300) ? ((Dd[7] < 16) ? (1 << Dd[7]) : 0) : Dd[7]
          );
  RwInfoSetReg (Row, 7, 1);
  Field16 (Info, "idVendor", Dd, 8);
  Field16 (Info, "idProduct", Dd, 10);
  Field16 (Info, "bcdDevice", Dd, 12);
  AddStrField (Info, UsbIo, "iManufacturer", Dd[14], 14);
  AddStrField (Info, UsbIo, "iProduct", Dd[15], 15);
  AddStrField (Info, UsbIo, "iSerialNumber", Dd[16], 16);
  Field8 (Info, "bNumConfigurations", Dd, 17);
}

VOID
RwUsbDecodeConfig (
  IN     EFI_USB_IO_PROTOCOL  *UsbIo  OPTIONAL,
  IN     CONST UINT8          *Cfg,
  IN     UINTN                Total,
  IN     BOOLEAN              SuperSpeed,
  IN OUT RW_INFO              *Info
  )
{
  UINTN        Off;
  UINT8        Len;
  UINT8        Type;
  CONST UINT8  *P;
  UINT32       O;
  RW_INFO_ROW  *Row;

  if ((Cfg == NULL) || (Info == NULL)) {
    return;
  }

  for (Off = 0; Off + 2 <= Total; Off += Len) {
    P    = Cfg + Off;
    Len  = P[0];
    Type = P[1];
    O    = (UINT32)Off;
    if ((Len < 2) || (Off + Len > Total)) {
      break;
    }

    switch (Type) {
      case 0x02:
        RwInfoSetReg (RwInfoSection (Info, 0, "Configuration Descriptor"), O, Len);
        if (Len < 9) {
          RwInfoAddHex (Info, 1, P, Len, Off);
          break;
        }

        Field16 (Info, "wTotalLength", Cfg, O + 2);
        Field8 (Info, "bNumInterfaces", Cfg, O + 4);
        Field8 (Info, "bConfigurationValue", Cfg, O + 5);
        AddStrField (Info, UsbIo, "iConfiguration", P[6], O + 6);
        Row = RwInfoField (
                Info,
                1,
                RwEmNormal,
                "bmAttributes",
                "%02Xh  %a%a",
                P[7],
                (P[7] & BIT6) ? "Self Powered" : "Bus Powered",
                (P[7] & BIT5) ? ", Remote Wakeup" : ""
                );
        RwInfoSetReg (Row, O + 7, 1);
        Row = RwInfoField (Info, 1, RwEmNormal, "bMaxPower", "%02Xh  (%d mA)", P[8], P[8] * (SuperSpeed ? 8 : 2));
        RwInfoSetReg (Row, O + 8, 1);
        break;

      case 0x04:
        RwInfoSetReg (RwInfoSection (Info, 0, "Interface Descriptor  (Interface %d, Alternate %d)", P[2], (Len > 3) ? P[3] : 0), O, Len);
        if (Len < 9) {
          RwInfoAddHex (Info, 1, P, Len, Off);
          break;
        }

        Field8 (Info, "bInterfaceNumber", Cfg, O + 2);
        Field8 (Info, "bAlternateSetting", Cfg, O + 3);
        Field8 (Info, "bNumEndpoints", Cfg, O + 4);
        Row = RwInfoField (Info, 1, RwEmNormal, "bInterfaceClass", "%02Xh  %a", P[5], RwUsbClassName (P[5]));
        RwInfoSetReg (Row, O + 5, 1);
        Field8 (Info, "bInterfaceSubClass", Cfg, O + 6);
        Field8 (Info, "bInterfaceProtocol", Cfg, O + 7);
        AddStrField (Info, UsbIo, "iInterface", P[8], O + 8);
        break;

      case 0x05:
        RwInfoSetReg (RwInfoSection (Info, 0, "Endpoint Descriptor  (EP %d %a)", P[2] & 0x0F, (P[2] & BIT7) ? "IN" : "OUT"), O, Len);
        if (Len < 7) {
          RwInfoAddHex (Info, 1, P, Len, Off);
          break;
        }

        Row = RwInfoField (Info, 1, RwEmNormal, "bEndpointAddress", "%02Xh  EP %d %a", P[2], P[2] & 0x0F, (P[2] & BIT7) ? "IN" : "OUT");
        RwInfoSetReg (Row, O + 2, 1);
        Row = RwInfoField (Info, 1, RwEmNormal, "bmAttributes", "%02Xh  %a", P[3], mXferType[P[3] & 3]);
        RwInfoSetReg (Row, O + 3, 1);
        Row = RwInfoField (
                Info,
                1,
                RwEmNormal,
                "wMaxPacketSize",
                "%04Xh  (%d bytes)",
                ReadUnaligned16 ((CONST UINT16 *)&P[4]),
                ReadUnaligned16 ((CONST UINT16 *)&P[4]) & 0x7FF
                );
        RwInfoSetReg (Row, O + 4, 2);
        Field8 (Info, "bInterval", Cfg, O + 6);
        break;

      case 0x0B:
        RwInfoSetReg (RwInfoSection (Info, 0, "Interface Association Descriptor"), O, Len);
        if (Len < 7) {
          RwInfoAddHex (Info, 1, P, Len, Off);
          break;
        }

        Field8 (Info, "bFirstInterface", Cfg, O + 2);
        Field8 (Info, "bInterfaceCount", Cfg, O + 3);
        Row = RwInfoField (Info, 1, RwEmNormal, "bFunctionClass", "%02Xh  %a", P[4], RwUsbClassName (P[4]));
        RwInfoSetReg (Row, O + 4, 1);
        Field8 (Info, "bFunctionSubClass", Cfg, O + 5);
        Field8 (Info, "bFunctionProtocol", Cfg, O + 6);
        if (Len > 7) {
          AddStrField (Info, UsbIo, "iFunction", P[7], O + 7);
        }

        break;

      case 0x21:
        RwInfoSetReg (RwInfoSection (Info, 0, "HID Descriptor"), O, Len);
        if (Len >= 9) {
          Field16 (Info, "bcdHID", Cfg, O + 2);
          Field8 (Info, "bCountryCode", Cfg, O + 4);
          Field8 (Info, "bNumDescriptors", Cfg, O + 5);
          Row = RwInfoField (Info, 1, RwEmNormal, "bDescriptorType", "%02Xh  %a", P[6], (P[6] == 0x22) ? "Report" : "");
          RwInfoSetReg (Row, O + 6, 1);
          Row = RwInfoField (
                  Info,
                  1,
                  RwEmNormal,
                  "wDescriptorLength",
                  "%04Xh  (%d bytes)",
                  ReadUnaligned16 ((CONST UINT16 *)&P[7]),
                  ReadUnaligned16 ((CONST UINT16 *)&P[7])
                  );
          RwInfoSetReg (Row, O + 7, 2);
        }

        break;

      case 0x30:
        RwInfoSetReg (RwInfoSection (Info, 0, "SuperSpeed Endpoint Companion Descriptor"), O, Len);
        if (Len >= 6) {
          Field8 (Info, "bMaxBurst", Cfg, O + 2);
          Field8 (Info, "bmAttributes", Cfg, O + 3);
          Field16 (Info, "wBytesPerInterval", Cfg, O + 4);
        }

        break;

      default:
        RwInfoSetReg (RwInfoSection (Info, 0, "Descriptor Type %02Xh  (%d bytes)", Type, Len), O, Len);
        RwInfoAddHex (Info, 1, P, Len, Off);
        break;
    }
  }
}

EFI_STATUS
RwUsbReadConfigSet (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  OUT UINT8                **Cfg,
  OUT UINT16               *Total
  )
{
  EFI_USB_CONFIG_DESCRIPTOR  Cfg9;
  UINT8                      *Buf;
  EFI_STATUS                 Status;

  if ((UsbIo == NULL) || (Cfg == NULL) || (Total == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Cfg   = NULL;
  *Total = 0;
  Status = UsbIo->UsbGetConfigDescriptor (UsbIo, &Cfg9);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  if (Cfg9.TotalLength < 9) {
    return EFI_DEVICE_ERROR;
  }

  Buf = AllocateZeroPool (Cfg9.TotalLength);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = UsbIn (
             UsbIo,
             0x80,
             0x06,
             (UINT16)((0x02 << 8) | (Cfg9.ConfigurationValue ? Cfg9.ConfigurationValue - 1 : 0)),
             0,
             Buf,
             Cfg9.TotalLength
             );
  if (EFI_ERROR (Status) || (Buf[1] != 0x02)) {
    FreePool (Buf);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  *Cfg   = Buf;
  *Total = Cfg9.TotalLength;
  return EFI_SUCCESS;
}

STATIC
VOID
AddDevicePath (
  IN OUT RW_INFO                   *Info,
  IN     EFI_DEVICE_PATH_PROTOCOL  *Dp
  )
{
  CHAR16  *DpText;

  if (Dp == NULL) {
    return;
  }

  DpText = ConvertDevicePathToText (Dp, TRUE, TRUE);
  if (DpText != NULL) {
    RwInfoField (Info, 1, RwEmNormal, "Device Path", "%s", DpText);
    FreePool (DpText);
  }
}

VOID
RwUsbDecodeDevice (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINTN              DevIndex,
  IN OUT RW_INFO            *Info
  )
{
  CONST RW_USB_DEV  *D;
  CONST RW_USB_HC   *H;
  RW_STR            Path;
  UINTN             Index;
  UINT8             *Cfg;
  UINT16            Total;
  UINT16            Status;
  EFI_STATUS        Rc;
  CHAR8             Name[16];
  CHAR8             Header[160];

  if ((Scan == NULL) || (Info == NULL) || (DevIndex >= Scan->DevCount)) {
    return;
  }

  D = &Scan->Devs[DevIndex];
  H = &Scan->Hcs[D->Hc];
  AsciiSPrint (Header, sizeof (Header), "[%04X:%04X] %a", D->Vid, D->Pid, D->Product);

  RwStrInit (&Path);
  RwStrAppendF (&Path, "Root port %d", D->Port[0] + 1);
  for (Index = 1; Index < D->Depth; Index++) {
    RwStrAppendF (&Path, " > hub port %d", D->Port[Index] + 1);
  }

  RwInfoAdd (Info, RwKindSection, 0, RwEmNormal, "Connection", Header);
  RwInfoField (Info, 1, RwEmNormal, "Host Controller", "%a  %a", H->Name, H->Pci);
  RwInfoField (Info, 1, RwEmNormal, "Port Path", "%a", Path.Buf);
  RwInfoField (Info, 1, RwEmStrong, "Speed", "%a", RwUsbSpeedName (D->Speed));
  RwInfoField (Info, 1, RwEmNormal, "Interfaces (handles)", "%d", (UINT32)D->Interfaces);
  AddDevicePath (Info, D->Dp);
  RwStrFree (&Path);

  RwUsbDecodeDeviceDescriptor (D->UsbIo, D->Desc, Info);

  //
  // Full configuration descriptor set of the active configuration.
  //
  Cfg   = NULL;
  Total = 0;
  Rc    = RwUsbReadConfigSet (D->UsbIo, &Cfg, &Total);
  if (!EFI_ERROR (Rc)) {
    RwUsbDecodeConfig (D->UsbIo, Cfg, Total, (BOOLEAN)(D->Speed == EFI_USB_SPEED_SUPER), Info);
  } else if (Rc != EFI_NOT_FOUND) {
    RwInfoText (Info, 0, RwEmDim, "Configuration descriptor set could not be read.");
  }

  if (D->IsHub) {
    RwInfoSection (Info, 0, "Hub  (%d downstream ports%a)", D->HubPorts, D->SsHub ? ", SuperSpeed" : "");
    for (Index = 0; Index < D->HubPorts; Index++) {
      AsciiSPrint (Name, sizeof (Name), "Port %d", (UINT32)(Index + 1));
      if (EFI_ERROR (RwUsbHubPortStatus (D->UsbIo, (UINT8)Index, &Status))) {
        RwInfoField (Info, 1, RwEmDim, Name, "status unavailable");
        continue;
      }

      RwInfoField (
        Info,
        1,
        (Status & BIT3) ? RwEmAlert : RwEmNormal,
        Name,
        "%04Xh  %a%a%a%a",
        Status,
        (Status & BIT0) ? "Connected" : "Empty",
        (Status & BIT1) ? ", Enabled" : "",
        (Status & BIT3) ? ", Over-current" : "",
        ((Status & BIT0) && !D->SsHub) ? ((Status & BIT10) ? ", High Speed" : ((Status & BIT9) ? ", Low Speed" : ", Full Speed")) : ""
        );
    }
  }

  RwInfoSetReg (RwInfoSection (Info, 0, "Raw Device Descriptor"), 0, 18);
  RwInfoAddHex (Info, 1, D->Desc, 18, 0);
  if (Cfg != NULL) {
    RwInfoSetReg (RwInfoSection (Info, 0, "Raw Configuration Descriptor Set (%d bytes)", Total), 0, Total);
    RwInfoAddHex (Info, 1, Cfg, Total, 0);
    FreePool (Cfg);
  }
}

VOID
RwUsbDecodeController (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINTN              HcIndex,
  IN OUT RW_INFO            *Info
  )
{
  RW_USB_HC  *H;
  UINTN      Port;
  UINT16     St;
  CHAR8      Name[16];

  if ((Scan == NULL) || (Info == NULL) || (HcIndex >= Scan->HcCount)) {
    return;
  }

  H = &Scan->Hcs[HcIndex];
  RwInfoAdd (Info, RwKindSection, 0, RwEmNormal, "Host Controller", H->Name);
  RwInfoField (Info, 1, RwEmNormal, "Type", "%a", H->Name);
  RwInfoField (Info, 1, RwEmNormal, "Location", "%a", H->Pci);
  RwInfoField (Info, 1, RwEmNormal, "Max Speed", "%a", RwUsbSpeedName (H->MaxSpeed));
  RwInfoField (Info, 1, RwEmNormal, "Root Hub Ports", "%d", H->Ports);
  AddDevicePath (Info, H->Dp);

  RwInfoSection (Info, 0, "Root Hub Port Status");
  for (Port = 0; Port < H->Ports; Port++) {
    AsciiSPrint (Name, sizeof (Name), "Port %2d", (UINT32)(Port + 1));
    if (EFI_ERROR (RwUsbRootPortStatus (H, (UINT8)Port, &St))) {
      RwInfoField (Info, 1, RwEmDim, Name, "status unavailable");
      continue;
    }

    RwInfoField (
      Info,
      1,
      (St & USB_PORT_STAT_OVERCURRENT) ? RwEmAlert : RwEmNormal,
      Name,
      "%04Xh  %a%a%a%a%a",
      St,
      (St & USB_PORT_STAT_CONNECTION) ? "Connected" : "Empty",
      (St & USB_PORT_STAT_ENABLE) ? ", Enabled" : "",
      (St & USB_PORT_STAT_OVERCURRENT) ? ", Over-current" : "",
      (St & USB_PORT_STAT_OWNER) ? ", Companion owned" : "",
      (St & USB_PORT_STAT_CONNECTION) ? ((St & USB_PORT_STAT_SUPER_SPEED) ? ", SuperSpeed" : ((St & USB_PORT_STAT_HIGH_SPEED) ? ", High Speed" : ((St & USB_PORT_STAT_LOW_SPEED) ? ", Low Speed" : ", Full Speed"))) : ""
      );
  }
}
