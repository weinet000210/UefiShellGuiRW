/** @file
  RwCoreLib Super I/O: probe the LPC config ports 2Eh and 4Eh for
  Nuvoton/Winbond, Fintek, ITE, SMSC/Microchip and ASPEED BMC
  (AST2400/2500/2600) parts, list their logical devices, and give any
  logical device's config registers as a data source.

  Config mode is entered before and left after every access, so the chip is
  never left unlocked.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWSIO_H_
#define RW_RWSIO_H_

#include <Library/RwCore/RwBase.h>

#define RW_SIO_NUVOTON  1
#define RW_SIO_FINTEK   2
#define RW_SIO_ITE      3
#define RW_SIO_SMSC     4
#define RW_SIO_ASPEED   5

#define RW_SIO_MAX_CHIPS  2
#define RW_SIO_MAX_LDN    0x20
#define RW_SIO_GLOBAL     0xFF     // "logical device" for the global registers

typedef struct {
  UINT16    Port;       // 2Eh or 4Eh
  UINT8     Kind;       // RW_SIO_*
  UINT8     Rev;
  UINT16    Id;
} RW_SIO_CHIP;

typedef struct {
  UINT8      Ldn;
  BOOLEAN    Active;    // 30h bit 0
  UINT16     Base1;     // 60h/61h
  UINT16     Base2;     // 62h/63h
  UINT8      Irq;       // 70h
  UINT8      Irq2;      // 72h
} RW_SIO_LDN;

typedef struct {
  RW_SIO_CHIP    Chip;
  RW_SIO_LDN     Ldns[RW_SIO_MAX_LDN];
  UINTN          LdnCount;
} RW_SIO_CHIP_INFO;

typedef struct {
  RW_SIO_CHIP_INFO    Chips[RW_SIO_MAX_CHIPS];
  UINTN               ChipCount;
} RW_SIO_LIST;

/**
  Probe one config port. FALSE when no supported chip answers.
**/
BOOLEAN
RwSioProbe (
  IN  UINT16       Port,
  OUT RW_SIO_CHIP  *Chip
  );

/**
  Probe 2Eh and 4Eh and read every logical device that answers.
  @retval EFI_NOT_FOUND  no Super I/O.
**/
EFI_STATUS
RwSioScan (
  OUT RW_SIO_LIST  *List
  );

/**
  Config register access with enter / LDN select / exit around it.
  Ldn RW_SIO_GLOBAL reads the global registers (LDN 0 selected).
**/
UINT8
RwSioRead (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn,
  IN UINT8              Reg
  );

VOID
RwSioWrite (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn,
  IN UINT8              Reg,
  IN UINT8              Value
  );

CONST CHAR8 *
RwSioVendorName (
  IN CONST RW_SIO_CHIP  *Chip
  );

/**
  Function of a logical device ("UART A", "Hardware monitor"), "" when unknown.
**/
CONST CHAR8 *
RwSioLdnName (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn
  );

/**
  "ITE IT8728  rev 2  at 2Eh"
**/
VOID
RwSioDescribe (
  IN  CONST RW_SIO_CHIP  *Chip,
  OUT CHAR8              *Buffer,
  IN  UINTN              BufferSize
  );

/**
  Chips and logical devices as rows (Tag = chip index * 256 + LDN, or
  chip index * 256 + RW_SIO_GLOBAL on the chip row). Also used by Dump All.
**/
VOID
RwSioDecode (
  IN     CONST RW_SIO_LIST  *List,
  IN OUT RW_INFO            *Info
  );

/**
  Config registers 00h-FFh of one logical device (or RW_SIO_GLOBAL).
**/
VOID
RwSioSourceInit (
  OUT RW_DATA_SOURCE     *Source,
  IN  CONST RW_SIO_CHIP  *Chip,
  IN  UINT8              Ldn
  );

#endif
