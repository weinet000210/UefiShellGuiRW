/** @file
  RwCoreLib PCI: configuration space access through
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL (so segments and ECAM work), bus
  enumeration, the bridge tree, and header / capability decoding.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWPCI_H_
#define RW_RWPCI_H_

#include <Library/RwCore/RwBase.h>
#include <Protocol/PciRootBridgeIo.h>

#define RW_PCI_NO_PCIE    0xFF
#define RW_PCI_NO_PARENT  (-1)

typedef struct {
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL    *RootBridge;
  UINT32                             Segment;
  UINT8                              BusMin;
  UINT8                              BusMax;
} RW_PCI_ROOT;

typedef struct {
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL    *RootBridge;
  UINT32                             RootIndex;    // index into RW_PCI_LIST.Roots
  UINT32                             Segment;
  UINT8                              Bus;
  UINT8                              Dev;
  UINT8                              Func;
  UINT8                              HeaderType;   // with the multi-function bit
  UINT16                             VendorId;
  UINT16                             DeviceId;
  UINT8                              RevisionId;
  UINT8                              BaseClass;
  UINT8                              SubClass;
  UINT8                              ProgIf;
  UINT8                              SecBus;       // bridges only, else 0
  UINT8                              SubBus;
  UINT8                              PcieType;     // device/port type, RW_PCI_NO_PCIE without a PCIe capability
  UINT8                              PcieCapPtr;   // 0 without a PCIe capability
  UINT8                              LinkSpeed;    // LnkSta speed code, 0 = no link
  UINT8                              LinkWidth;
  UINT8                              MaxSpeed;     // LnkCap
  UINT8                              MaxWidth;
  BOOLEAN                            ExtConfig;    // 4 KB config space reachable
  INT32                              Parent;       // index of the bridge above, RW_PCI_NO_PARENT on a root bus
  UINT8                              Depth;        // bridges between the root bus and this device
} RW_PCI_DEVICE;

typedef struct {
  RW_PCI_ROOT      *Roots;
  UINTN            RootCount;
  RW_PCI_DEVICE    *Devices;      // in bus / device / function order per root bridge
  UINTN            DeviceCount;
} RW_PCI_LIST;

/**
  Enumerate every root bridge and every function on its buses.
  The list is caller-owned; release it with RwPciFree.

  @retval EFI_NOT_FOUND         no PCI root bridge (List is empty).
  @retval EFI_OUT_OF_RESOURCES  some devices were dropped.
**/
EFI_STATUS
RwPciScan (
  OUT RW_PCI_LIST  *List
  );

VOID
RwPciFree (
  IN OUT RW_PCI_LIST  *List
  );

/**
  Config read / write. Reg is 0..FFFh, Width 1, 2 or 4.
**/
EFI_STATUS
RwPciCfgRead (
  IN  CONST RW_PCI_DEVICE  *Device,
  IN  UINT32               Reg,
  IN  UINTN                Width,
  OUT UINT64               *Value
  );

EFI_STATUS
RwPciCfgWrite (
  IN CONST RW_PCI_DEVICE  *Device,
  IN UINT32               Reg,
  IN UINTN                Width,
  IN UINT64               Value
  );

/**
  Config read that returns all ones on error (for decoders).
**/
UINT32
RwPciRd (
  IN CONST RW_PCI_DEVICE  *Device,
  IN UINT32               Reg,
  IN UINTN                Width
  );

/**
  TRUE for a PCI-to-PCI or CardBus bridge with a valid secondary bus.
**/
BOOLEAN
RwPciIsBridge (
  IN CONST RW_PCI_DEVICE  *Device
  );

/**
  Device indexes in tree order: per root bridge, each root-bus device
  followed by everything behind it (depth first). Order holds DeviceCount
  entries. Returns the number written.
**/
UINTN
RwPciTreeOrder (
  IN  CONST RW_PCI_LIST  *List,
  OUT UINTN              *Order
  );

/**
  "Host Bridge", "SATA Controller", ... ("Other" when unknown).
**/
CONST CHAR8 *
RwPciClassName (
  IN UINT8  BaseClass,
  IN UINT8  SubClass
  );

/**
  "Root Port", "Endpoint", ... or NULL for RW_PCI_NO_PCIE / reserved types.
**/
CONST CHAR8 *
RwPciPcieTypeName (
  IN UINT8  PcieType
  );

/**
  "2.5 GT/s" ... "64.0 GT/s" for a link speed code, "?" otherwise.
**/
CONST CHAR8 *
RwPciSpeedName (
  IN UINTN  Code
  );

CONST CHAR8 *
RwPciCapName (
  IN UINT8  Id
  );

CONST CHAR8 *
RwPciExtCapName (
  IN UINT16  Id
  );

/**
  "PCI 0000:00:1F.0  8086:2918  ISA Bridge"
**/
VOID
RwPciDescribe (
  IN  CONST RW_PCI_DEVICE  *Device,
  OUT CHAR8                *Buffer,
  IN  UINTN                BufferSize
  );

/**
  The device information page: header, BARs, bridge windows, capabilities
  and extended capabilities, decoded.
**/
VOID
RwPciDecode (
  IN     CONST RW_PCI_DEVICE  *Device,
  IN OUT RW_INFO              *Info
  );

/**
  The bridge tree as rows: a section per root bridge, one text row per
  device (Level = depth + 1, Tag = device index).
**/
VOID
RwPciDecodeTree (
  IN     CONST RW_PCI_LIST  *List,
  IN OUT RW_INFO            *Info
  );

/**
  Dump All: device list, tree and every device's information page.
**/
VOID
RwPciDump (
  IN     CONST RW_PCI_LIST  *List,
  IN OUT RW_INFO            *Info
  );

/**
  Config space of Device as a data source: 256 bytes, or 4 KB when the
  extended space is reachable. Writes to BARs, the expansion ROM BAR, bridge
  bus numbers and bridge windows are dangerous.
**/
VOID
RwPciSourceInit (
  OUT RW_DATA_SOURCE       *Source,
  IN  CONST RW_PCI_DEVICE  *Device
  );

#endif
