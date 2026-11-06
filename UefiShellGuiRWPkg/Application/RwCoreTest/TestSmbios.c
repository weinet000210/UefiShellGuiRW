/** @file
  RwCoreTest: SMBIOS module. OVMF on QEMU q35 publishes SMBIOS with at least
  types 0 (BIOS), 1 (System) and 127 (End-of-Table); the other structures
  (3, 4, 16, 17, 19, 32, ...) depend on the QEMU version and are only logged.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

/**
  The first field row named Name, or NULL.
**/
STATIC
CONST RW_INFO_ROW *
FindField (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if ((Info->Rows[Index].Kind == RwKindField) && (AsciiStrCmp (Info->Rows[Index].Name, Name) == 0)) {
      return &Info->Rows[Index];
    }
  }

  return NULL;
}

/**
  Decode the first structure of Type and check the decode succeeded and
  produced the "Decoded fields" and raw data sections.
**/
STATIC
VOID
CheckDecodes (
  IN CONST RW_SMBIOS_TABLE  *Table,
  IN UINT8                  Type,
  IN CONST CHAR8            *KeyField   OPTIONAL
  )
{
  INTN               Index;
  RW_INFO            Info;
  EFI_STATUS         Status;
  CONST RW_INFO_ROW  *Row;
  CHAR8              What[96];

  Index = RwSmbiosFindType (Table, Type, 0);
  AsciiSPrint (What, sizeof (What), "SMBIOS type %d present", Type);
  if (!CHECK (Index >= 0, What)) {
    return;
  }

  RwInfoInit (&Info);
  Status = RwSmbiosDecode (Table, (UINTN)Index, &Info);
  AsciiSPrint (What, sizeof (What), "SMBIOS type %d decodes", Type);
  CHECK (!EFI_ERROR (Status) && !Info.Oom && (Info.Count >= 3), What);
  AsciiSPrint (What, sizeof (What), "SMBIOS type %d starts with Decoded fields", Type);
  CHECK ((Info.Count > 0) && (Info.Rows[0].Kind == RwKindSection) && (AsciiStrCmp (Info.Rows[0].Name, "Decoded fields") == 0), What);

  if (KeyField != NULL) {
    Row = FindField (&Info, KeyField);
    AsciiSPrint (What, sizeof (What), "SMBIOS type %d has field %a", Type, KeyField);
    if (CHECK (Row != NULL, What)) {
      TestLog ("    %a = %a (offset %Xh)", Row->Name, Row->Value, Row->Offset);
      AsciiSPrint (What, sizeof (What), "SMBIOS type %d field %a records its offset", Type, KeyField);
      CHECK (Row->Offset == 0x04 && Row->Size == 1, What);
    }
  }

  RwInfoFree (&Info);
}

VOID
TestSmbios (
  VOID
  )
{
  RW_SMBIOS_TABLE         Table;
  EFI_STATUS              Status;
  UINTN                   Index;
  RW_INFO                 Info;
  CHAR8                   Title[160];
  CONST RW_SMBIOS_STRUCT  *S;
  CONST CHAR8             *Str;

  TestLog ("[SMBIOS]");

  CHECK (AsciiStrCmp (RwSmbiosTypeName (0), "BIOS Information") == 0, "RwSmbiosTypeName(0)");
  CHECK (AsciiStrCmp (RwSmbiosTypeName (17), "Memory Device") == 0, "RwSmbiosTypeName(17)");
  CHECK (AsciiStrCmp (RwSmbiosTypeName (127), "End-of-Table") == 0, "RwSmbiosTypeName(127)");
  CHECK (AsciiStrCmp (RwSmbiosTypeName (200), "OEM Specific") == 0, "RwSmbiosTypeName(200)");

  Status = RwSmbiosLoad (&Table);
  if (!CHECK (Status == EFI_SUCCESS, "RwSmbiosLoad")) {
    TestLog ("  RwSmbiosLoad: %r", Status);
    return;
  }

  TestLog ("  %a", Table.Summary);
  CHECK (Table.MajorVersion >= 2, "SMBIOS major version >= 2");
  CHECK (Table.TableAddress != 0 && Table.TableSize != 0, "SMBIOS table address and size");
  CHECK (Table.Count >= 3, "SMBIOS has at least 3 structures");
  CHECK (!Table.Truncated, "SMBIOS walk not truncated");
  CHECK (
    (Table.Count > 0) && (Table.Structs[Table.Count - 1].Type == RW_SMBIOS_TYPE_END),
    "SMBIOS walk ends at type 127"
    );

  for (Index = 0; Index < Table.Count; Index++) {
    S = &Table.Structs[Index];
    RwSmbiosStructTitle (S, Title, sizeof (Title));
    Str = RwSmbiosGetString (S, 1);
    TestLog ("  %a  [%a]", Title, (Str != NULL) ? Str : "-");
    RwInfoInit (&Info);
    Status = RwSmbiosDecode (&Table, Index, &Info);
    if (EFI_ERROR (Status) || Info.Oom) {
      TestLog ("    decode: %r, %d rows, oom %d", Status, Info.Count, Info.Oom);
    }

    RwInfoFree (&Info);
  }

  CheckDecodes (&Table, 0, "Vendor");
  CheckDecodes (&Table, 1, "Manufacturer");
  CheckDecodes (&Table, RW_SMBIOS_TYPE_END, NULL);

  if (Table.Count > 0) {
    CHECK (RwSmbiosGetString (&Table.Structs[0], 0) == NULL, "RwSmbiosGetString(0) is NULL");
    CHECK (RwSmbiosGetString (&Table.Structs[0], 255) == NULL, "RwSmbiosGetString(255) is NULL");
  }

  RwInfoInit (&Info);
  CHECK (RwSmbiosDecode (&Table, Table.Count, &Info) == EFI_INVALID_PARAMETER, "RwSmbiosDecode rejects a bad index");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  Status = RwSmbiosDump (&Info);
  CHECK (!EFI_ERROR (Status) && !Info.Oom && (Info.Count > Table.Count), "RwSmbiosDump");
  TestLog ("  dump: %d rows", Info.Count);
  RwInfoFree (&Info);

  RwSmbiosFree (&Table);
  CHECK (Table.Structs == NULL && Table.Count == 0, "RwSmbiosFree");
}
