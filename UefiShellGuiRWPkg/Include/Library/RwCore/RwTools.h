/** @file
  RwCoreLib tools (Phase 6): the write log, the full text dump, and the file
  formats the tools save (BMP screenshots, hex dumps, page snapshots).

  Nothing here touches a file system: the application saves the buffers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWTOOLS_H_
#define RW_RWTOOLS_H_

#include <Library/RwCore/RwBase.h>

//
// ---- Write log -----------------------------------------------------------------
//
// Every write the user makes goes through RwDsWrite, RwMsrWriteOn or
// RwSmbusEnable, and each of them adds an entry here: time, space, address,
// width, old value (read back first when reads have no side effects), new
// value and result. The log lives in memory only and keeps the newest
// RW_WLOG_MAX entries; nothing is written to disk unless the user saves it.
//
#define RW_WLOG_MAX        1000
#define RW_WLOG_SPACE_MAX  48

typedef struct {
  EFI_TIME      Time;
  BOOLEAN       TimeValid;
  BOOLEAN       OldValid;
  UINT8         Width;                      ///< bytes (1, 2, 4, 8)
  CHAR8         Space[RW_WLOG_SPACE_MAX];   ///< "Memory", "PCI 00:1F.0 8086:2918", "MSR CPU 1"
  UINT64        Address;
  UINT64        Old;
  UINT64        New;
  EFI_STATUS    Status;
  UINT64        Serial;                     ///< 1 for the first write since start, never reused
} RW_WLOG_ENTRY;

VOID
RwWlogAdd (
  IN CONST CHAR8  *Space,
  IN UINT64       Address,
  IN UINTN        Width,
  IN BOOLEAN      OldValid,
  IN UINT64       Old,
  IN UINT64       New,
  IN EFI_STATUS   Status
  );

/** Entries kept (at most RW_WLOG_MAX). **/
UINTN
RwWlogCount (
  VOID
  );

/** Entry Index, 0 = the oldest kept. NULL when out of range. **/
CONST RW_WLOG_ENTRY *
RwWlogGet (
  IN UINTN  Index
  );

/** Writes logged since start (also counts the ones dropped from the ring). **/
UINT64
RwWlogTotal (
  VOID
  );

VOID
RwWlogClear (
  VOID
  );

/**
  Cells of one entry for a table or a text line: time "hh:mm:ss", address
  (16 hex digits above 4 GB, else 8), old / new value in the entry's width,
  status ("OK" or the EFI status).
**/
VOID
RwWlogFormat (
  IN  CONST RW_WLOG_ENTRY  *Entry,
  OUT CHAR8                *Time,
  IN  UINTN                TimeSize,
  OUT CHAR8                *Address,
  IN  UINTN                AddressSize,
  OUT CHAR8                *Old,
  IN  UINTN                OldSize,
  OUT CHAR8                *New,
  IN  UINTN                NewSize,
  OUT CHAR8                *Status,
  IN  UINTN                StatusSize
  );

/** The whole log as text, oldest first, one line per write. **/
VOID
RwWlogToText (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  );

//
// ---- Dump all ----------------------------------------------------------------------
//
#define RW_DUMP_PCI      BIT0
#define RW_DUMP_ACPI     BIT1     // table list, decoded tables, ASL of DSDT / SSDTs
#define RW_DUMP_SMBIOS   BIT2
#define RW_DUMP_USB      BIT3
#define RW_DUMP_SIO      BIT4
#define RW_DUMP_SPD      BIT5     // SPD EEPROMs 50h-57h; never enables a disabled SMBus host
#define RW_DUMP_STORAGE  BIT6
#define RW_DUMP_EDID     BIT7
#define RW_DUMP_MSR      BIT8     // known MSRs on every CPU (an AP only reads what the BSP read)
#define RW_DUMP_ALL      0x1FF

/**
  Append the selected parts as text, each under a "==== Name ====" heading.
  A part that is not there says so in one line.
**/
VOID
RwDumpAll (
  IN     UINT32   Parts,
  IN     BOOLEAN  Crlf,
  IN OUT RW_STR   *Out
  );

//
// ---- File formats ------------------------------------------------------------------
//

/**
  24-bit bottom-up BMP of Width x Height 0x00RRGGBB pixels (row-major).
  FreePool *Bmp.
**/
EFI_STATUS
RwBmpEncode (
  IN  CONST UINT32  *Pixels,
  IN  UINTN         Width,
  IN  UINTN         Height,
  OUT UINT8         **Bmp,
  OUT UINTN         *Size
  );

/**
  Hex dump text of Size bytes read at Base: "Address: 16 bytes  ASCII" with
  AddrDigits address digits; bytes whose Valid entry is FALSE print as "--".
  Valid may be NULL (all valid).
**/
VOID
RwHexText (
  IN     CONST UINT8    *Data,
  IN     CONST BOOLEAN  *Valid  OPTIONAL,
  IN     UINTN          Size,
  IN     UINT64         Base,
  IN     UINTN          AddrDigits,
  IN     BOOLEAN        Crlf,
  IN OUT RW_STR         *Out
  );

//
// Page snapshot file (SNAPnnnn.BIN):
//   "RWSNAP01", UINT64 page address, UINT32 size, UINT32 0, CHAR8 Name[80]
//   (the data source), Size data bytes, (Size + 7) / 8 valid bits.
//
#define RW_SNAP_MAGIC      "RWSNAP01"
#define RW_SNAP_NAME_MAX   80
#define RW_SNAP_HEADER     (8 + 8 + 4 + 4 + RW_SNAP_NAME_MAX)

EFI_STATUS
RwSnapEncode (
  IN  CONST CHAR8    *Name,
  IN  UINT64         Page,
  IN  CONST UINT8    *Data,
  IN  CONST BOOLEAN  *Valid,
  IN  UINTN          Size,
  OUT UINT8          **File,
  OUT UINTN          *FileSize
  );

/**
  @retval EFI_VOLUME_CORRUPTED  not a snapshot, or not of Size bytes.
**/
EFI_STATUS
RwSnapDecode (
  IN  CONST UINT8  *File,
  IN  UINTN        FileSize,
  IN  UINTN        Size,
  OUT CHAR8        *Name,
  OUT UINT64       *Page,
  OUT UINT8        *Data,
  OUT BOOLEAN      *Valid
  );

#endif
