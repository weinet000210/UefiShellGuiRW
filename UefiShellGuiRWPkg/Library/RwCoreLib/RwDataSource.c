/** @file
  RW_DATA_SOURCE helpers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

STATIC
BOOLEAN
WidthOk (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  UINTN  Bit;

  switch (Width) {
    case 1:
      Bit = RW_DS_WIDTH_8;
      break;
    case 2:
      Bit = RW_DS_WIDTH_16;
      break;
    case 4:
      Bit = RW_DS_WIDTH_32;
      break;
    case 8:
      Bit = RW_DS_WIDTH_64;
      break;
    default:
      return FALSE;
  }

  if ((Source->WidthMask & Bit) == 0) {
    return FALSE;
  }

  if ((Address < Source->Base) || (Address > Source->Limit) ||
      (Width - 1 > Source->Limit - Address))
  {
    return FALSE;
  }

  return TRUE;
}

EFI_STATUS
RwDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  *Value = 0;
  if ((Source == NULL) || (Source->Read == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!WidthOk (Source, Address, Width)) {
    return EFI_INVALID_PARAMETER;
  }

  return Source->Read (Source, Address, Width, Value);
}

EFI_STATUS
RwDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  if (Source == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Source->Write == NULL) {
    return EFI_WRITE_PROTECTED;
  }

  if (!WidthOk (Source, Address, Width)) {
    return EFI_INVALID_PARAMETER;
  }

  return Source->Write (Source, Address, Width, Value);
}

BOOLEAN
RwDsIsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  if ((Source == NULL) || (Source->IsDangerous == NULL)) {
    return FALSE;
  }

  return Source->IsDangerous (Source, Address, Width);
}

UINT64
RwDsSize (
  IN RW_DATA_SOURCE  *Source
  )
{
  if (Source->Limit - Source->Base == MAX_UINT64) {
    return MAX_UINT64;
  }

  return Source->Limit - Source->Base + 1;
}

EFI_STATUS
RwDsReadBlock (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Count,
  OUT UINT8           *Buffer,
  OUT UINT8           *Valid   OPTIONAL
  )
{
  UINTN       Done;
  UINTN       Width;
  UINTN       Index;
  UINT64      Value;
  UINT64      Byte;
  EFI_STATUS  Status;
  EFI_STATUS  Result;

  Result = EFI_SUCCESS;
  if (Valid != NULL) {
    ZeroMem (Valid, (Count + 7) / 8);
  }

  for (Done = 0; Done < Count; Done += Width) {
    //
    // Widest accepted, naturally aligned width that stays inside the block.
    //
    for (Width = 8; Width > 1; Width /= 2) {
      if ((Done + Width <= Count) && (((Address + Done) & (Width - 1)) == 0) &&
          WidthOk (Source, Address + Done, Width))
      {
        break;
      }
    }

    Status = RwDsRead (Source, Address + Done, Width, &Value);
    for (Index = 0; Index < Width; Index++) {
      if (EFI_ERROR (Status) && (Width > 1) && ((Source->WidthMask & RW_DS_WIDTH_8) != 0)) {
        //
        // A wide access failed: find the bytes that do read.
        //
        if (EFI_ERROR (RwDsRead (Source, Address + Done + Index, 1, &Byte))) {
          Buffer[Done + Index] = 0xFF;
          Result               = EFI_DEVICE_ERROR;
          continue;
        }

        Buffer[Done + Index] = (UINT8)Byte;
      } else if (EFI_ERROR (Status)) {
        Buffer[Done + Index] = 0xFF;
        Result               = Status;
        continue;
      } else {
        Buffer[Done + Index] = (UINT8)RShiftU64 (Value, Index * 8);
      }

      if (Valid != NULL) {
        Valid[(Done + Index) / 8] |= (UINT8)(1 << ((Done + Index) % 8));
      }
    }
  }

  return Result;
}
