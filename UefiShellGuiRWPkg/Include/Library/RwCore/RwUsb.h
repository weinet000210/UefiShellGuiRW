/** @file
  RwCoreLib USB module: host controllers (EFI_USB2_HC_PROTOCOL + PCI I/O),
  the devices the firmware's USB bus driver enumerated (EFI_USB_IO_PROTOCOL),
  root hub / hub port status and speeds, string descriptors, the
  controller -> port -> device tree, descriptor decoding and hot-plug
  tracking between two scans.

  Only standard read requests are issued (GET_DESCRIPTOR, hub GET_STATUS).
  Only devices the firmware enumerated are seen.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWUSB_H_
#define RW_RWUSB_H_

#include <Library/RwCore/RwBase.h>
#include <Protocol/UsbIo.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/DevicePath.h>

#define RW_USB_MAX_HC         16
#define RW_USB_MAX_DEV        128
#define RW_USB_MAX_DEPTH      8       // tiers of the port chain
#define RW_USB_SPEED_UNKNOWN  0xFF    // else EFI_USB_SPEED_FULL/LOW/HIGH/SUPER
#define RW_USB_TIMEOUT_MS     1000
#define RW_USB_NO_INDEX       (-1)

//
// Default hold time of hot-plug marks (uefirrw: 10 s).
//
#define RW_USB_HOTPLUG_SECONDS  10

//
// One host controller.
//
typedef struct {
  EFI_HANDLE                  Handle;
  EFI_USB2_HC_PROTOCOL        *Hc;
  EFI_DEVICE_PATH_PROTOCOL    *Dp;
  UINTN                       DpSize;       // without the end node
  CHAR8                       Name[48];     // "xHCI Host Controller"
  CHAR8                       Pci[48];      // "PCI 0000:00:1D.0  [8086:2934]" or ""
  BOOLEAN                     HasPci;
  UINT8                       ProgIf;       // PCI programming interface (00 UHCI, 10 OHCI, 20 EHCI, 30 xHCI)
  UINT16                      VendorId;
  UINT16                      DeviceId;
  UINTN                       Seg;
  UINTN                       Bus;
  UINTN                       Dev;
  UINTN                       Fn;
  UINT8                       MaxSpeed;     // EFI_USB_SPEED_*
  UINT8                       Ports;        // root hub ports
} RW_USB_HC;

//
// One USB device. The firmware creates one USB I/O handle per interface;
// they are merged into one device (Interfaces counts them).
//
typedef struct {
  UINTN                       Hc;                      // index into RW_USB_SCAN.Hcs
  INTN                        Parent;                  // index of the hub it hangs off, RW_USB_NO_INDEX = root port
  UINTN                       Depth;                   // tiers used in Port[]
  UINT8                       Port[RW_USB_MAX_DEPTH];  // 0-based port per tier
  EFI_HANDLE                  Handle;                  // first interface handle
  EFI_USB_IO_PROTOCOL         *UsbIo;
  EFI_DEVICE_PATH_PROTOCOL    *Dp;
  UINT8                       Desc[18];                // device descriptor, raw
  UINT16                      Vid;
  UINT16                      Pid;
  UINTN                       Interfaces;
  UINT8                       Speed;                   // EFI_USB_SPEED_* or RW_USB_SPEED_UNKNOWN
  UINT8                       HubPorts;
  BOOLEAN                     IsHub;
  BOOLEAN                     SsHub;
  UINT8                       IfClass;                 // device class, or first interface's when 00h
  CHAR8                       Product[64];             // iProduct string, else the class name
} RW_USB_DEV;

//
// A scan: caller-owned, free with RwUsbScanFree.
//
typedef struct {
  RW_USB_HC     *Hcs;
  UINTN         HcCount;
  RW_USB_DEV    *Devs;
  UINTN         DevCount;
} RW_USB_SCAN;

//
// Hot-plug tracking key: the position (controller handle, port chain) and
// VID:PID of a device.
//
typedef struct {
  EFI_HANDLE    HcHandle;
  UINTN         Depth;
  UINT8         Port[RW_USB_MAX_DEPTH];
  UINT16        Vid;
  UINT16        Pid;
  UINT64        Since;      // seconds stamp of the change (tracker only)
  CHAR8         Label[96];  // "[VID:PID] Product"
} RW_USB_TRACK;

//
// Result of comparing two scans: devices that appeared in the new scan and
// devices of the old scan that are gone. Free with RwUsbChangesFree.
//
typedef struct {
  UINTN           *New;        // indexes into the new scan's Devs
  UINTN           NewCount;
  RW_USB_TRACK    *Gone;       // keys of the old scan's devices
  UINTN           GoneCount;
} RW_USB_CHANGES;

//
// Running hot-plug state across repeated scans (uefirrw behaviour): new
// marks and removed marks held for HoldSeconds. Init with RwUsbHotplugInit,
// free with RwUsbHotplugFree.
//
typedef struct {
  RW_USB_TRACK    *Prev;
  UINTN           PrevCount;
  BOOLEAN         HavePrev;
  RW_USB_TRACK    *New;
  UINTN           NewCount;
  RW_USB_TRACK    *Gone;
  UINTN           GoneCount;
} RW_USB_HOTPLUG;

//
// Tree: controller -> ports -> device -> (hub) ports -> device ...
//
typedef enum {
  RwUsbNodeController = 0,
  RwUsbNodePort       = 1,
  RwUsbNodeDevice     = 2
} RW_USB_NODE_KIND;

typedef enum {
  RwUsbPortEmpty      = 0,   // nothing connected
  RwUsbPortConnected  = 1,   // connected but not enumerated by the firmware
  RwUsbPortDevice     = 2,   // a device node follows as child
  RwUsbPortRemoved    = 3,   // a device was unplugged here recently (hot-plug tracker)
  RwUsbPortUnknown    = 4    // status could not be read: reported like Empty (uefirrw)
} RW_USB_PORT_STATE;

typedef struct {
  UINT8     Kind;           // RW_USB_NODE_KIND
  UINT8     State;          // RW_USB_PORT_STATE for port nodes
  UINT8     Emphasis;       // RW_INFO_EMPHASIS
  UINT8     Level;          // 0 = controller
  INT32     Parent;         // node index, -1 for a controller
  INT32     Hc;             // controller index
  INT32     Dev;            // device index for device nodes and occupied ports, else -1
  UINT8     PortNumber;     // 1-based, port nodes
  BOOLEAN   IsNew;          // device nodes / occupied ports: marked new by the tracker
  CHAR8     Name[32];       // "Port 3", "xHCI Host Controller", "[0627:0001]"
  CHAR8     Value[128];     // description
} RW_USB_NODE;

typedef struct {
  RW_USB_NODE    *Nodes;
  UINTN          Count;
  UINTN          Capacity;
  BOOLEAN        Oom;
} RW_USB_TREE;

/**
  Name of a USB class code ("HID", "Mass Storage", "Unknown").
**/
CONST CHAR8 *
RwUsbClassName (
  IN UINT8  Class
  );

/**
  Long speed name ("High Speed (480 Mbps)"), "Unknown" out of range.
**/
CONST CHAR8 *
RwUsbSpeedName (
  IN UINT8  Speed
  );

/**
  Short speed name ("FS", "LS", "HS", "SS"), "--" out of range.
**/
CONST CHAR8 *
RwUsbSpeedShort (
  IN UINT8  Speed
  );

/**
  Speed of a device from its root hub port status (EFI_USB_PORT_STATUS).
**/
UINT8
RwUsbSpeedFromRootStatus (
  IN UINT16  PortStatus
  );

/**
  Speed of a device from its parent hub's wPortStatus. A SuperSpeed hub's
  ports are always SuperSpeed.
**/
UINT8
RwUsbSpeedFromHubStatus (
  IN BOOLEAN  SsHub,
  IN UINT16   PortStatus
  );

/**
  Fetch string descriptor Index in the device's first language as ASCII.

  @retval TRUE   Buf holds the string.
  @retval FALSE  Index 0, no languages or the request failed; Buf is "".
**/
BOOLEAN
RwUsbGetString (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  IN  UINT8                Index,
  OUT CHAR8                *Buf,
  IN  UINTN                BufSize
  );

/**
  Hub class GET_STATUS of a downstream port (0-based) of a hub.
**/
EFI_STATUS
RwUsbHubPortStatus (
  IN  EFI_USB_IO_PROTOCOL  *HubIo,
  IN  UINT8                Port,
  OUT UINT16               *Status
  );

/**
  Root hub port status of a controller's port (0-based).
**/
EFI_STATUS
RwUsbRootPortStatus (
  IN  RW_USB_HC  *Hc,
  IN  UINT8      Port,
  OUT UINT16     *Status
  );

/**
  Scan controllers and devices, merge interfaces, resolve parents and port
  speeds. EFI_SUCCESS also when there is no controller (HcCount = 0).
  Scan is always initialised; free it with RwUsbScanFree.
**/
EFI_STATUS
RwUsbScan (
  OUT RW_USB_SCAN  *Scan
  );

VOID
RwUsbScanFree (
  IN OUT RW_USB_SCAN  *Scan
  );

/**
  Index of the device on port Port (0-based) under the chain Chain[0..Depth-1]
  of controller Hc (Depth 0 = root port), or RW_USB_NO_INDEX.
**/
INTN
RwUsbFindChild (
  IN CONST RW_USB_SCAN  *Scan,
  IN UINTN              Hc,
  IN CONST UINT8        *Chain  OPTIONAL,
  IN UINTN              Depth,
  IN UINT8              Port
  );

/**
  Tracking key of a device of a scan.
**/
VOID
RwUsbTrackFromDev (
  IN  CONST RW_USB_SCAN  *Scan,
  IN  UINTN              DevIndex,
  OUT RW_USB_TRACK       *Track
  );

/**
  Same position and VID:PID.
**/
BOOLEAN
RwUsbSameDevice (
  IN CONST RW_USB_TRACK  *A,
  IN CONST RW_USB_TRACK  *B
  );

/**
  Compare two scans: devices of New not in Old, devices of Old not in New.
  Changes is always initialised; free with RwUsbChangesFree.
**/
EFI_STATUS
RwUsbCompareScans (
  IN  CONST RW_USB_SCAN  *Old,
  IN  CONST RW_USB_SCAN  *New,
  OUT RW_USB_CHANGES     *Changes
  );

VOID
RwUsbChangesFree (
  IN OUT RW_USB_CHANGES  *Changes
  );

VOID
RwUsbHotplugInit (
  OUT RW_USB_HOTPLUG  *Tracker
  );

VOID
RwUsbHotplugFree (
  IN OUT RW_USB_HOTPLUG  *Tracker
  );

/**
  Feed a fresh scan to the tracker (uefirrw UpdateHotplug): marks devices
  that appeared and devices that left since the previous scan with stamp
  Now (seconds, any monotonic base, see RwUsbNowSeconds), then drops marks
  older than HoldSeconds, a "new" mark whose device left again, and a
  "removed" mark whose device came back. The first scan marks nothing.
**/
EFI_STATUS
RwUsbHotplugUpdate (
  IN OUT RW_USB_HOTPLUG     *Tracker,
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINT64             Now,
  IN     UINT64             HoldSeconds
  );

/**
  Seconds stamp from the RTC (gRT->GetTime), 0 when unavailable.
**/
UINT64
RwUsbNowSeconds (
  VOID
  );

/**
  The device carries a "new" mark.
**/
BOOLEAN
RwUsbHotplugIsNew (
  IN CONST RW_USB_HOTPLUG  *Tracker,
  IN CONST RW_USB_SCAN     *Scan,
  IN UINTN                 DevIndex
  );

/**
  A recently removed device that sat on Port (0-based) under Chain of
  controller Hc, or NULL.
**/
CONST RW_USB_TRACK *
RwUsbHotplugGoneAt (
  IN CONST RW_USB_HOTPLUG  *Tracker,
  IN CONST RW_USB_SCAN     *Scan,
  IN UINTN                 Hc,
  IN CONST UINT8           *Chain  OPTIONAL,
  IN UINTN                 Depth,
  IN UINT8                 Port
  );

/**
  Build the tree of a scan. Every root hub port and every hub port appears
  (also ports beyond a hub's reported count that hold a known child), with
  its state; occupied ports have the device as child, hubs their ports.
  Tracker may be NULL (no new / removed marks). Free with RwUsbTreeFree.
**/
EFI_STATUS
RwUsbBuildTree (
  IN  CONST RW_USB_SCAN     *Scan,
  IN  CONST RW_USB_HOTPLUG  *Tracker  OPTIONAL,
  OUT RW_USB_TREE           *Tree
  );

VOID
RwUsbTreeFree (
  IN OUT RW_USB_TREE  *Tree
  );

//
// Tag of controller rows in RwUsbTreeToInfo (device rows: device index).
//
#define RW_USB_HC_TAG_BASE  10000

/**
  The tree as RW_INFO text rows (uefirrw dump layout), one row per node,
  Level = node level; device rows tagged with the device index,
  controller rows with RW_USB_HC_TAG_BASE + controller index.
**/
VOID
RwUsbTreeToInfo (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     CONST RW_USB_TREE  *Tree,
  IN OUT RW_INFO            *Info
  );

/**
  Controller page: type, location, max speed, ports, device path, and the
  status of every root hub port.
**/
VOID
RwUsbDecodeController (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINTN              HcIndex,
  IN OUT RW_INFO            *Info
  );

/**
  Device page: connection, device descriptor, the active configuration's
  full descriptor set, hub port status, raw descriptors. Issues
  GET_DESCRIPTOR and hub GET_STATUS requests.
**/
VOID
RwUsbDecodeDevice (
  IN     CONST RW_USB_SCAN  *Scan,
  IN     UINTN              DevIndex,
  IN OUT RW_INFO            *Info
  );

/**
  Decode an 18-byte device descriptor. UsbIo (optional) resolves string
  indexes. Row offsets are offsets in the device descriptor.
**/
VOID
RwUsbDecodeDeviceDescriptor (
  IN     EFI_USB_IO_PROTOCOL  *UsbIo  OPTIONAL,
  IN     CONST UINT8          *Desc,
  IN OUT RW_INFO              *Info
  );

/**
  Decode a configuration descriptor set (configuration, interface,
  endpoint, IAD, HID, SuperSpeed companion; anything else as hex). UsbIo
  (optional) resolves string indexes. SuperSpeed selects 8 mA bMaxPower
  units. Row offsets are offsets in Cfg.
**/
VOID
RwUsbDecodeConfig (
  IN     EFI_USB_IO_PROTOCOL  *UsbIo  OPTIONAL,
  IN     CONST UINT8          *Cfg,
  IN     UINTN                Total,
  IN     BOOLEAN              SuperSpeed,
  IN OUT RW_INFO              *Info
  );

/**
  Read the full configuration descriptor set of the device's active
  configuration (FreePool it).
**/
EFI_STATUS
RwUsbReadConfigSet (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  OUT UINT8                **Cfg,
  OUT UINT16               *Total
  );

#endif
