/** @file
  RwCoreLib ACPI: table list (RSDP -> XSDT / RSDT -> every table, plus the
  DSDT and FACS the FADT points to), checksum check, header and field decode
  (FADT, MADT, MCFG, HPET, DMAR) and the AML to ASL disassembler.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWACPI_H_
#define RW_RWACPI_H_

#include <Library/RwCore/RwBase.h>

//
// ---- Table list -----------------------------------------------------------
//
#define RW_ACPI_MAX_TABLES  128
#define RW_ACPI_RAW_MAX     (512 * 1024)   // RwAcpiDecode dumps at most this many raw bytes

typedef struct {
  UINT8      *Ptr;              // the table in memory (firmware owned, not copied)
  UINT64     Address;           // same as Ptr
  UINT32     Length;
  BOOLEAN    HasHeader;         // FALSE for the FACS (no standard header)
  BOOLEAN    ChecksumOk;        // TRUE when the bytes sum to 0 (always TRUE without header)
  UINT8      Revision;          // header revision, FACS version for the FACS
  CHAR8      Signature[5];      // non-printable bytes as ' '
  CHAR8      OemId[7];          // "" padded with spaces for the FACS
  CHAR8      OemTableId[9];
} RW_ACPI_TABLE;

typedef struct {
  UINT8            *Rsdp;           // NULL: the firmware publishes no ACPI tables
  UINT8            RsdpRevision;
  CHAR8            RsdpOemId[7];
  BOOLEAN          UsesXsdt;        // revision >= 2 and a non-zero XSDT address
  UINT64           SdtAddress;      // XSDT or RSDT address
  RW_ACPI_TABLE    *Tables;         // Count entries, XSDT/RSDT first, then its entries, then DSDT / FACS
  UINTN            Count;
} RW_ACPI_TABLES;

/**
  Find the RSDP in the UEFI configuration table (ACPI 2.0 GUID first, then
  ACPI 1.0) and list every table: the XSDT (or RSDT), each table it points
  to, and the DSDT and FACS referenced from the FADT (X_ fields preferred).
  Duplicate addresses are listed once; tables shorter than their header or
  longer than 16 MB are skipped; at most RW_ACPI_MAX_TABLES entries.

  @param[out] Tables  Result, release with RwAcpiFree. Always initialised.

  @retval EFI_SUCCESS           The RSDP was found (Count may still be 0).
  @retval EFI_NOT_FOUND         No ACPI tables are published.
  @retval EFI_OUT_OF_RESOURCES  No memory for the list.
**/
EFI_STATUS
RwAcpiScan (
  OUT RW_ACPI_TABLES  *Tables
  );

/**
  Release what RwAcpiScan allocated and zero Tables.

  @param[in,out] Tables  List to release.
**/
VOID
RwAcpiFree (
  IN OUT RW_ACPI_TABLES  *Tables
  );

/**
  Find a table by signature.

  @param[in] Tables     List from RwAcpiScan.
  @param[in] Signature  Four characters, e.g. "FACP", "SSDT".
  @param[in] Instance   0 for the first table with that signature, 1 for the second, ...

  @return the table, or NULL.
**/
RW_ACPI_TABLE *
RwAcpiFindTable (
  IN CONST RW_ACPI_TABLES  *Tables,
  IN CONST CHAR8           *Signature,
  IN UINTN                 Instance
  );

/**
  8-bit sum of Length bytes (0 for a valid ACPI table).

  @param[in] Data    Bytes.
  @param[in] Length  Byte count.

  @return the sum modulo 256.
**/
UINT8
RwAcpiChecksum (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  );

/**
  TRUE for a DSDT or SSDT (an AML definition block).

  @param[in] Table  Table entry.
**/
BOOLEAN
RwAcpiIsAml (
  IN CONST RW_ACPI_TABLE  *Table
  );

/**
  The table list as an info document: an "RSDP" section (address, revision,
  OEM ID, XSDT/RSDT address), then a "Tables (N)" section with one Field row
  per table, tagged with its index in Tables->Tables. The row's Name is the
  signature, its Value "OEMID   OEMTABLE  rev   N  length  address  OK|BAD
  CHECKSUM|-" (the uefirrw list columns); a BAD CHECKSUM row has RwEmAlert.
  Without RSDP a single Text row "No ACPI tables published by the firmware."
  (RwEmAlert).

  @param[in]     Tables  List from RwAcpiScan.
  @param[in,out] Info    Rows are appended.
**/
VOID
RwAcpiListInfo (
  IN     CONST RW_ACPI_TABLES  *Tables,
  IN OUT RW_INFO               *Info
  );

/**
  Decode one table: header (signature, address, length, revision, checksum,
  OEM ids, creator; FACS fields for the FACS), the decoded body for FADT,
  MADT, MCFG, HPET and DMAR, a note for DSDT / SSDT, and the raw bytes (at most
  RW_ACPI_RAW_MAX) as a hex dump. Field rows carry their table offset
  (RwInfoSetReg).

  @param[in]     Table  Table entry from RwAcpiScan.
  @param[in,out] Info   Rows are appended.
**/
VOID
RwAcpiDecode (
  IN     CONST RW_ACPI_TABLE  *Table,
  IN OUT RW_INFO              *Info
  );

/**
  File name a table is saved under: the signature followed by Number and
  Extension, e.g. "DSDT3.aml" / "SSDT5.dsl" (uefirrw used the table index as
  Number).

  @param[in]  Table      Table entry.
  @param[in]  Number     Number appended to the signature.
  @param[in]  Extension  Extension without the dot, e.g. "aml".
  @param[out] Buffer     Receives the name.
  @param[in]  Size       Size of Buffer in bytes (32 is enough).
**/
VOID
RwAcpiFileName (
  IN  CONST RW_ACPI_TABLE  *Table,
  IN  UINTN                Number,
  IN  CONST CHAR8          *Extension,
  OUT CHAR8                *Buffer,
  IN  UINTN                Size
  );

//
// ---- AML disassembler -----------------------------------------------------
//
// AML method invocations carry no argument count, so RwAmlPrepare first
// walks every DSDT / SSDT silently and records each Method and External
// declaration; RwAmlDisassemble then looks names up in that table with the
// ACPI namespace search rules. Without a prepare pass method calls are shown
// as bare names and their arguments as separate statements.
//
// The context holds the method table and the parser state; nothing is kept
// in module globals.
//
#define RW_AML_MAX_PATH  128      // 32 name segments

typedef struct {
  CHAR8    Path[RW_AML_MAX_PATH];  // absolute path as concatenated 4-byte segments
  UINT8    Len;
  UINT8    Args;
} RW_AML_METHOD;

typedef struct {
  //
  // Method table (filled by RwAmlPrepare).
  //
  RW_AML_METHOD    *Methods;
  UINTN            MethodCount;
  UINTN            MethodCap;
  BOOLEAN          Prepared;
  //
  // Parser state, private to RwAml.c and reset by every call.
  //
  UINT8            *Base;
  UINT8            *P;
  BOOLEAN          Silent;
  BOOLEAN          Prepass;
  BOOLEAN          ElseIf;       // next If prints as ElseIf
  BOOLEAN          UuidHint;     // operands of LEqual/LNotEqual: 16-byte buffer = ToUUID
  BOOLEAN          Crlf;
  UINTN            Depth;
  CHAR8            Scope[RW_AML_MAX_PATH];
  UINTN            ScopeLen;
  RW_STR           *Out;
  RW_INFO          *Info;
} RW_AML_CTX;

/**
  Initialise an empty context.

  @param[out] Ctx  Context.
**/
VOID
RwAmlInit (
  OUT RW_AML_CTX  *Ctx
  );

/**
  Release the method table and reset the context.

  @param[in,out] Ctx  Context.
**/
VOID
RwAmlFree (
  IN OUT RW_AML_CTX  *Ctx
  );

/**
  Pre-pass: collect Method and External declarations (path and argument
  count) from every definition block given. Replaces the previous table.

  @param[in,out] Ctx      Context.
  @param[in]     Tables   Definition blocks (full tables including the 36-byte header).
  @param[in]     Lengths  Their lengths.
  @param[in]     Count    Number of tables.
**/
VOID
RwAmlPrepare (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       **Tables,
  IN     UINT32      *Lengths,
  IN     UINTN       Count
  );

/**
  RwAmlPrepare over every DSDT and SSDT of a table list.

  @param[in,out] Ctx     Context.
  @param[in]     Tables  List from RwAcpiScan.
**/
VOID
RwAmlPrepareTables (
  IN OUT RW_AML_CTX            *Ctx,
  IN     CONST RW_ACPI_TABLES  *Tables
  );

/**
  Disassemble one DSDT / SSDT into ASL source text (ASL 1.0 operator syntax,
  four spaces per level): a comment block, DefinitionBlock (...), { body }.
  This is also the content of the .dsl file uefirrw saved (with Crlf TRUE).

  @param[in,out] Ctx     Context (prepared).
  @param[in]     Table   The table including its header.
  @param[in]     Length  Table length.
  @param[in]     Crlf    TRUE: lines end with "\r\n", FALSE: "\n".
  @param[in,out] Out     Text is appended.

  @retval EFI_SUCCESS            Done (undecodable AML shows as a comment in the text).
  @retval EFI_INVALID_PARAMETER  Table shorter than its header ("Table too short." is appended).
  @retval EFI_OUT_OF_RESOURCES   Out ran out of memory.
**/
EFI_STATUS
RwAmlDisassemble (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table,
  IN     UINT32      Length,
  IN     BOOLEAN     Crlf,
  IN OUT RW_STR      *Out
  );

/**
  Same as RwAmlDisassemble, one Text row per line (leading spaces kept,
  Level 0) with emphasis: RwEmStrong for DefinitionBlock / Scope / Device /
  Method ... heads, RwEmDim for comments and External, RwEmAlert for
  undecodable AML.

  @param[in,out] Ctx     Context (prepared).
  @param[in]     Table   The table including its header.
  @param[in]     Length  Table length.
  @param[in,out] Info    Rows are appended.

  @retval EFI_SUCCESS            Done.
  @retval EFI_INVALID_PARAMETER  Table shorter than its header.
  @retval EFI_OUT_OF_RESOURCES   A row was dropped for lack of memory.
**/
EFI_STATUS
RwAmlDisassembleInfo (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table,
  IN     UINT32      Length,
  IN OUT RW_INFO     *Info
  );

#endif
