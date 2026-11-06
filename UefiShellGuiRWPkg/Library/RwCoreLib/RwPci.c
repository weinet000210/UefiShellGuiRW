/** @file
  RwCoreLib PCI: configuration access, enumeration, tree and data source.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci.h>

#define MAX_PCI_ROOTS    32
#define PCI_GROW         64

STATIC_ASSERT (sizeof (RW_PCI_DEVICE) <= RW_DS_CTX_MAX, "RW_PCI_DEVICE must fit a data source context");

STATIC
UINT64
PciAddress (
  IN UINT8   Bus,
  IN UINT8   Dev,
  IN UINT8   Func,
  IN UINT32  Reg
  )
{
  UINT64  Addr;

  Addr = EFI_PCI_ADDRESS (Bus, Dev, Func, 0);
  if (Reg < 0x100) {
    Addr |= Reg;
  } else {
    Addr |= LShiftU64 (Reg, 32);   // ExtendedRegister field
  }

  return Addr;
}

STATIC
EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH
PciWidth (
  IN UINTN  Width
  )
{
  return (Width == 1) ? EfiPciWidthUint8 : ((Width == 2) ? EfiPciWidthUint16 : EfiPciWidthUint32);
}

STATIC
EFI_STATUS
CfgRead (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Rb,
  IN  UINT8                            Bus,
  IN  UINT8                            Dev,
  IN  UINT8                            Func,
  IN  UINT32                           Reg,
  IN  UINTN                            Width,
  OUT UINT64                           *Value
  )
{
  *Value = 0;
  if ((Rb == NULL) || (Reg > 0xFFF) || ((Width != 1) && (Width != 2) && (Width != 4))) {
    return EFI_INVALID_PARAMETER;
  }

  return Rb->Pci.Read (Rb, PciWidth (Width), PciAddress (Bus, Dev, Func, Reg), 1, Value);
}

EFI_STATUS
RwPciCfgRead (
  IN  CONST RW_PCI_DEVICE  *Device,
  IN  UINT32               Reg,
  IN  UINTN                Width,
  OUT UINT64               *Value
  )
{
  return CfgRead (Device->RootBridge, Device->Bus, Device->Dev, Device->Func, Reg, Width, Value);
}

EFI_STATUS
RwPciCfgWrite (
  IN CONST RW_PCI_DEVICE  *Device,
  IN UINT32               Reg,
  IN UINTN                Width,
  IN UINT64               Value
  )
{
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Rb;

  Rb = Device->RootBridge;
  if ((Rb == NULL) || (Reg > 0xFFF) || ((Width != 1) && (Width != 2) && (Width != 4))) {
    return EFI_INVALID_PARAMETER;
  }

  return Rb->Pci.Write (Rb, PciWidth (Width), PciAddress (Device->Bus, Device->Dev, Device->Func, Reg), 1, &Value);
}

UINT32
RwPciRd (
  IN CONST RW_PCI_DEVICE  *Device,
  IN UINT32               Reg,
  IN UINTN                Width
  )
{
  UINT64  Val;

  if (EFI_ERROR (RwPciCfgRead (Device, Reg, Width, &Val))) {
    return (Width == 4) ? 0xFFFFFFFF : ((Width == 2) ? 0xFFFF : 0xFF);
  }

  return (UINT32)Val;
}

BOOLEAN
RwPciIsBridge (
  IN CONST RW_PCI_DEVICE  *Device
  )
{
  UINT8  Type;

  Type = Device->HeaderType & 0x7F;
  return (BOOLEAN)(((Type == 1) || (Type == 2)) && (Device->SecBus > Device->Bus));
}

STATIC
VOID
BusRange (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Rb,
  OUT UINTN                            *Min,
  OUT UINTN                            *Max
  )
{
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Desc;
  VOID                               *Config;

  *Min = 0;
  *Max = 255;
  if (EFI_ERROR (Rb->Configuration (Rb, &Config)) || (Config == NULL)) {
    return;
  }

  Desc = (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *)Config;
  while (Desc->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) {
    if (Desc->ResType == ACPI_ADDRESS_SPACE_TYPE_BUS) {
      *Min = (UINTN)Desc->AddrRangeMin;
      *Max = (UINTN)Desc->AddrRangeMax;
      if (*Max > 255) {
        *Max = 255;
      }

      if (*Min > *Max) {
        *Min = *Max;
      }

      return;
    }

    Desc++;
  }
}

/**
  Bridge bus numbers and PCI Express port type / link state.
**/
STATIC
VOID
ProbeExtra (
  IN OUT RW_PCI_DEVICE  *E
  )
{
  UINT64  Val;
  UINT8   Ptr;
  UINTN   Guard;
  UINT16  Lnk;

  E->PcieType   = RW_PCI_NO_PCIE;
  E->PcieCapPtr = 0;
  E->SecBus     = 0;
  E->SubBus     = 0;
  E->LinkSpeed  = 0;
  E->LinkWidth  = 0;
  E->MaxSpeed   = 0;
  E->MaxWidth   = 0;
  if (((E->HeaderType & 0x7F) == 1) || ((E->HeaderType & 0x7F) == 2)) {
    RwPciCfgRead (E, 0x19, 1, &Val);
    E->SecBus = (UINT8)Val;
    RwPciCfgRead (E, 0x1A, 1, &Val);
    E->SubBus = (UINT8)Val;
  }

  E->ExtConfig = (BOOLEAN)(!EFI_ERROR (RwPciCfgRead (E, 0x100, 4, &Val)) && ((UINT32)Val != 0xFFFFFFFF));

  RwPciCfgRead (E, PCI_PRIMARY_STATUS_OFFSET, 2, &Val);
  if ((Val & EFI_PCI_STATUS_CAPABILITY) == 0) {
    return;
  }

  RwPciCfgRead (E, ((E->HeaderType & 0x7F) == 2) ? 0x14 : PCI_CAPBILITY_POINTER_OFFSET, 1, &Val);
  Ptr = (UINT8)Val & 0xFC;
  for (Guard = 0; (Ptr >= 0x40) && (Guard < 48); Guard++) {
    RwPciCfgRead (E, Ptr, 2, &Val);
    if ((UINT8)Val == EFI_PCI_CAPABILITY_ID_PCIEXP) {
      E->PcieCapPtr = Ptr;
      RwPciCfgRead (E, Ptr + 2, 2, &Val);
      E->PcieType = (UINT8)((Val >> 4) & 0x0F);
      if ((E->PcieType == 9) || (E->PcieType == 0x0A)) {
        return;     // integrated endpoint / event collector: no link
      }

      RwPciCfgRead (E, Ptr + 0x0C, 2, &Val);
      E->MaxSpeed = (UINT8)(Val & 0x0F);
      E->MaxWidth = (UINT8)((Val >> 4) & 0x3F);
      RwPciCfgRead (E, Ptr + 0x12, 2, &Val);
      Lnk          = (UINT16)Val;
      E->LinkSpeed = (UINT8)(Lnk & 0x0F);
      E->LinkWidth = (UINT8)((Lnk >> 4) & 0x3F);
      return;
    }

    Ptr = (UINT8)(Val >> 8) & 0xFC;
  }
}

STATIC
RW_PCI_DEVICE *
NewDevice (
  IN OUT RW_PCI_LIST  *List,
  IN OUT UINTN        *Capacity
  )
{
  RW_PCI_DEVICE  *Grown;

  if (List->DeviceCount == *Capacity) {
    Grown = ReallocatePool (
              *Capacity * sizeof (RW_PCI_DEVICE),
              (*Capacity + PCI_GROW) * sizeof (RW_PCI_DEVICE),
              List->Devices
              );
    if (Grown == NULL) {
      return NULL;
    }

    List->Devices = Grown;
    *Capacity    += PCI_GROW;
  }

  return &List->Devices[List->DeviceCount++];
}

/**
  Parent links: a device's parent is the bridge under the same root bridge
  whose secondary bus is the device's bus.
**/
STATIC
VOID
LinkTree (
  IN OUT RW_PCI_LIST  *List
  )
{
  UINTN          Index;
  UINTN          Scan;
  RW_PCI_DEVICE  *E;
  RW_PCI_DEVICE  *B;
  INT32          Up;
  UINTN          Guard;

  for (Index = 0; Index < List->DeviceCount; Index++) {
    E         = &List->Devices[Index];
    E->Parent = RW_PCI_NO_PARENT;
    for (Scan = 0; Scan < List->DeviceCount; Scan++) {
      B = &List->Devices[Scan];
      if ((Scan != Index) && (B->RootIndex == E->RootIndex) && RwPciIsBridge (B) && (B->SecBus == E->Bus)) {
        E->Parent = (INT32)Scan;
        break;
      }
    }
  }

  for (Index = 0; Index < List->DeviceCount; Index++) {
    E        = &List->Devices[Index];
    E->Depth = 0;
    Up       = E->Parent;
    for (Guard = 0; (Up != RW_PCI_NO_PARENT) && (Guard < 32); Guard++) {
      E->Depth++;
      Up = List->Devices[Up].Parent;
    }

    if (Guard == 32) {
      E->Parent = RW_PCI_NO_PARENT;   // broken bus numbering: a loop
      E->Depth  = 0;
    }
  }
}

EFI_STATUS
RwPciScan (
  OUT RW_PCI_LIST  *List
  )
{
  EFI_STATUS                       Status;
  EFI_STATUS                       Result;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            HIndex;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Rb;
  UINTN                            BusMin;
  UINTN                            BusMax;
  UINTN                            Bus;
  UINTN                            Dev;
  UINTN                            Func;
  UINT64                           Id;
  UINT64                           ClassReg;
  UINT64                           Header;
  RW_PCI_DEVICE                    *E;
  UINTN                            Capacity;

  ZeroMem (List, sizeof (*List));
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciRootBridgeIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    return EFI_NOT_FOUND;
  }

  List->Roots = AllocateZeroPool (sizeof (RW_PCI_ROOT) * MIN (HandleCount, MAX_PCI_ROOTS));
  if (List->Roots == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Result   = EFI_SUCCESS;
  Capacity = 0;
  for (HIndex = 0; (HIndex < HandleCount) && (List->RootCount < MAX_PCI_ROOTS); HIndex++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[HIndex], &gEfiPciRootBridgeIoProtocolGuid, (VOID **)&Rb))) {
      continue;
    }

    BusRange (Rb, &BusMin, &BusMax);
    List->Roots[List->RootCount].RootBridge = Rb;
    List->Roots[List->RootCount].Segment    = Rb->SegmentNumber;
    List->Roots[List->RootCount].BusMin     = (UINT8)BusMin;
    List->Roots[List->RootCount].BusMax     = (UINT8)BusMax;
    List->RootCount++;

    for (Bus = BusMin; Bus <= BusMax; Bus++) {
      for (Dev = 0; Dev <= PCI_MAX_DEVICE; Dev++) {
        for (Func = 0; Func <= PCI_MAX_FUNC; Func++) {
          if (EFI_ERROR (CfgRead (Rb, (UINT8)Bus, (UINT8)Dev, (UINT8)Func, 0, 4, &Id))) {
            break;
          }

          if (((UINT16)Id == 0xFFFF) || ((UINT16)Id == 0x0000)) {
            if (Func == 0) {
              break;     // no device: skip remaining functions
            }

            continue;
          }

          CfgRead (Rb, (UINT8)Bus, (UINT8)Dev, (UINT8)Func, PCI_HEADER_TYPE_OFFSET, 1, &Header);
          CfgRead (Rb, (UINT8)Bus, (UINT8)Dev, (UINT8)Func, PCI_REVISION_ID_OFFSET, 4, &ClassReg);

          E = NewDevice (List, &Capacity);
          if (E == NULL) {
            Result = EFI_OUT_OF_RESOURCES;
          } else {
            ZeroMem (E, sizeof (*E));
            E->RootBridge = Rb;
            E->RootIndex  = (UINT32)(List->RootCount - 1);
            E->Segment    = Rb->SegmentNumber;
            E->Bus        = (UINT8)Bus;
            E->Dev        = (UINT8)Dev;
            E->Func       = (UINT8)Func;
            E->VendorId   = (UINT16)Id;
            E->DeviceId   = (UINT16)RShiftU64 (Id, 16);
            E->RevisionId = (UINT8)ClassReg;
            E->ProgIf     = (UINT8)RShiftU64 (ClassReg, 8);
            E->SubClass   = (UINT8)RShiftU64 (ClassReg, 16);
            E->BaseClass  = (UINT8)RShiftU64 (ClassReg, 24);
            E->HeaderType = (UINT8)Header;
            ProbeExtra (E);
          }

          if ((Func == 0) && ((Header & HEADER_TYPE_MULTI_FUNCTION) == 0)) {
            break;   // single-function device
          }
        }
      }
    }
  }

  FreePool (Handles);
  LinkTree (List);
  return Result;
}

VOID
RwPciFree (
  IN OUT RW_PCI_LIST  *List
  )
{
  if (List->Roots != NULL) {
    FreePool (List->Roots);
  }

  if (List->Devices != NULL) {
    FreePool (List->Devices);
  }

  ZeroMem (List, sizeof (*List));
}

STATIC
VOID
TreeVisit (
  IN     CONST RW_PCI_LIST  *List,
  IN     UINTN              Index,
  IN OUT UINTN              *Order,
  IN OUT UINTN              *Count
  )
{
  UINTN  Child;

  Order[(*Count)++] = Index;
  if (!RwPciIsBridge (&List->Devices[Index]) || (List->Devices[Index].Depth >= 31)) {
    return;
  }

  for (Child = 0; Child < List->DeviceCount; Child++) {
    if (List->Devices[Child].Parent == (INT32)Index) {
      TreeVisit (List, Child, Order, Count);
    }
  }
}

UINTN
RwPciTreeOrder (
  IN  CONST RW_PCI_LIST  *List,
  OUT UINTN              *Order
  )
{
  UINTN  Root;
  UINTN  Index;
  UINTN  Count;

  Count = 0;
  for (Root = 0; Root < List->RootCount; Root++) {
    for (Index = 0; Index < List->DeviceCount; Index++) {
      if ((List->Devices[Index].RootIndex == Root) && (List->Devices[Index].Parent == RW_PCI_NO_PARENT)) {
        TreeVisit (List, Index, Order, &Count);
      }
    }
  }

  return Count;
}

//
// ---- Data source ---------------------------------------------------------
//

STATIC
EFI_STATUS
EFIAPI
PciDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  return RwPciCfgRead ((RW_PCI_DEVICE *)Source->Ctx, (UINT32)Address, Width, Value);
}

STATIC
EFI_STATUS
EFIAPI
PciDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  return RwPciCfgWrite ((RW_PCI_DEVICE *)Source->Ctx, (UINT32)Address, Width, Value);
}

STATIC
BOOLEAN
Overlaps (
  IN UINT64  Address,
  IN UINTN   Width,
  IN UINT64  First,
  IN UINT64  Last
  )
{
  return (BOOLEAN)((Address <= Last) && (Address + Width - 1 >= First));
}

/**
  BARs and the expansion ROM BAR move device decoding; bus numbers and
  bridge windows reroute whole buses.
**/
STATIC
BOOLEAN
EFIAPI
PciDsIsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  RW_PCI_DEVICE  *E;

  E = (RW_PCI_DEVICE *)Source->Ctx;
  switch (E->HeaderType & 0x7F) {
    case 0:
      return (BOOLEAN)(Overlaps (Address, Width, 0x10, 0x27) || Overlaps (Address, Width, 0x30, 0x33));
    case 1:
      return (BOOLEAN)(Overlaps (Address, Width, 0x10, 0x2F) || Overlaps (Address, Width, 0x30, 0x33) ||
                       Overlaps (Address, Width, 0x38, 0x3B));
    case 2:
      return (BOOLEAN)(Overlaps (Address, Width, 0x10, 0x13) || Overlaps (Address, Width, 0x18, 0x3B));
    default:
      return FALSE;
  }
}

VOID
RwPciSourceInit (
  OUT RW_DATA_SOURCE       *Source,
  IN  CONST RW_PCI_DEVICE  *Device
  )
{
  ZeroMem (Source, sizeof (*Source));
  CopyMem (Source->Ctx, Device, sizeof (*Device));
  RwPciDescribe (Device, Source->Name, sizeof (Source->Name));
  Source->Base        = 0;
  Source->Limit       = Device->ExtConfig ? 0xFFF : 0xFF;
  Source->AddrDigits  = Device->ExtConfig ? 3 : 2;
  Source->WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32;
  Source->SideEffects = FALSE;
  Source->Read        = PciDsRead;
  Source->Write       = PciDsWrite;
  Source->IsDangerous = PciDsIsDangerous;
}
