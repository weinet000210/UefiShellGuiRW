/** @file
  RwCoreLib Super I/O.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

STATIC CONST CHAR8  *mIteLdn[] = {
  NULL, "Serial port 1", "Serial port 2", "Parallel port", "Environment controller",
  "KBC (keyboard)", "KBC (mouse)", "GPIO", NULL, NULL, "CIR"
};

STATIC CONST CHAR8  *mNuvotonLdn[] = {
  "FDC", "Parallel port", "UART A", "UART B", NULL, "KBC", "CIR", "GPIO 6-8",
  "WDT1 / GPIO 0", "GPIO 1-8", "ACPI", "Hardware monitor", "Vendor", "WDT / Vendor",
  "CIR wake-up", "GPIO push-pull / open-drain", NULL, NULL, NULL, NULL, "Port 80 UART",
  NULL, "Deep sleep"
};

STATIC CONST CHAR8  *mAspeedLdn[] = {
  NULL, NULL, "SUART1", "SUART2", "System wake-up control", "KBC", NULL, "GPIO",
  NULL, NULL, NULL, "SUART3", "SUART4", "iLPC2AHB", "Mailbox"
};

STATIC
VOID
SioEnter (
  IN CONST RW_SIO_CHIP  *C
  )
{
  switch (C->Kind) {
    case RW_SIO_ITE:
      IoWrite8 (C->Port, 0x87);
      IoWrite8 (C->Port, 0x01);
      IoWrite8 (C->Port, 0x55);
      IoWrite8 (C->Port, (C->Port == 0x2E) ? 0x55 : 0xAA);
      break;
    case RW_SIO_SMSC:
      IoWrite8 (C->Port, 0x55);
      break;
    case RW_SIO_ASPEED:
      IoWrite8 (C->Port, 0xA5);
      IoWrite8 (C->Port, 0xA5);
      break;
    default:
      IoWrite8 (C->Port, 0x87);
      IoWrite8 (C->Port, 0x87);
      break;
  }
}

STATIC
UINT8
SioReg (
  IN CONST RW_SIO_CHIP  *C,
  IN UINT8     Reg
  )
{
  IoWrite8 (C->Port, Reg);
  return IoRead8 (C->Port + 1);
}

STATIC
VOID
SioSetReg (
  IN CONST RW_SIO_CHIP  *C,
  IN UINT8     Reg,
  IN UINT8     Value
  )
{
  IoWrite8 (C->Port, Reg);
  IoWrite8 (C->Port + 1, Value);
}

STATIC
VOID
SioExit (
  IN CONST RW_SIO_CHIP  *C
  )
{
  if (C->Kind == RW_SIO_ITE) {
    SioSetReg (C, 0x02, 0x02);
  } else {
    IoWrite8 (C->Port, 0xAA);
  }
}

BOOLEAN
RwSioProbe (
  IN  UINT16       Port,
  OUT RW_SIO_CHIP  *C
  )
{
  UINT16  Id;

  ZeroMem (C, sizeof (*C));
  C->Port = Port;

  //
  // ASPEED BMC Super I/O: A5h A5h unlocks, AAh locks. Lock first so a chip
  // the firmware left unlocked does not answer every sequence; an ITE part
  // left unlocked is recognised by its ID below instead.
  //
  IoWrite8 (Port, 0xAA);
  C->Kind = RW_SIO_ASPEED;
  SioEnter (C);
  Id = (UINT16)((SioReg (C, 0x20) << 8) | SioReg (C, 0x21));
  if ((Id != 0xFFFF) && (Id != 0x0000) && ((Id >> 8) < 0x85 || (Id >> 8) > 0x88)) {
    SioSetReg (C, 0x07, 0x02);
    if (SioReg (C, 0x07) == 0x02) {
      C->Id  = Id;
      C->Rev = 0;
      SioExit (C);
      //
      // A Nuvoton/Winbond/Fintek part does not unlock on A5h; make sure
      // this is not one that was already unlocked.
      //
      if ((UINT16)((SioReg (C, 0x20) << 8) | SioReg (C, 0x21)) == 0xFFFF) {
        return TRUE;
      }
    }
  }

  SioExit (C);

  //
  // Nuvoton / Winbond / Fintek: 87h 87h.
  //
  C->Kind = RW_SIO_NUVOTON;
  SioEnter (C);
  Id = (UINT16)((SioReg (C, 0x20) << 8) | SioReg (C, 0x21));
  if ((Id != 0xFFFF) && (Id != 0x0000)) {
    if (((SioReg (C, 0x23) << 8) | SioReg (C, 0x24)) == 0x1934) {
      C->Kind = RW_SIO_FINTEK;
      C->Rev  = 0;
    } else {
      C->Rev = (UINT8)(Id & 0x0F);
    }

    C->Id = Id;
    SioExit (C);
    return TRUE;
  }

  SioExit (C);

  //
  // ITE: 87h 01h 55h 55h (2Eh) / 87h 01h 55h AAh (4Eh).
  //
  C->Kind = RW_SIO_ITE;
  SioEnter (C);
  Id = (UINT16)((SioReg (C, 0x20) << 8) | SioReg (C, 0x21));
  if (((Id >> 8) == 0x85) || ((Id >> 8) == 0x86) || ((Id >> 8) == 0x87) || ((Id >> 8) == 0x88)) {
    C->Id  = Id;
    C->Rev = SioReg (C, 0x22) & 0x0F;
    SioExit (C);
    return TRUE;
  }

  SioExit (C);

  //
  // SMSC / Microchip: 55h, one-byte device ID at 20h.
  //
  C->Kind = RW_SIO_SMSC;
  SioEnter (C);
  Id = SioReg (C, 0x20);
  if ((Id != 0xFF) && (Id != 0x00)) {
    C->Id  = Id;
    C->Rev = SioReg (C, 0x21);
    SioExit (C);
    return TRUE;
  }

  IoWrite8 (C->Port, 0xAA);
  return FALSE;
}

CONST CHAR8 *
RwSioVendorName (
  IN CONST RW_SIO_CHIP  *Chip
  )
{
  switch (Chip->Kind) {
    case RW_SIO_NUVOTON: return "Nuvoton/Winbond";
    case RW_SIO_FINTEK:  return "Fintek";
    case RW_SIO_ITE:     return "ITE";
    case RW_SIO_ASPEED:  return "ASPEED BMC";
    default:             return "SMSC/Microchip";
  }
}

CONST CHAR8 *
RwSioLdnName (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn
  )
{
  CONST CHAR8  *Name;

  Name = NULL;
  if ((Chip->Kind == RW_SIO_ITE) && (Ldn < ARRAY_SIZE (mIteLdn))) {
    Name = mIteLdn[Ldn];
  } else if ((Chip->Kind == RW_SIO_ASPEED) && (Ldn < ARRAY_SIZE (mAspeedLdn))) {
    Name = mAspeedLdn[Ldn];
  } else if ((Chip->Kind == RW_SIO_NUVOTON) && (Ldn < ARRAY_SIZE (mNuvotonLdn))) {
    Name = mNuvotonLdn[Ldn];
  }

  return (Name != NULL) ? Name : "";
}

VOID
RwSioDescribe (
  IN  CONST RW_SIO_CHIP  *Chip,
  OUT CHAR8              *Buffer,
  IN  UINTN              BufferSize
  )
{
  if (Chip->Kind == RW_SIO_ITE) {
    AsciiSPrint (Buffer, BufferSize, "%a IT%04X  rev %X  at %02Xh", RwSioVendorName (Chip), Chip->Id, Chip->Rev, Chip->Port);
  } else if (Chip->Kind == RW_SIO_SMSC) {
    AsciiSPrint (Buffer, BufferSize, "%a  device ID %02Xh  rev %02Xh  at %02Xh", RwSioVendorName (Chip), Chip->Id, Chip->Rev, Chip->Port);
  } else {
    AsciiSPrint (Buffer, BufferSize, "%a  chip ID %04Xh  at %02Xh", RwSioVendorName (Chip), Chip->Id, Chip->Port);
  }
}

UINT8
RwSioRead (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn,
  IN UINT8              Reg
  )
{
  UINT8  Value;

  SioEnter (Chip);
  SioSetReg (Chip, 0x07, (Ldn == RW_SIO_GLOBAL) ? 0 : Ldn);
  Value = SioReg (Chip, Reg);
  SioExit (Chip);
  return Value;
}

VOID
RwSioWrite (
  IN CONST RW_SIO_CHIP  *Chip,
  IN UINT8              Ldn,
  IN UINT8              Reg,
  IN UINT8              Value
  )
{
  SioEnter (Chip);
  SioSetReg (Chip, 0x07, (Ldn == RW_SIO_GLOBAL) ? 0 : Ldn);
  SioSetReg (Chip, Reg, Value);
  SioExit (Chip);
}

EFI_STATUS
RwSioScan (
  OUT RW_SIO_LIST  *List
  )
{
  STATIC CONST UINT16  Ports[] = { 0x2E, 0x4E };
  UINTN                Index;
  UINT8                Ldn;
  RW_SIO_CHIP_INFO     *Ci;
  RW_SIO_CHIP          *C;
  RW_SIO_LDN           *L;
  UINT8                Act;
  UINT16               Base1;
  UINT8                Irq;

  ZeroMem (List, sizeof (*List));
  for (Index = 0; Index < ARRAY_SIZE (Ports); Index++) {
    Ci = &List->Chips[List->ChipCount];
    if (!RwSioProbe (Ports[Index], &Ci->Chip)) {
      continue;
    }

    List->ChipCount++;
    C = &Ci->Chip;
    SioEnter (C);
    for (Ldn = 0; Ldn < RW_SIO_MAX_LDN; Ldn++) {
      SioSetReg (C, 0x07, Ldn);
      if (SioReg (C, 0x07) != Ldn) {
        continue;
      }

      Act   = SioReg (C, 0x30);
      Base1 = (UINT16)((SioReg (C, 0x60) << 8) | SioReg (C, 0x61));
      Irq   = SioReg (C, 0x70);
      if ((Act == 0xFF) && (Base1 == 0xFFFF) && (Irq == 0xFF)) {
        continue;
      }

      L         = &Ci->Ldns[Ci->LdnCount++];
      L->Ldn    = Ldn;
      L->Active = (BOOLEAN)((Act & 1) != 0);
      L->Base1  = Base1;
      L->Base2  = (UINT16)((SioReg (C, 0x62) << 8) | SioReg (C, 0x63));
      L->Irq    = Irq;
      L->Irq2   = SioReg (C, 0x72);
    }

    SioExit (C);
  }

  return (List->ChipCount == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

VOID
RwSioDecode (
  IN     CONST RW_SIO_LIST  *List,
  IN OUT RW_INFO            *Info
  )
{
  UINTN                   Index;
  UINTN                   L;
  CONST RW_SIO_CHIP_INFO  *Ci;
  CONST RW_SIO_LDN        *D;
  CHAR8                   Title[80];
  CHAR8                   Name[16];

  if (List->ChipCount == 0) {
    RwInfoText (Info, 0, RwEmAlert, "No Super I/O answered on 2Eh/4Eh (Nuvoton/Winbond, Fintek, ITE, SMSC, ASPEED sequences).");
    RwInfoText (Info, 0, RwEmNormal, "Many laptops and servers have none, or keep it behind the EC/BMC.");
    return;
  }

  for (Index = 0; Index < List->ChipCount; Index++) {
    Ci = &List->Chips[Index];
    RwSioDescribe (&Ci->Chip, Title, sizeof (Title));
    RwInfoSetTag (RwInfoSection (Info, 0, "%a", Title), (INT32)(Index * 256 + RW_SIO_GLOBAL));
    for (L = 0; L < Ci->LdnCount; L++) {
      D = &Ci->Ldns[L];
      AsciiSPrint (Name, sizeof (Name), "LDN %02Xh", D->Ldn);
      RwInfoSetTag (
        RwInfoField (
          Info,
          1,
          D->Active ? RwEmNormal : RwEmDim,
          Name,
          "%a  base %04Xh %04Xh  IRQ %d %d  %a",
          D->Active ? "active" : "off   ",
          D->Base1,
          D->Base2,
          D->Irq & 0x0F,
          D->Irq2 & 0x0F,
          RwSioLdnName (&Ci->Chip, D->Ldn)
          ),
        (INT32)(Index * 256 + D->Ldn)
        );
    }
  }
}

typedef struct {
  RW_SIO_CHIP    Chip;
  UINT8          Ldn;
} SIO_DS_CTX;

STATIC
EFI_STATUS
EFIAPI
SioDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  SIO_DS_CTX  *X;
  UINTN       Index;

  X      = (SIO_DS_CTX *)Source->Ctx;
  *Value = 0;
  SioEnter (&X->Chip);
  SioSetReg (&X->Chip, 0x07, (X->Ldn == RW_SIO_GLOBAL) ? 0 : X->Ldn);
  for (Index = 0; Index < Width; Index++) {
    *Value |= LShiftU64 (SioReg (&X->Chip, (UINT8)(Address + Index)), 8 * Index);
  }

  SioExit (&X->Chip);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SioDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  SIO_DS_CTX  *X;
  UINTN       Index;

  X = (SIO_DS_CTX *)Source->Ctx;
  SioEnter (&X->Chip);
  SioSetReg (&X->Chip, 0x07, (X->Ldn == RW_SIO_GLOBAL) ? 0 : X->Ldn);
  for (Index = 0; Index < Width; Index++) {
    SioSetReg (&X->Chip, (UINT8)(Address + Index), (UINT8)RShiftU64 (Value, 8 * Index));
  }

  SioExit (&X->Chip);
  return EFI_SUCCESS;
}

VOID
RwSioSourceInit (
  OUT RW_DATA_SOURCE     *Source,
  IN  CONST RW_SIO_CHIP  *Chip,
  IN  UINT8              Ldn
  )
{
  SIO_DS_CTX  *X;

  ZeroMem (Source, sizeof (*Source));
  X       = (SIO_DS_CTX *)Source->Ctx;
  X->Chip = *Chip;
  X->Ldn  = Ldn;
  AsciiSPrint (
    Source->Name,
    sizeof (Source->Name),
    "Super I/O %Xh  LDN %02Xh  %a",
    Chip->Port,
    (Ldn == RW_SIO_GLOBAL) ? 0 : Ldn,
    (Ldn == RW_SIO_GLOBAL) ? "(global registers)" : RwSioLdnName (Chip, Ldn)
    );
  Source->Base        = 0;
  Source->Limit       = 0xFF;
  Source->AddrDigits  = 2;
  Source->WidthMask   = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32;
  Source->SideEffects = TRUE;
  Source->Read        = SioDsRead;
  Source->Write       = SioDsWrite;
}
