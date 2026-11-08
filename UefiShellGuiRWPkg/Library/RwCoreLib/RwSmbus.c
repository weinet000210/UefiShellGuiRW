/** @file
  RwCoreLib SMBus host controller and SPD.

  Intel ICH / PCH and AMD SB / FCH SMBus hosts share the PIIX4 register
  layout: status 0, control 2, command 3, slave address 4, data 0 at 5.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define SMB_STS   0
#define SMB_CNT   2
#define SMB_CMD   3
#define SMB_ADDR  4
#define SMB_D0    5

#define STS_BUSY    BIT0
#define STS_INTR    BIT1
#define STS_DEVERR  BIT2
#define STS_BUSERR  BIT3
#define STS_FAILED  BIT4
#define STS_ERRORS  (STS_DEVERR | STS_BUSERR | STS_FAILED)

#define CNT_START      BIT6
#define CNT_KILL       BIT1
#define PROT_QUICK     0x00
#define PROT_BYTEDATA  0x08

#define SMB_POLL_US     10
#define SMB_POLL_COUNT  10000       // 100 ms

//
// ---- Controller ----------------------------------------------------------
//

STATIC
UINT8
AmdPmRead (
  IN UINT8  Reg
  )
{
  IoWrite8 (0xCD6, Reg);
  return IoRead8 (0xCD7);
}

EFI_STATUS
RwSmbusFind (
  OUT RW_SMBUS  *Bus
  )
{
  RW_PCI_LIST    List;
  RW_PCI_DEVICE  *D;
  UINTN          Index;
  UINT32         Bar;
  EFI_STATUS     Status;

  ZeroMem (Bus, sizeof (*Bus));
  if (EFI_ERROR (RwPciScan (&List)) && (List.DeviceCount == 0)) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < List.DeviceCount; Index++) {
    D = &List.Devices[Index];
    if ((D->BaseClass != 0x0C) || (D->SubClass != 0x05)) {
      continue;
    }

    if ((D->VendorId != 0x8086) && (D->VendorId != 0x1022) && (D->VendorId != 0x1002)) {
      continue;
    }

    Bus->VendorId = D->VendorId;
    Bus->DeviceId = D->DeviceId;
    Bus->Segment  = D->Segment;
    Bus->Bus      = D->Bus;
    Bus->Dev      = D->Dev;
    Bus->Func     = D->Func;
    if (D->VendorId == 0x8086) {
      Bus->Kind        = RW_SMBUS_INTEL;
      Bar              = RwPciRd (D, 0x20, 4);
      Bus->Base        = ((Bar & BIT0) != 0) ? (UINT16)(Bar & 0xFFE0) : 0;
      Bus->HostEnabled = (BOOLEAN)((RwPciRd (D, 0x40, 1) & BIT0) != 0);
    } else {
      Bus->Kind        = RW_SMBUS_AMD;
      Bus->HostEnabled = TRUE;
      if ((D->DeviceId == 0x790B) || ((D->DeviceId == 0x780B) && (D->RevisionId >= 0x49))) {
        //
        // FCH (Kerncz and later): PM 00h bit 4 enables the decode, PM 01h
        // holds bits 15:8 of the base.
        //
        Bus->Base = ((AmdPmRead (0x00) & BIT4) != 0) ? (UINT16)(AmdPmRead (0x01) << 8) : 0;
      } else {
        Bus->Base = (UINT16)((AmdPmRead (0x2C) | (AmdPmRead (0x2D) << 8)) & 0xFFE0);
      }

      if (Bus->Base == 0) {
        Bus->Base = 0x0B00;
      }
    }

    AsciiSPrint (
      Bus->Name,
      sizeof (Bus->Name),
      "%a SMBus %04X:%04X at %02X:%02X.%X, I/O %04Xh",
      (Bus->Kind == RW_SMBUS_INTEL) ? "Intel" : "AMD",
      Bus->VendorId,
      Bus->DeviceId,
      Bus->Bus,
      Bus->Dev,
      Bus->Func,
      Bus->Base
      );
    Bus->IoDecode = (BOOLEAN)((RwPciRd (D, 0x04, 2) & BIT0) != 0);
    CopyMem (&Bus->Pci, D, sizeof (*D));
    Status = ((Bus->Base != 0) && Bus->HostEnabled && Bus->IoDecode && (IoRead8 (Bus->Base + SMB_STS) != 0xFF)) ? EFI_SUCCESS : EFI_NOT_READY;
    break;
  }

  RwPciFree (&List);
  return Status;
}

EFI_STATUS
RwSmbusEnable (
  IN OUT RW_SMBUS  *Bus
  )
{
  if ((Bus->Pci.RootBridge == NULL) || (Bus->Base == 0)) {
    return EFI_NOT_READY;
  }

  if (!Bus->IoDecode) {
    RwPciCfgWrite (&Bus->Pci, 0x04, 2, RwPciRd (&Bus->Pci, 0x04, 2) | BIT0);
    Bus->IoDecode = (BOOLEAN)((RwPciRd (&Bus->Pci, 0x04, 2) & BIT0) != 0);
  }

  if ((Bus->Kind == RW_SMBUS_INTEL) && !Bus->HostEnabled) {
    RwPciCfgWrite (&Bus->Pci, 0x40, 1, RwPciRd (&Bus->Pci, 0x40, 1) | BIT0);
    Bus->HostEnabled = (BOOLEAN)((RwPciRd (&Bus->Pci, 0x40, 1) & BIT0) != 0);
  }

  return (Bus->IoDecode && Bus->HostEnabled && (IoRead8 (Bus->Base + SMB_STS) != 0xFF)) ? EFI_SUCCESS : EFI_NOT_READY;
}

STATIC
EFI_STATUS
Transaction (
  IN     CONST RW_SMBUS  *Bus,
  IN     UINT8           Addr,
  IN     BOOLEAN         Read,
  IN     UINT8           Protocol,
  IN     UINT8           Command,
  IN OUT UINT8           *Data
  )
{
  UINT16  Base;
  UINT8   Sts;
  UINTN   Count;

  if ((Bus == NULL) || (Bus->Base == 0) || (Addr > 0x7F)) {
    return EFI_INVALID_PARAMETER;
  }

  Base = Bus->Base;
  Sts  = IoRead8 (Base + SMB_STS);
  if ((Sts & STS_BUSY) != 0) {
    //
    // A transaction is stuck: stop it and give up this time.
    //
    IoWrite8 (Base + SMB_CNT, CNT_KILL);
    gBS->Stall (100);
    IoWrite8 (Base + SMB_CNT, 0);
    IoWrite8 (Base + SMB_STS, 0xFE);
    return EFI_NOT_READY;
  }

  IoWrite8 (Base + SMB_STS, 0xFE);
  IoWrite8 (Base + SMB_ADDR, (UINT8)((Addr << 1) | (Read ? 1 : 0)));
  IoWrite8 (Base + SMB_CMD, Command);
  if (!Read && (Data != NULL)) {
    IoWrite8 (Base + SMB_D0, *Data);
  }

  IoWrite8 (Base + SMB_CNT, (UINT8)(CNT_START | Protocol));
  for (Count = 0; Count < SMB_POLL_COUNT; Count++) {
    gBS->Stall (SMB_POLL_US);
    Sts = IoRead8 (Base + SMB_STS);
    if (((Sts & STS_BUSY) == 0) && ((Sts & (STS_INTR | STS_ERRORS)) != 0)) {
      break;
    }
  }

  IoWrite8 (Base + SMB_STS, 0xFE);
  if (Count == SMB_POLL_COUNT) {
    IoWrite8 (Base + SMB_CNT, CNT_KILL);
    gBS->Stall (100);
    IoWrite8 (Base + SMB_CNT, 0);
    IoWrite8 (Base + SMB_STS, 0xFE);
    return EFI_TIMEOUT;
  }

  if ((Sts & STS_DEVERR) != 0) {
    return EFI_NO_RESPONSE;
  }

  if ((Sts & (STS_BUSERR | STS_FAILED)) != 0) {
    return EFI_DEVICE_ERROR;
  }

  if (Read && (Data != NULL)) {
    *Data = IoRead8 (Base + SMB_D0);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RwSmbusReadByte (
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr,
  IN  UINT8           Command,
  OUT UINT8           *Value
  )
{
  *Value = 0xFF;
  return Transaction (Bus, Addr, TRUE, PROT_BYTEDATA, Command, Value);
}

EFI_STATUS
RwSmbusWriteByte (
  IN CONST RW_SMBUS  *Bus,
  IN UINT8           Addr,
  IN UINT8           Command,
  IN UINT8           Value
  )
{
  return Transaction (Bus, Addr, FALSE, PROT_BYTEDATA, Command, &Value);
}

EFI_STATUS
RwSmbusQuickWrite (
  IN CONST RW_SMBUS  *Bus,
  IN UINT8           Addr
  )
{
  return Transaction (Bus, Addr, FALSE, PROT_QUICK, 0, NULL);
}

UINTN
RwSmbusScan (
  IN  CONST RW_SMBUS  *Bus,
  OUT BOOLEAN         Present[128]
  )
{
  UINT8  Addr;
  UINT8  Value;
  UINTN  Count;

  ZeroMem (Present, 128 * sizeof (BOOLEAN));
  Count = 0;
  for (Addr = RW_SMBUS_FIRST_ADDR; Addr <= RW_SMBUS_LAST_ADDR; Addr++) {
    if (!EFI_ERROR (RwSmbusReadByte (Bus, Addr, 0, &Value))) {
      Present[Addr] = TRUE;
      Count++;
    }
  }

  return Count;
}

CONST CHAR8 *
RwSmbusAddrHint (
  IN UINT8  Addr
  )
{
  if ((Addr >= 0x50) && (Addr <= 0x57)) {
    return "SPD EEPROM";
  }

  if ((Addr >= 0x18) && (Addr <= 0x1F)) {
    return "Thermal sensor (TSOD)";
  }

  if ((Addr >= 0x30) && (Addr <= 0x37)) {
    return "SPD write protect / page";
  }

  if ((Addr >= 0x48) && (Addr <= 0x4F)) {
    return "SPD5 hub / temperature";
  }

  if ((Addr >= 0x2C) && (Addr <= 0x2F)) {
    return "Hardware monitor";
  }

  if ((Addr == 0x69) || (Addr == 0x6A) || (Addr == 0x68)) {
    return "Clock generator";
  }

  if (Addr == 0x08) {
    return "SMBus host / battery charger";
  }

  if (Addr == 0x0B) {
    return "Smart battery";
  }

  if (Addr == 0x44) {
    return "ME / SMBus slave";
  }

  return "";
}

//
// Data source over one device.
//
typedef struct {
  UINT16    Base;
  UINT8     Addr;
} SMB_DS;

STATIC
VOID
DsBus (
  IN  CONST SMB_DS  *P,
  OUT RW_SMBUS      *Bus
  )
{
  ZeroMem (Bus, sizeof (*Bus));
  Bus->Base = P->Base;
}

STATIC
EFI_STATUS
EFIAPI
SmbDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  SMB_DS      *P;
  RW_SMBUS    Bus;
  UINT8       Byte;
  EFI_STATUS  Status;

  P = (SMB_DS *)Source->Ctx;
  DsBus (P, &Bus);
  Status = RwSmbusReadByte (&Bus, P->Addr, (UINT8)Address, &Byte);
  *Value = EFI_ERROR (Status) ? 0 : Byte;
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
SmbDsWrite (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width,
  IN UINT64          Value
  )
{
  SMB_DS    *P;
  RW_SMBUS  Bus;

  P = (SMB_DS *)Source->Ctx;
  DsBus (P, &Bus);
  return RwSmbusWriteByte (&Bus, P->Addr, (UINT8)Address, (UINT8)Value);
}

STATIC
BOOLEAN
EFIAPI
SmbDsDangerous (
  IN RW_DATA_SOURCE  *Source,
  IN UINT64          Address,
  IN UINTN           Width
  )
{
  return TRUE;
}

VOID
RwSmbusSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr
  )
{
  SMB_DS  *P;

  STATIC_ASSERT (sizeof (SMB_DS) <= RW_DS_CTX_MAX, "SMB_DS too large");
  ZeroMem (Source, sizeof (*Source));
  AsciiSPrint (Source->Name, sizeof (Source->Name), "SMBus device %02Xh", Addr);
  Source->Base        = 0;
  Source->Limit       = 0xFF;
  Source->AddrDigits  = 2;
  Source->WidthMask   = RW_DS_WIDTH_8;
  Source->SideEffects = TRUE;
  Source->Read        = SmbDsRead;
  Source->Write       = SmbDsWrite;
  Source->IsDangerous = SmbDsDangerous;
  P                   = (SMB_DS *)Source->Ctx;
  P->Base             = Bus->Base;
  P->Addr             = Addr;
}
