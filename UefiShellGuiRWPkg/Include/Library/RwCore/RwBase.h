/** @file
  RwCoreLib base types: growable strings, structured info documents and the
  DataSource interface the hex grid edits.

  RwCoreLib holds every hardware access and every decoder of the tool. It
  never draws: it does not include LVGL, and it never writes to ConOut. Views
  turn the structures declared here into widgets, and the dump commands turn
  them into text.

  All text produced by RwCoreLib is CHAR8 (ASCII, valid UTF-8). Print formats
  therefore use %a for strings.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_CORE_BASE_H_
#define RW_CORE_BASE_H_

#include <Uefi.h>

//
// ---- RW_STR: growable NUL-terminated CHAR8 string ---------------------------
//
// An RW_STR that ran out of memory keeps what it had and sets Oom; every
// append on it is then ignored. Buf is never NULL after RwStrInit.
//
typedef struct {
  CHAR8      *Buf;
  UINTN      Len;
  UINTN      Cap;
  BOOLEAN    Oom;
} RW_STR;

VOID
RwStrInit (
  OUT RW_STR  *Str
  );

VOID
RwStrFree (
  IN OUT RW_STR  *Str
  );

VOID
RwStrReset (
  IN OUT RW_STR  *Str
  );

/**
  Hand the buffer over to the caller (free it with FreePool) and reset Str.
**/
CHAR8 *
RwStrDetach (
  IN OUT RW_STR  *Str
  );

VOID
RwStrAppendN (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Text,
  IN     UINTN        Count
  );

VOID
RwStrAppend (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Text
  );

VOID
RwStrAppendChar (
  IN OUT RW_STR  *Str,
  IN     CHAR8   Ch
  );

VOID
EFIAPI
RwStrAppendF (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Format,
  ...
  );

VOID
EFIAPI
RwStrAppendV (
  IN OUT RW_STR       *Str,
  IN     CONST CHAR8  *Format,
  IN     VA_LIST      Marker
  );

/**
  Allocate a formatted string (FreePool it). NULL when out of memory.
**/
CHAR8 *
EFIAPI
RwSPrintAlloc (
  IN CONST CHAR8  *Format,
  ...
  );

/**
  Copy a CHAR16 string to ASCII, replacing characters above 0x7E by '?'.
  Always NUL-terminates when DstSize > 0.
**/
VOID
RwUnicodeToAscii (
  IN  CONST CHAR16  *Src,
  OUT CHAR8         *Dst,
  IN  UINTN         DstSize
  );

//
// ---- RW_INFO: structured information document --------------------------
//
// A decoder fills an RW_INFO with rows. Views show it as a property table
// (Name | Value) or a tree, the dump commands print it as text.
//
//   Section  a group heading ("Capabilities", "[50] MSI"). Value optional.
//   Field    Name = Value.
//   Text     a free line (disassembly, hex dump, notes). Name holds the text.
//
// Level indents a row under the section before it (0 = top).
//
typedef enum {
  RwKindSection = 0,
  RwKindField   = 1,
  RwKindText    = 2
} RW_INFO_KIND;

//
// Emphasis, mapped to colors by the view.
//
typedef enum {
  RwEmNormal = 0,
  RwEmStrong = 1,     // the value that matters (current state, link status)
  RwEmAlert  = 2,     // error bits set, something unusual
  RwEmDim    = 3      // unused, not present, raw undecoded data
} RW_INFO_EMPHASIS;

#define RW_INFO_NO_TAG     (-1)
#define RW_INFO_NO_OFFSET  MAX_UINT32

typedef struct {
  UINT8     Kind;       // RW_INFO_KIND
  UINT8     Level;
  UINT8     Emphasis;   // RW_INFO_EMPHASIS
  UINT8     Reserved;
  INT32     Tag;        // >= 0: a selectable item (device index, table index), else RW_INFO_NO_TAG
  UINT32    Offset;     // register / structure offset the row decodes, or RW_INFO_NO_OFFSET
  UINT32    Size;       // bytes at Offset (0 when unknown)
  CHAR8     *Name;      // never NULL
  CHAR8     *Value;     // never NULL ("" when none)
} RW_INFO_ROW;

typedef struct {
  RW_INFO_ROW    *Rows;
  UINTN          Count;
  UINTN          Capacity;
  BOOLEAN        Oom;       // a row was dropped for lack of memory
} RW_INFO;

VOID
RwInfoInit (
  OUT RW_INFO  *Info
  );

VOID
RwInfoFree (
  IN OUT RW_INFO  *Info
  );

/**
  Append a row. Name and Value are copied; Value may be NULL.
  @return the new row, or NULL when out of memory.
**/
RW_INFO_ROW *
RwInfoAdd (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Kind,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Value
  );

RW_INFO_ROW *
EFIAPI
RwInfoSection (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST CHAR8  *Format,
  ...
  );

/**
  Name = formatted value.
**/
RW_INFO_ROW *
EFIAPI
RwInfoField (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Format,
  ...
  );

RW_INFO_ROW *
EFIAPI
RwInfoText (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Format,
  ...
  );

/**
  Record which bytes a row decodes. Row may be NULL (ignored).
**/
VOID
RwInfoSetReg (
  IN RW_INFO_ROW  *Row,
  IN UINT32       Offset,
  IN UINT32       Size
  );

VOID
RwInfoSetTag (
  IN RW_INFO_ROW  *Row,
  IN INT32        Tag
  );

/**
  Hex + ASCII dump as Text rows ("0000: 00 11 ... ................").
**/
VOID
RwInfoAddHex (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST UINT8  *Data,
  IN     UINTN        Size,
  IN     UINTN        Base
  );

/**
  Append Src's rows to Dst, Level-shifted by LevelShift.
**/
VOID
RwInfoAppend (
  IN OUT RW_INFO        *Dst,
  IN     CONST RW_INFO  *Src,
  IN     UINT8          LevelShift
  );

/**
  Plain text rendering for dumps: sections as headings, fields aligned as
  "Name  Value", two spaces per level. Lines end with "\r\n" when Crlf.
**/
VOID
RwInfoToText (
  IN     CONST RW_INFO  *Info,
  IN OUT RW_STR         *Out,
  IN     BOOLEAN        Crlf
  );

/**
  Bit names of Value, space separated, for decoders: Names[n] names bit n
  (NULL = unnamed). Appends "none" when no named bit is set and NoneText is
  not NULL.
**/
VOID
RwStrAppendBits (
  IN OUT RW_STR       *Str,
  IN     UINT64       Value,
  IN     CONST CHAR8  **Names,
  IN     UINTN        Count,
  IN     CONST CHAR8  *NoneText
  );

//
// ---- RW_DATA_SOURCE: an addressable register space ------------------------
//
// The hex grid edits any space through this interface: PCI config space,
// memory, I/O ports, CMOS, EC RAM, a Super I/O logical device.
//
// Addresses run from Base to Limit (inclusive). Width is 1, 2, 4 or 8 bytes
// and only widths in WidthMask (bit n = 2^n bytes) are accepted; the grid
// splits wider accesses. A Read that fails reports the error and the grid
// shows "--". Write is NULL for a read-only space.
//
// IsDangerous returns TRUE when writing Width bytes at Address may reset,
// hang or reconfigure the machine (CF9h, BARs, bus numbers, SPI flash / ACPI
// regions). The grid then asks before the write. NULL = never dangerous.
//
#define RW_DS_NAME_MAX   80
#define RW_DS_CTX_MAX    64

#define RW_DS_WIDTH_8    BIT0
#define RW_DS_WIDTH_16   BIT1
#define RW_DS_WIDTH_32   BIT2
#define RW_DS_WIDTH_64   BIT3

typedef struct _RW_DATA_SOURCE RW_DATA_SOURCE;

typedef
EFI_STATUS
(EFIAPI *RW_DS_READ)(
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  );

typedef
EFI_STATUS
(EFIAPI *RW_DS_WRITE)(
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  );

typedef
BOOLEAN
(EFIAPI *RW_DS_IS_DANGEROUS)(
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  );

struct _RW_DATA_SOURCE {
  CHAR8                 Name[RW_DS_NAME_MAX];   // window title, e.g. "PCI 00:1F.0 8086:2918"
  UINT64                Base;
  UINT64                Limit;
  UINT8                 AddrDigits;             // hex digits for the address column
  UINT8                 WidthMask;              // RW_DS_WIDTH_*
  BOOLEAN               SideEffects;            // reads may change state (I/O, EC): no auto refresh
  RW_DS_READ            Read;
  RW_DS_WRITE           Write;                  // NULL = read-only
  RW_DS_IS_DANGEROUS    IsDangerous;            // NULL = never
  UINT64                Ctx[RW_DS_CTX_MAX / sizeof (UINT64)];  // source private data
};

EFI_STATUS
RwDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  );

/**
  Write without asking. EFI_WRITE_PROTECTED for a read-only space.
  Callers that talk to the user check RwDsIsDangerous first.
**/
EFI_STATUS
RwDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  );

BOOLEAN
RwDsIsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  );

UINT64
RwDsSize (
  IN RW_DATA_SOURCE  *Source
  );

/**
  Read Count bytes at Address into Buffer, using the widest width the source
  accepts. Bytes that fail to read are 0xFF and their bit in Valid (one bit
  per byte, may be NULL) is clear.
**/
EFI_STATUS
RwDsReadBlock (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Count,
  OUT UINT8           *Buffer,
  OUT UINT8           *Valid   OPTIONAL
  );

#endif
