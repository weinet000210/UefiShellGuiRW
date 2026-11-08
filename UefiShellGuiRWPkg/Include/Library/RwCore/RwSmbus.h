/** @file
  RwCoreLib SMBus module: the chipset SMBus host controller (Intel ICH/PCH
  and AMD SB/FCH, both with the PIIX4 register layout), device scan, byte
  read / write, and SPD EEPROMs of DDR3, DDR4 and DDR5 modules (paging and
  decode).

  Only the controller's own I/O registers are touched. Writes to SMBus
  devices can make a memory module unusable (SPD) or change clocks; views
  treat every SMBus write as dangerous.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWSMBUS_H_
#define RW_RWSMBUS_H_

#include <Library/RwCore/RwBase.h>
#include <Library/RwCore/RwPci.h>

#define RW_SMBUS_INTEL  1
#define RW_SMBUS_AMD    2

#define RW_SMBUS_FIRST_ADDR  0x08
#define RW_SMBUS_LAST_ADDR   0x77

typedef struct {
  UINT8      Kind;          // RW_SMBUS_INTEL / RW_SMBUS_AMD
  UINT16     Base;          // I/O base of the host registers
  UINT16     VendorId;
  UINT16     DeviceId;
  UINT32     Segment;
  UINT8      Bus;
  UINT8      Dev;
  UINT8      Func;
  BOOLEAN    HostEnabled;   // Intel HOSTC.HST_EN (always TRUE for AMD)
  BOOLEAN    IoDecode;      // PCI command I/O space enable
  RW_PCI_DEVICE  Pci;       // the controller's function
  CHAR8      Name[64];      // "Intel SMBus 8086:2930 at 00:1F.3, I/O 0700h"
} RW_SMBUS;

/**
  Find the SMBus host controller (PCI class 0C05h from Intel or AMD).

  @retval EFI_NOT_FOUND     no supported controller.
  @retval EFI_NOT_READY     found, but its I/O base is not set or does not answer
                            (Bus is filled so the caller can say which).
**/
EFI_STATUS
RwSmbusFind (
  OUT RW_SMBUS  *Bus
  );

/**
  Turn on what RwSmbusFind reported off: the controller's I/O decode (PCI
  command bit 0) and, on Intel, HOSTC.HST_EN. Firmware often leaves them off
  after boot; this is a configuration change, views ask first.

  @retval EFI_NOT_READY  still not usable (no I/O base).
**/
EFI_STATUS
RwSmbusEnable (
  IN OUT RW_SMBUS  *Bus
  );

/**
  Read Byte Data: Command is the register (offset) of device Addr (7-bit).

  @retval EFI_NO_RESPONSE   no device acknowledged the address.
  @retval EFI_TIMEOUT       the controller did not finish.
  @retval EFI_DEVICE_ERROR  bus collision or failed transaction.
**/
EFI_STATUS
RwSmbusReadByte (
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr,
  IN  UINT8           Command,
  OUT UINT8           *Value
  );

EFI_STATUS
RwSmbusWriteByte (
  IN CONST RW_SMBUS  *Bus,
  IN UINT8           Addr,
  IN UINT8           Command,
  IN UINT8           Value
  );

/**
  Quick Write (address only), used to select DDR4 SPD pages (36h / 37h).
**/
EFI_STATUS
RwSmbusQuickWrite (
  IN CONST RW_SMBUS  *Bus,
  IN UINT8           Addr
  );

/**
  Addresses RW_SMBUS_FIRST_ADDR..RW_SMBUS_LAST_ADDR that answer a Read Byte
  of register 0. Present[Addr] is set for each; returns the count.
**/
UINTN
RwSmbusScan (
  IN  CONST RW_SMBUS  *Bus,
  OUT BOOLEAN         Present[128]
  );

/**
  What usually sits at Addr ("SPD EEPROM", "Thermal sensor", ...), "" when
  nothing typical.
**/
CONST CHAR8 *
RwSmbusAddrHint (
  IN UINT8  Addr
  );

/**
  The 256 registers of one SMBus device as a data source (byte width,
  reads talk to the device, every write is dangerous).
**/
VOID
RwSmbusSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr
  );

//
// ---- SPD -------------------------------------------------------------------
//
#define RW_SPD_MAX  1024

#define RW_SPD_DDR3  0x0B
#define RW_SPD_DDR4  0x0C
#define RW_SPD_DDR5  0x12

typedef struct {
  UINT8    Addr;
  UINT8    Type;              // SPD byte 2 (RW_SPD_*), 0 for a blank EEPROM
  UINTN    Size;              // bytes read into Data
  UINT8    Data[RW_SPD_MAX];
} RW_SPD;

/**
  Read the whole SPD of the module at Addr (50h-57h): 256 bytes for DDR3 and
  unknown types, 512 for DDR4 (page select through 36h / 37h, page 0
  restored), 1024 for DDR5 (SPD5 hub MR11 paging, page 0 restored).
**/
EFI_STATUS
RwSpdRead (
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr,
  OUT RW_SPD          *Spd
  );

/** "DDR4 SDRAM", "DDR5 SDRAM", "Unknown (00h)". **/
VOID
RwSpdTypeName (
  IN  UINT8  Type,
  OUT CHAR8  *Buffer,
  IN  UINTN  BufferSize
  );

/** JEDEC JEP106 manufacturer (bank byte with parity, ID byte), NULL when unknown. **/
CONST CHAR8 *
RwSpdManufacturer (
  IN UINT8  Bank,
  IN UINT8  Id
  );

/** CRC-16 (polynomial 1021h, initial 0) as SPD uses. **/
UINT16
RwSpdCrc16 (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  );

/**
  One line: "DDR4 SO-DIMM 8 GB 3200 MT/s  Samsung M471A1K43DB1-CWE", or
  "Blank SPD" / "Unknown SPD type 07h".
**/
VOID
RwSpdSummary (
  IN  CONST RW_SPD  *Spd,
  OUT CHAR8         *Buffer,
  IN  UINTN         BufferSize
  );

/**
  Decode: type, module type, capacity, organisation, speed and main
  timings, voltage, manufacturer, part number, serial, date, CRC; then the
  raw bytes. Field rows carry their SPD offsets.
**/
VOID
RwSpdDecode (
  IN     CONST RW_SPD  *Spd,
  IN OUT RW_INFO       *Info
  );

/**
  The SPD bytes read by RwSpdRead as a read-only source (Spd must stay
  valid while the source is used).
**/
VOID
RwSpdSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_SPD    *Spd
  );

#endif
