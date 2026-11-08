/** @file
  RwCoreLib simple spaces: memory, I/O, CMOS and EC.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define EC_OBF         0x01
#define EC_IBF         0x02
#define EC_TIMEOUT_US  20000

#define FLASH_WINDOW_BASE  0xFF000000ULL
#define FLASH_WINDOW_LAST  0xFFFFFFFFULL

typedef struct {
  UINT16    Data;
  UINT16    Cmd;
} EC_PORTS;

//
// ---- Memory ----------------------------------------------------------------
//

STATIC
EFI_STATUS
EFIAPI
MemDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  switch (Width) {
    case 1:
      *Value = MmioRead8 ((UINTN)Address);
      break;
    case 2:
      *Value = MmioRead16 ((UINTN)Address);
      break;
    case 4:
      *Value = MmioRead32 ((UINTN)Address);
      break;
    case 8:
      *Value = MmioRead64 ((UINTN)Address);
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MemDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  switch (Width) {
    case 1:
      MmioWrite8 ((UINTN)Address, (UINT8)Value);
      break;
    case 2:
      MmioWrite16 ((UINTN)Address, (UINT16)Value);
      break;
    case 4:
      MmioWrite32 ((UINTN)Address, (UINT32)Value);
      break;
    case 8:
      MmioWrite64 ((UINTN)Address, Value);
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

BOOLEAN
RwMemIsDangerous (
  IN UINT64  Address,
  IN UINT64  Size
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  EFI_STATUS             Status;
  UINT64                 Last;
  UINT64                 DescLast;
  UINTN                  Offset;
  BOOLEAN                Hit;

  if (Size == 0) {
    return FALSE;
  }

  Last = (Address + Size - 1 < Address) ? MAX_UINT64 : Address + Size - 1;
  if ((Address <= FLASH_WINDOW_LAST) && (Last >= FLASH_WINDOW_BASE)) {
    return TRUE;
  }

  MapSize = 0;
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return FALSE;
  }

  MapSize += 8 * sizeof (EFI_MEMORY_DESCRIPTOR);
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    return TRUE;    // cannot tell: ask
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return TRUE;
  }

  Hit = FALSE;
  for (Offset = 0; Offset + DescSize <= MapSize; Offset += DescSize) {
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if ((Desc->Type != EfiACPIReclaimMemory) && (Desc->Type != EfiACPIMemoryNVS)) {
      continue;
    }

    DescLast = Desc->PhysicalStart + LShiftU64 (Desc->NumberOfPages, EFI_PAGE_SHIFT) - 1;
    if ((Address <= DescLast) && (Last >= Desc->PhysicalStart)) {
      Hit = TRUE;
      break;
    }
  }

  FreePool (Map);
  return Hit;
}

STATIC
BOOLEAN
EFIAPI
MemDsIsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  return RwMemIsDangerous (Address, Width);
}

VOID
RwMemSourceInit (
  OUT RW_DATA_SOURCE  *Source
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiStrCpyS (Source->Name, sizeof (Source->Name), "Memory");
  Source->Base        = 0;
  Source->Limit       = MAX_UINT64;
  Source->AddrDigits  = 16;
  Source->WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32 | RW_DS_WIDTH_64;
  Source->SideEffects = FALSE;
  Source->Read        = MemDsRead;
  Source->Write       = MemDsWrite;
  Source->IsDangerous = MemDsIsDangerous;
}

EFI_STATUS
RwMemMapGet (
  OUT RW_MEM_RANGE  **Ranges,
  OUT UINTN         *Count
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  EFI_STATUS             Status;
  UINTN                  Offset;
  UINTN                  Used;
  UINTN                  Index;
  UINTN                  Pos;
  RW_MEM_RANGE           *Out;
  RW_MEM_RANGE           Item;
  RW_MEM_RANGE           *Last;

  *Ranges = NULL;
  *Count  = 0;
  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  MapSize += 8 * DescSize;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return Status;
  }

  Out = AllocatePool ((MapSize / DescSize + 1) * sizeof (RW_MEM_RANGE));
  if (Out == NULL) {
    FreePool (Map);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Insertion sort by start address (firmware maps are nearly sorted).
  //
  Used = 0;
  for (Offset = 0; Offset + DescSize <= MapSize; Offset += DescSize) {
    Desc           = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    Item.Start     = Desc->PhysicalStart;
    Item.Pages     = Desc->NumberOfPages;
    Item.Attribute = Desc->Attribute;
    Item.Type      = Desc->Type;
    Pos            = Used;
    while ((Pos > 0) && (Out[Pos - 1].Start > Item.Start)) {
      Out[Pos] = Out[Pos - 1];
      Pos--;
    }

    Out[Pos] = Item;
    Used++;
  }

  FreePool (Map);

  //
  // Merge neighbours of the same type and attributes.
  //
  Pos = 0;
  for (Index = 0; Index < Used; Index++) {
    if (Pos > 0) {
      Last = &Out[Pos - 1];
      if ((Last->Type == Out[Index].Type) && (Last->Attribute == Out[Index].Attribute) &&
          (Last->Start + LShiftU64 (Last->Pages, EFI_PAGE_SHIFT) == Out[Index].Start))
      {
        Last->Pages += Out[Index].Pages;
        continue;
      }
    }

    Out[Pos++] = Out[Index];
  }

  *Ranges = Out;
  *Count  = Pos;
  return EFI_SUCCESS;
}

CONST CHAR8 *
RwMemTypeName (
  IN UINT32  Type
  )
{
  STATIC CONST CHAR8  *Names[] = {
    "Reserved",     "Loader Code",  "Loader Data", "BS Code",   "BS Data",
    "RT Code",      "RT Data",      "Conventional", "Unusable", "ACPI Reclaim",
    "ACPI NVS",     "MMIO",         "MMIO Port",   "PAL Code",  "Persistent",
    "Unaccepted"
  };

  return (Type < ARRAY_SIZE (Names)) ? Names[Type] : "OEM/OS";
}

//
// ---- I/O ports -------------------------------------------------------------
//

STATIC
EFI_STATUS
EFIAPI
IoDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  switch (Width) {
    case 1:
      *Value = IoRead8 ((UINTN)Address);
      break;
    case 2:
      *Value = IoRead16 ((UINTN)Address);
      break;
    case 4:
      *Value = IoRead32 ((UINTN)Address);
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
IoDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  switch (Width) {
    case 1:
      IoWrite8 ((UINTN)Address, (UINT8)Value);
      break;
    case 2:
      IoWrite16 ((UINTN)Address, (UINT16)Value);
      break;
    case 4:
      IoWrite32 ((UINTN)Address, (UINT32)Value);
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
IoDsIsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  STATIC CONST UINT16  Ports[] = { 0xCF9, 0x92, 0x64 };
  UINTN                Index;

  for (Index = 0; Index < ARRAY_SIZE (Ports); Index++) {
    if ((Address <= Ports[Index]) && (Address + Width > Ports[Index])) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
RwIoSourceInit (
  OUT RW_DATA_SOURCE  *Source
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiStrCpyS (Source->Name, sizeof (Source->Name), "I/O Space");
  Source->Base        = 0;
  Source->Limit       = 0xFFFF;
  Source->AddrDigits  = 4;
  Source->WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32;
  Source->SideEffects = TRUE;
  Source->Read        = IoDsRead;
  Source->Write       = IoDsWrite;
  Source->IsDangerous = IoDsIsDangerous;
}

//
// ---- CMOS ------------------------------------------------------------------
//

UINT8
RwCmosRead8 (
  IN UINT8  Index
  )
{
  if (Index < 0x80) {
    IoWrite8 (0x70, Index);
    return IoRead8 (0x71);
  }

  IoWrite8 (0x72, Index);
  return IoRead8 (0x73);
}

VOID
RwCmosWrite8 (
  IN UINT8  Index,
  IN UINT8  Value
  )
{
  if (Index < 0x80) {
    IoWrite8 (0x70, Index);
    IoWrite8 (0x71, Value);
  } else {
    IoWrite8 (0x72, Index);
    IoWrite8 (0x73, Value);
  }
}

STATIC
EFI_STATUS
EFIAPI
CmosDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  *Value = RwCmosRead8 ((UINT8)Address);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
CmosDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  RwCmosWrite8 ((UINT8)Address, (UINT8)Value);
  return EFI_SUCCESS;
}

VOID
RwCmosSourceInit (
  OUT RW_DATA_SOURCE  *Source
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiStrCpyS (Source->Name, sizeof (Source->Name), "CMOS");
  Source->Base        = 0;
  Source->Limit       = 0xFF;
  Source->AddrDigits  = 2;
  Source->WidthMask   = RW_DS_WIDTH_8;
  Source->SideEffects = TRUE;
  Source->Read        = CmosDsRead;
  Source->Write       = CmosDsWrite;
}

//
// ---- Index / data port pair ------------------------------------------------
//

typedef struct {
  UINT16    IndexPort;
  UINT16    DataPort;
} INDEX_DATA_CTX;

STATIC
EFI_STATUS
EFIAPI
IndexDataDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  INDEX_DATA_CTX  *Ctx;

  Ctx = (INDEX_DATA_CTX *)Source->Ctx;
  IoWrite8 (Ctx->IndexPort, (UINT8)Address);
  *Value = IoRead8 (Ctx->DataPort);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
IndexDataDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  INDEX_DATA_CTX  *Ctx;

  Ctx = (INDEX_DATA_CTX *)Source->Ctx;
  IoWrite8 (Ctx->IndexPort, (UINT8)Address);
  IoWrite8 (Ctx->DataPort, (UINT8)Value);
  return EFI_SUCCESS;
}

VOID
RwIndexDataSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  UINT16          IndexPort,
  IN  UINT16          DataPort
  )
{
  INDEX_DATA_CTX  *Ctx;

  ZeroMem (Source, sizeof (*Source));
  AsciiSPrint (Source->Name, sizeof (Source->Name), "Index/Data %04Xh/%04Xh", IndexPort, DataPort);
  Source->Base        = 0;
  Source->Limit       = 0xFF;
  Source->AddrDigits  = 2;
  Source->WidthMask   = RW_DS_WIDTH_8;
  Source->SideEffects = TRUE;
  Source->Read        = IndexDataDsRead;
  Source->Write       = IndexDataDsWrite;
  Ctx                 = (INDEX_DATA_CTX *)Source->Ctx;
  Ctx->IndexPort      = IndexPort;
  Ctx->DataPort       = DataPort;
}

//
// ---- Embedded Controller ---------------------------------------------------
//

STATIC
BOOLEAN
EcWait (
  IN UINT16  CmdPort,
  IN UINT8   Mask,
  IN UINT8   Want
  )
{
  UINTN  Us;

  for (Us = 0; Us < EC_TIMEOUT_US; Us += 10) {
    if ((IoRead8 (CmdPort) & Mask) == Want) {
      return TRUE;
    }

    gBS->Stall (10);
  }

  return FALSE;
}

EFI_STATUS
RwEcRead8 (
  IN  UINT16  DataPort,
  IN  UINT16  CmdPort,
  IN  UINT8   Address,
  OUT UINT8   *Value
  )
{
  //
  // Drop a stale byte the EC may have left in its output buffer.
  //
  if (IoRead8 (CmdPort) & EC_OBF) {
    IoRead8 (DataPort);
  }

  if (!EcWait (CmdPort, EC_IBF, 0)) {
    return EFI_TIMEOUT;
  }

  IoWrite8 (CmdPort, 0x80);
  if (!EcWait (CmdPort, EC_IBF, 0)) {
    return EFI_TIMEOUT;
  }

  IoWrite8 (DataPort, Address);
  if (!EcWait (CmdPort, EC_OBF, EC_OBF)) {
    return EFI_TIMEOUT;
  }

  *Value = IoRead8 (DataPort);
  return EFI_SUCCESS;
}

EFI_STATUS
RwEcWrite8 (
  IN UINT16  DataPort,
  IN UINT16  CmdPort,
  IN UINT8   Address,
  IN UINT8   Value
  )
{
  if (!EcWait (CmdPort, EC_IBF, 0)) {
    return EFI_TIMEOUT;
  }

  IoWrite8 (CmdPort, 0x81);
  if (!EcWait (CmdPort, EC_IBF, 0)) {
    return EFI_TIMEOUT;
  }

  IoWrite8 (DataPort, Address);
  if (!EcWait (CmdPort, EC_IBF, 0)) {
    return EFI_TIMEOUT;
  }

  IoWrite8 (DataPort, Value);
  return EcWait (CmdPort, EC_IBF, 0) ? EFI_SUCCESS : EFI_TIMEOUT;
}

BOOLEAN
RwEcProbe (
  IN  UINT16  DataPort,
  IN  UINT16  CmdPort,
  OUT UINT8   *Status  OPTIONAL
  )
{
  UINT8  Sts;
  UINT8  Probe;

  Sts = IoRead8 (CmdPort);
  if (Status != NULL) {
    *Status = Sts;
  }

  return (BOOLEAN)((Sts != 0xFF) && !EFI_ERROR (RwEcRead8 (DataPort, CmdPort, 0, &Probe)));
}

STATIC
EFI_STATUS
EFIAPI
EcDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  EC_PORTS  *P;
  UINT8     Byte;
  EFI_STATUS  Status;

  P      = (EC_PORTS *)Source->Ctx;
  Status = RwEcRead8 (P->Data, P->Cmd, (UINT8)Address, &Byte);
  *Value = EFI_ERROR (Status) ? 0 : Byte;
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EcDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  EC_PORTS  *P;

  P = (EC_PORTS *)Source->Ctx;
  return RwEcWrite8 (P->Data, P->Cmd, (UINT8)Address, (UINT8)Value);
}

VOID
RwEcSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  UINT16          DataPort,
  IN  UINT16          CmdPort
  )
{
  EC_PORTS  *P;

  ZeroMem (Source, sizeof (*Source));
  P       = (EC_PORTS *)Source->Ctx;
  P->Data = DataPort;
  P->Cmd  = CmdPort;
  AsciiSPrint (Source->Name, sizeof (Source->Name), "EC RAM  (data %Xh, cmd %Xh)", DataPort, CmdPort);
  Source->Base        = 0;
  Source->Limit       = 0xFF;
  Source->AddrDigits  = 2;
  Source->WidthMask   = RW_DS_WIDTH_8;
  Source->SideEffects = TRUE;
  Source->Read        = EcDsRead;
  Source->Write       = EcDsWrite;
}
