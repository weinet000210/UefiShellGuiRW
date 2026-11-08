/** @file
  RwCoreLib simple spaces: physical memory / MMIO, I/O ports, CMOS RAM and
  Embedded Controller RAM, each as an RW_DATA_SOURCE.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWSPACE_H_
#define RW_RWSPACE_H_

#include <Library/RwCore/RwBase.h>

//
// Default ACPI EC ports (ACPI 6.5 section 12.3).
//
#define RW_EC_DEFAULT_DATA  0x62
#define RW_EC_DEFAULT_CMD   0x66

/**
  Physical memory and MMIO (identity mapped), the whole 64-bit range.
  Writes into ACPI reclaim / NVS memory and into the firmware flash window
  below 4 GB (FF000000h-FFFFFFFFh) are dangerous.
**/
VOID
RwMemSourceInit (
  OUT RW_DATA_SOURCE  *Source
  );

/**
  TRUE when [Address, Address + Size) touches ACPI reclaim / NVS memory or
  the firmware flash window.
**/
BOOLEAN
RwMemIsDangerous (
  IN UINT64  Address,
  IN UINT64  Size
  );

/**
  One range of the UEFI memory map; adjacent descriptors of the same type
  and attributes are merged.
**/
typedef struct {
  UINT64    Start;
  UINT64    Pages;
  UINT64    Attribute;
  UINT32    Type;          // EFI_MEMORY_TYPE
} RW_MEM_RANGE;

/**
  The memory map sorted by address. FreePool *Ranges.
**/
EFI_STATUS
RwMemMapGet (
  OUT RW_MEM_RANGE  **Ranges,
  OUT UINTN         *Count
  );

/**
  Short name of an EFI memory type ("Conventional", "ACPI NVS", ...).
**/
CONST CHAR8 *
RwMemTypeName (
  IN UINT32  Type
  );

/**
  I/O ports 0000h-FFFFh. Writes to CF9h (reset control), 92h (fast reset)
  and 64h (keyboard controller command, FEh resets) are dangerous.
**/
VOID
RwIoSourceInit (
  OUT RW_DATA_SOURCE  *Source
  );

/**
  CMOS / RTC RAM: 00h-7Fh through ports 70h/71h, 80h-FFh (the upper bank on
  most chipsets) through 72h/73h. Byte accesses only.
**/
UINT8
RwCmosRead8 (
  IN UINT8  Index
  );

VOID
RwCmosWrite8 (
  IN UINT8  Index,
  IN UINT8  Value
  );

VOID
RwCmosSourceInit (
  OUT RW_DATA_SOURCE  *Source
  );

/**
  A generic index / data port pair, 8-bit index and 8-bit data, registers
  00h-FFh: a read writes the index port then reads the data port, a write
  writes the index port then the data port. Both ports are I/O ports.
  Reads talk to the hardware (SideEffects); nothing is dangerous here, the
  caller warns about pairs it knows (for example a reset port).
**/
VOID
RwIndexDataSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  UINT16          IndexPort,
  IN  UINT16          DataPort
  );

/**
  ACPI EC RAM access: read command 80h / write command 81h on the command
  port, address and data on the data port. Every wait times out after 20 ms.

  @retval EFI_TIMEOUT  the EC did not answer.
**/
EFI_STATUS
RwEcRead8 (
  IN  UINT16  DataPort,
  IN  UINT16  CmdPort,
  IN  UINT8   Address,
  OUT UINT8   *Value
  );

EFI_STATUS
RwEcWrite8 (
  IN UINT16  DataPort,
  IN UINT16  CmdPort,
  IN UINT8   Address,
  IN UINT8   Value
  );

/**
  TRUE when an EC answers on these ports (status is not FFh and a read of
  address 0 completes). Status receives the status register when not NULL.
**/
BOOLEAN
RwEcProbe (
  IN  UINT16  DataPort,
  IN  UINT16  CmdPort,
  OUT UINT8   *Status  OPTIONAL
  );

/**
  EC RAM 00h-FFh as a data source. Reads talk to the EC (SideEffects).
**/
VOID
RwEcSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  UINT16          DataPort,
  IN  UINT16          CmdPort
  );

#endif
