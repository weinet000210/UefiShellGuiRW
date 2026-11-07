/** @file
  RwCoreLib SMBIOS module: entry point discovery (SMBIOS 2.x and 3.x), the
  structure walk, type names and per-type structure decoders following the
  DMTF SMBIOS Reference Specification (DSP0134).

  The structures returned point into the firmware's SMBIOS table, which stays
  in memory for the life of the boot; RwSmbiosFree only releases the list.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWSMBIOS_H_
#define RW_RWSMBIOS_H_

#include <Library/RwCore/RwBase.h>

//
// Most structures RwSmbiosLoad records (the walk stops there).
//
#define RW_SMBIOS_MAX_STRUCTS  1024

#define RW_SMBIOS_TYPE_END     127

//
// One SMBIOS structure.
//
typedef struct {
  UINT8          Type;
  UINT8          Length;        // formatted area length (header byte 1)
  UINT16         Handle;
  UINTN          TotalLength;   // formatted area + string set incl. the double NUL
  CONST UINT8    *Data;         // start of the formatted area (firmware memory)
  CONST CHAR8    *TypeName;     // RwSmbiosTypeName (Type)
} RW_SMBIOS_STRUCT;

//
// The SMBIOS table as found in the EFI configuration table. When both a 3.x
// (64-bit) and a 2.x (32-bit) entry point exist, the 3.x one is used.
//
typedef struct {
  BOOLEAN             Is64Bit;          // TRUE: SMBIOS 3.x entry point
  UINT8               MajorVersion;
  UINT8               MinorVersion;
  UINT8               DocRev;           // 3.x only, else 0
  CONST VOID          *EntryPoint;
  UINT64              TableAddress;
  UINT32              TableSize;        // 3.x: maximum size, 2.x: table length
  UINT16              EpStructCount;    // 2.x: structure count in the entry point, 3.x: 0
  BOOLEAN             Truncated;        // the walk stopped at RW_SMBIOS_MAX_STRUCTS
  CHAR8               Summary[160];     // one-line entry point description
  RW_SMBIOS_STRUCT    *Structs;         // Count entries, FreePool'd by RwSmbiosFree
  UINTN               Count;
} RW_SMBIOS_TABLE;

/**
  Find the SMBIOS entry point and walk every structure, up to and including
  the End-of-Table (type 127) structure. The walk stops early at a structure
  whose length is below 4 or whose string set does not end inside the table.

  @param[out] Table  Filled on success; release with RwSmbiosFree.

  @retval EFI_SUCCESS           Table filled (Count may be 0).
  @retval EFI_NOT_FOUND         No SMBIOS entry point in the configuration table.
  @retval EFI_OUT_OF_RESOURCES  No memory for the structure list.
  @retval EFI_INVALID_PARAMETER Table is NULL.
**/
EFI_STATUS
RwSmbiosLoad (
  OUT RW_SMBIOS_TABLE  *Table
  );

/**
  Release what RwSmbiosLoad allocated and zero Table. Safe on a zeroed or
  already freed table.

  @param[in, out] Table  Table to release.
**/
VOID
RwSmbiosFree (
  IN OUT RW_SMBIOS_TABLE  *Table
  );

/**
  Name of an SMBIOS structure type ("BIOS Information", "Inactive",
  "End-of-Table", "OEM Specific", "Unknown").

  @param[in] Type  Structure type.

  @return a static string, never NULL.
**/
CONST CHAR8 *
RwSmbiosTypeName (
  IN UINT8  Type
  );

/**
  String Number (1-based) of a structure's string set.

  @param[in] Struct  Structure.
  @param[in] Number  String number as stored in the formatted area.

  @return the string inside the table, or NULL for 0 or a missing string.
**/
CONST CHAR8 *
RwSmbiosGetString (
  IN CONST RW_SMBIOS_STRUCT  *Struct,
  IN UINT8                   Number
  );

/**
  Index of the first structure of Type at or after Start.

  @param[in] Table  Loaded table.
  @param[in] Type   Structure type.
  @param[in] Start  First index to look at.

  @return the index, or -1 when there is none.
**/
INTN
RwSmbiosFindType (
  IN CONST RW_SMBIOS_TABLE  *Table,
  IN UINT8                  Type,
  IN UINTN                  Start
  );

/**
  Title line of a structure: "Type 0 (00h)  BIOS Information   Handle 0000h
  Length 18h   @ <address>".

  @param[in]  Struct      Structure.
  @param[out] Buffer      Receives the text.
  @param[in]  BufferSize  Size of Buffer in bytes.
**/
VOID
RwSmbiosStructTitle (
  IN  CONST RW_SMBIOS_STRUCT  *Struct,
  OUT CHAR8                   *Buffer,
  IN  UINTN                   BufferSize
  );

/**
  Entry point description as fields: version, entry point kind and address,
  table address and size, structure counts.

  @param[in]      Table  Loaded table.
  @param[in, out] Info   Rows are appended here.
**/
VOID
RwSmbiosEntryInfo (
  IN     CONST RW_SMBIOS_TABLE  *Table,
  IN OUT RW_INFO                *Info
  );

/**
  Decode one structure: section "Decoded fields" (per-type decoders for
  types 0-4, 7-9, 11-13, 16, 17, 19, 32, 41; a note for the others), section
  "Strings" (the string set), and hex dumps of the formatted area and the
  string set. Field rows record their offset in the structure
  (RwInfoSetReg); hex rows of the string set use offsets from the start of
  the structure as well.

  @param[in]      Struct  Structure.
  @param[in, out] Info    Rows are appended here.

  @retval EFI_SUCCESS            Decoded (Info->Oom tells whether rows were dropped).
  @retval EFI_INVALID_PARAMETER  Struct or Info is NULL, or the structure is malformed.
**/
EFI_STATUS
RwSmbiosDecodeStruct (
  IN     CONST RW_SMBIOS_STRUCT  *Struct,
  IN OUT RW_INFO                 *Info
  );

/**
  RwSmbiosDecodeStruct for Table->Structs[Index].

  @param[in]      Table  Loaded table.
  @param[in]      Index  Structure index.
  @param[in, out] Info   Rows are appended here.

  @retval EFI_SUCCESS            Decoded.
  @retval EFI_INVALID_PARAMETER  Index out of range or a NULL parameter.
**/
EFI_STATUS
RwSmbiosDecode (
  IN     CONST RW_SMBIOS_TABLE  *Table,
  IN     UINTN                  Index,
  IN OUT RW_INFO                *Info
  );

/**
  Dump All: load the table and decode every structure. Adds a section
  "SMBIOS (n structures)" with the entry point summary, then per structure a
  section "SMBIOS Structure" (value: RwSmbiosStructTitle) with the decode one
  level below. When there is no table, adds the section "SMBIOS: not found".

  @param[in, out] Info  Rows are appended here.

  @return the status of RwSmbiosLoad.
**/
EFI_STATUS
RwSmbiosDump (
  IN OUT RW_INFO  *Info
  );

#endif
