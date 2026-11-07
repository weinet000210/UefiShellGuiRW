/** @file
  ACPI tables: RSDP -> XSDT/RSDT -> every table (plus DSDT and FACS from the
  FADT), with checksum check, header decode, field decode for FADT, MADT,
  MCFG, HPET, DMAR, and a raw dump of every table.

  Ported from uefirrw AcpiView.c.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/UefiLib.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Acpi.h>

STATIC
UINT16
R16 (
  IN CONST UINT8  *P
  )
{
  return (UINT16)(P[0] | (P[1] << 8));
}

STATIC
UINT32
R32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

STATIC
UINT64
R64 (
  IN CONST UINT8  *P
  )
{
  return (UINT64)R32 (P) | LShiftU64 (R32 (P + 4), 32);
}

UINT8
RwAcpiChecksum (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT8  S;

  S = 0;
  while (Length-- > 0) {
    S = (UINT8)(S + *Data++);
  }

  return S;
}

/**
  Copy N bytes of ASCII into a CHAR8 string, replacing non-printables by ' '.
**/
STATIC
VOID
AsciiField (
  OUT CHAR8        *Out,
  IN  CONST UINT8  *In,
  IN  UINTN        N
  )
{
  UINTN  Index;

  for (Index = 0; Index < N; Index++) {
    Out[Index] = ((In[Index] >= 0x20) && (In[Index] < 0x7F)) ? (CHAR8)In[Index] : ' ';
  }

  Out[N] = '\0';
}

//
// ---- Scan ---------------------------------------------------------------------
//

STATIC
BOOLEAN
Known (
  IN CONST RW_ACPI_TABLES  *Tables,
  IN UINT8                 *Ptr
  )
{
  UINTN  Index;

  for (Index = 0; Index < Tables->Count; Index++) {
    if (Tables->Tables[Index].Ptr == Ptr) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
AddTable (
  IN OUT RW_ACPI_TABLES  *Tables,
  IN     UINT64          Address,
  IN     BOOLEAN         HasHeader
  )
{
  UINT8          *Ptr;
  UINT32         Len;
  RW_ACPI_TABLE  *E;

  if ((Address == 0) || (Address > MAX_ADDRESS) || (Tables->Count >= RW_ACPI_MAX_TABLES)) {
    return;
  }

  Ptr = (UINT8 *)(UINTN)Address;
  if (Known (Tables, Ptr)) {
    return;
  }

  Len = R32 (Ptr + 4);
  if ((Len < (HasHeader ? 36u : 8u)) || (Len > 16 * 1024 * 1024)) {
    return;
  }

  E            = &Tables->Tables[Tables->Count];
  E->Ptr       = Ptr;
  E->Address   = Address;
  E->Length    = Len;
  E->HasHeader = HasHeader;
  AsciiField (E->Signature, Ptr, 4);
  if (HasHeader) {
    AsciiField (E->OemId, Ptr + 10, 6);
    AsciiField (E->OemTableId, Ptr + 16, 8);
    E->Revision   = Ptr[8];
    E->ChecksumOk = (BOOLEAN)(RwAcpiChecksum (Ptr, Len) == 0);
  } else {
    AsciiStrCpyS (E->OemId, sizeof (E->OemId), "      ");
    AsciiStrCpyS (E->OemTableId, sizeof (E->OemTableId), "        ");
    E->Revision   = (Len > 32) ? Ptr[32] : 0;    // FACS version
    E->ChecksumOk = TRUE;
  }

  Tables->Count++;
}

EFI_STATUS
RwAcpiScan (
  OUT RW_ACPI_TABLES  *Tables
  )
{
  UINT8          *Rsdp;
  UINT8          *Sdt;
  UINT32         Len;
  UINTN          Entry;
  UINTN          Index;
  UINT8          *Fadt;
  BOOLEAN        Xsdt;

  ZeroMem (Tables, sizeof (*Tables));
  Rsdp = NULL;
  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL)) {
    if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi10TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL)) {
      return EFI_NOT_FOUND;
    }
  }

  Tables->Tables = AllocateZeroPool (RW_ACPI_MAX_TABLES * sizeof (RW_ACPI_TABLE));
  if (Tables->Tables == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Xsdt                 = (BOOLEAN)((Rsdp[15] >= 2) && (R64 (Rsdp + 24) != 0));
  Tables->Rsdp         = Rsdp;
  Tables->RsdpRevision = Rsdp[15];
  Tables->UsesXsdt     = Xsdt;
  Tables->SdtAddress   = Xsdt ? R64 (Rsdp + 24) : (UINT64)R32 (Rsdp + 16);
  AsciiField (Tables->RsdpOemId, Rsdp + 9, 6);

  if ((Tables->SdtAddress == 0) || (Tables->SdtAddress > MAX_ADDRESS)) {
    return EFI_SUCCESS;
  }

  Sdt = (UINT8 *)(UINTN)Tables->SdtAddress;
  AddTable (Tables, (UINTN)Sdt, TRUE);
  Len = R32 (Sdt + 4);
  for (Entry = 36; Entry + (Xsdt ? 8 : 4) <= Len; Entry += (Xsdt ? 8 : 4)) {
    AddTable (Tables, Xsdt ? R64 (Sdt + Entry) : R32 (Sdt + Entry), TRUE);
  }

  //
  // DSDT and FACS are only referenced from the FADT.
  //
  for (Index = 0; Index < Tables->Count; Index++) {
    Fadt = Tables->Tables[Index].Ptr;
    if (CompareMem (Fadt, "FACP", 4) != 0) {
      continue;
    }

    Len = Tables->Tables[Index].Length;
    if ((Len >= 148) && (R64 (Fadt + 140) != 0)) {
      AddTable (Tables, R64 (Fadt + 140), TRUE);
    } else if (Len >= 44) {
      AddTable (Tables, R32 (Fadt + 40), TRUE);
    }

    if ((Len >= 140) && (R64 (Fadt + 132) != 0)) {
      AddTable (Tables, R64 (Fadt + 132), FALSE);
    } else if (Len >= 40) {
      AddTable (Tables, R32 (Fadt + 36), FALSE);
    }
  }

  return EFI_SUCCESS;
}

VOID
RwAcpiFree (
  IN OUT RW_ACPI_TABLES  *Tables
  )
{
  if (Tables->Tables != NULL) {
    FreePool (Tables->Tables);
  }

  ZeroMem (Tables, sizeof (*Tables));
}

RW_ACPI_TABLE *
RwAcpiFindTable (
  IN CONST RW_ACPI_TABLES  *Tables,
  IN CONST CHAR8           *Signature,
  IN UINTN                 Instance
  )
{
  UINTN  Index;

  if ((Tables == NULL) || (Signature == NULL) || (AsciiStrLen (Signature) < 4)) {
    return NULL;
  }

  for (Index = 0; Index < Tables->Count; Index++) {
    if (CompareMem (Tables->Tables[Index].Ptr, Signature, 4) == 0) {
      if (Instance == 0) {
        return &Tables->Tables[Index];
      }

      Instance--;
    }
  }

  return NULL;
}

BOOLEAN
RwAcpiIsAml (
  IN CONST RW_ACPI_TABLE  *Table
  )
{
  return (BOOLEAN)((Table->Ptr != NULL) && Table->HasHeader &&
                   ((CompareMem (Table->Ptr, "DSDT", 4) == 0) || (CompareMem (Table->Ptr, "SSDT", 4) == 0)));
}

VOID
RwAcpiFileName (
  IN  CONST RW_ACPI_TABLE  *Table,
  IN  UINTN                Number,
  IN  CONST CHAR8          *Extension,
  OUT CHAR8                *Buffer,
  IN  UINTN                Size
  )
{
  AsciiSPrint (
    Buffer,
    Size,
    "%c%c%c%c%d.%a",
    Table->Ptr[0],
    Table->Ptr[1],
    Table->Ptr[2],
    Table->Ptr[3],
    Number,
    Extension
    );
}

VOID
RwAcpiListInfo (
  IN     CONST RW_ACPI_TABLES  *Tables,
  IN OUT RW_INFO               *Info
  )
{
  UINTN                Index;
  CONST RW_ACPI_TABLE  *E;
  RW_INFO_ROW          *Row;

  if (Tables->Rsdp == NULL) {
    RwInfoText (Info, 0, RwEmAlert, "No ACPI tables published by the firmware.");
    return;
  }

  RwInfoSection (Info, 0, "RSDP");
  RwInfoField (Info, 1, RwEmNormal, "Address", "%lX", (UINT64)(UINTN)Tables->Rsdp);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Revision", "%d", Tables->RsdpRevision), 15, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "OEM ID", "%a", Tables->RsdpOemId), 9, 6);
  RwInfoSetReg (
    RwInfoField (Info, 1, RwEmNormal, Tables->UsesXsdt ? "XSDT" : "RSDT", "%lX", Tables->SdtAddress),
    Tables->UsesXsdt ? 24 : 16,
    Tables->UsesXsdt ? 8 : 4
    );

  RwInfoSection (Info, 0, "Tables (%d)", Tables->Count);
  for (Index = 0; Index < Tables->Count; Index++) {
    E   = &Tables->Tables[Index];
    Row = RwInfoField (
            Info,
            1,
            (E->HasHeader && !E->ChecksumOk) ? RwEmAlert : RwEmNormal,
            E->Signature,
            "%-6a  %-8a  rev %3d  %8X  %12lX  %a",
            E->OemId,
            E->OemTableId,
            E->Revision,
            E->Length,
            (UINT64)(UINTN)E->Ptr,
            E->HasHeader ? (E->ChecksumOk ? "OK" : "BAD CHECKSUM") : "-"
            );
    RwInfoSetTag (Row, (INT32)Index);
  }
}

//
// ---- Decoders ----------------------------------------------------------------------
//

STATIC CONST CHAR8  *mSpaceNames[] = { "Memory", "IO", "PCI Config", "EC", "SMBus", "CMOS", "PCI BAR", "IPMI", "GPIO", "Serial", "PCC", "PRM" };

/**
  Generic Address Structure at T + Off as one field.
**/
STATIC
VOID
AddGas (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST CHAR8  *Label,
  IN     CONST UINT8  *T,
  IN     UINT32       Off
  )
{
  CONST UINT8  *G;

  G = T + Off;
  if (R64 (G + 4) == 0) {
    RwInfoSetReg (RwInfoField (Info, Level, RwEmDim, Label, "(not present)"), Off, 12);
    return;
  }

  RwInfoSetReg (
    RwInfoField (
      Info,
      Level,
      RwEmNormal,
      Label,
      "%a %lX  width %d bits, offset %d, access %d",
      (G[0] < ARRAY_SIZE (mSpaceNames)) ? mSpaceNames[G[0]] : "Space?",
      R64 (G + 4),
      G[1],
      G[2],
      G[3]
      ),
    Off,
    12
    );
}

STATIC
VOID
DecodeFadt (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Len
  )
{
  STATIC CONST CHAR8  *Profiles[] = {
    "Unspecified", "Desktop", "Mobile", "Workstation", "Enterprise Server", "SOHO Server",
    "Appliance PC", "Performance Server", "Tablet"
  };
  STATIC CONST CHAR8  *FlagNames[] = {
    "WBINVD", "WBINVD_FLUSH", "PROC_C1", "P_LVL2_UP", "PWR_BUTTON", "SLP_BUTTON", "FIX_RTC",
    "RTC_S4", "TMR_VAL_EXT", "DCK_CAP", "RESET_REG_SUP", "SEALED_CASE", "HEADLESS", "CPU_SW_SLP",
    "PCI_EXP_WAK", "USE_PLATFORM_CLOCK", "S4_RTC_STS_VALID", "REMOTE_POWER_ON", "FORCE_APIC_CLUSTER",
    "FORCE_APIC_PHYS", "HW_REDUCED_ACPI", "LOW_POWER_S0_IDLE", "PERSISTENT_CPU_CACHES"
  };
  UINT32  Flags;
  UINT16  Arch;
  UINTN   Bit;
  CHAR8   Name[16];

  if (Len < 116) {
    return;
  }

  RwInfoSection (Info, 0, "Fixed ACPI Description Table");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "FACS", "%08X", R32 (T + 36)), 36, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "DSDT", "%08X", R32 (T + 40)), 40, 4);
  if (Len >= 148) {
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "X_FACS", "%lX", R64 (T + 132)), 132, 8);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "X_DSDT", "%lX", R64 (T + 140)), 140, 8);
  }

  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM profile", "%a", (T[45] < ARRAY_SIZE (Profiles)) ? Profiles[T[45]] : "Reserved"), 45, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "SCI interrupt", "%d", R16 (T + 46)), 46, 2);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "SMI_CMD", "%04X", R32 (T + 48)), 48, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "ACPI_ENABLE", "%02X", T[52]), 52, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "ACPI_DISABLE", "%02X", T[53]), 53, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "S4BIOS_REQ", "%02X", T[54]), 54, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PSTATE_CNT", "%02X", T[55]), 55, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "CST_CNT", "%02X", T[95]), 95, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1a_EVT_BLK", "%04X", R32 (T + 56)), 56, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1b_EVT_BLK", "%04X", R32 (T + 60)), 60, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1_EVT_LEN", "%d", T[88]), 88, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1a_CNT_BLK", "%04X", R32 (T + 64)), 64, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1b_CNT_BLK", "%04X", R32 (T + 68)), 68, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM1_CNT_LEN", "%d", T[89]), 89, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM2_CNT_BLK", "%04X", R32 (T + 72)), 72, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM2_CNT_LEN", "%d", T[90]), 90, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM_TMR_BLK", "%04X", R32 (T + 76)), 76, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "PM_TMR_LEN", "%d", T[91]), 91, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "GPE0_BLK", "%04X", R32 (T + 80)), 80, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "GPE0_BLK_LEN", "%d", T[92]), 92, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "GPE1_BLK", "%04X", R32 (T + 84)), 84, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "GPE1_BLK_LEN", "%d", T[93]), 93, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "GPE1_BASE", "%d", T[94]), 94, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "C2 latency", "%d us", R16 (T + 96)), 96, 2);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "C3 latency", "%d us", R16 (T + 98)), 98, 2);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Century (CMOS)", "%02X", T[108]), 108, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Day alarm", "%02X", T[106]), 106, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Month alarm", "%02X", T[107]), 107, 1);
  Arch = R16 (T + 109);
  RwInfoSetReg (
    RwInfoField (
      Info,
      1,
      RwEmNormal,
      "IAPC_BOOT_ARCH",
      "%04X %a%a%a%a",
      Arch,
      (Arch & BIT0) ? " LEGACY_DEVICES" : "",
      (Arch & BIT1) ? " 8042" : "",
      (Arch & BIT2) ? " VGA_NOT_PRESENT" : "",
      (Arch & BIT4) ? " PCIE_ASPM_CONTROLS" : ""
      ),
    109,
    2
    );
  Flags = R32 (T + 112);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Flags", "%08X", Flags), 112, 4);
  for (Bit = 0; Bit < ARRAY_SIZE (FlagNames); Bit++) {
    if (Flags & (1u << Bit)) {
      AsciiSPrint (Name, sizeof (Name), "bit %2d", Bit);
      RwInfoSetReg (RwInfoField (Info, 2, RwEmDim, Name, "%a", FlagNames[Bit]), 112 + (UINT32)(Bit / 8), 1);
    }
  }

  if (Len >= 129) {
    AddGas (Info, 1, "RESET_REG", T, 116);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "RESET_VALUE", "%02X", T[128]), 128, 1);
  }

  if (Len >= 132) {
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "FADT minor version", "%d.%d", T[8], T[131] & 0x0F), 131, 1);
  }

  if (Len >= 244) {
    AddGas (Info, 1, "X_PM1a_EVT_BLK", T, 148);
    AddGas (Info, 1, "X_PM1a_CNT_BLK", T, 172);
    AddGas (Info, 1, "X_PM_TMR_BLK", T, 208);
    AddGas (Info, 1, "X_GPE0_BLK", T, 220);
  }

  if (Len >= 268) {
    AddGas (Info, 1, "SLEEP_CONTROL_REG", T, 244);
    AddGas (Info, 1, "SLEEP_STATUS_REG", T, 256);
  }
}

STATIC
VOID
DecodeMadt (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Len
  )
{
  UINT32       Off;
  UINT8        Type;
  UINT8        L;
  UINTN        Cpus;
  UINTN        Enabled;
  UINT32       Flags;
  RW_INFO_ROW  *Row;

  if (Len < 44) {
    return;
  }

  RwInfoSection (Info, 0, "Multiple APIC Description Table");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Local APIC address", "%08X", R32 (T + 36)), 36, 4);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Flags", "%08X%a", R32 (T + 40), (R32 (T + 40) & 1) ? " (PC-AT dual 8259)" : ""), 40, 4);
  Cpus    = 0;
  Enabled = 0;
  for (Off = 44; Off + 2 <= Len; Off += L) {
    Type = T[Off];
    L    = T[Off + 1];
    if ((L < 2) || (Off + L > Len)) {
      break;
    }

    Row = NULL;
    switch (Type) {
      case 0:
        Flags = R32 (T + Off + 4);
        Cpus++;
        Enabled += (Flags & 1) ? 1 : 0;
        Row      = RwInfoField (Info, 1, (Flags & 1) ? RwEmNormal : RwEmDim, "Local APIC", "UID %3d  APIC ID %3d  %a", T[Off + 2], T[Off + 3], (Flags & 1) ? "enabled" : ((Flags & 2) ? "online capable" : "disabled"));
        break;
      case 1:
        Row = RwInfoField (Info, 1, RwEmNormal, "I/O APIC", "ID %d  address %08X  GSI base %d", T[Off + 2], R32 (T + Off + 4), R32 (T + Off + 8));
        break;
      case 2:
        Row = RwInfoField (Info, 1, RwEmNormal, "IRQ override", "bus %d  IRQ %2d -> GSI %2d  flags %04X", T[Off + 2], T[Off + 3], R32 (T + Off + 4), R16 (T + Off + 8));
        break;
      case 3:
        Row = RwInfoField (Info, 1, RwEmNormal, "NMI source", "GSI %d  flags %04X", R32 (T + Off + 4), R16 (T + Off + 2));
        break;
      case 4:
        if (T[Off + 2] == 0xFF) {
          Row = RwInfoField (Info, 1, RwEmNormal, "Local APIC NMI", "UID all  LINT%d  flags %04X", T[Off + 5], R16 (T + Off + 3));
        } else {
          Row = RwInfoField (Info, 1, RwEmNormal, "Local APIC NMI", "UID %d  LINT%d  flags %04X", T[Off + 2], T[Off + 5], R16 (T + Off + 3));
        }

        break;
      case 5:
        Row = RwInfoField (Info, 1, RwEmNormal, "LAPIC override", "address %lX", R64 (T + Off + 4));
        break;
      case 9:
        Flags = R32 (T + Off + 8);
        Cpus++;
        Enabled += (Flags & 1) ? 1 : 0;
        Row      = RwInfoField (Info, 1, (Flags & 1) ? RwEmNormal : RwEmDim, "Local x2APIC", "UID %3d  x2APIC ID %3d  %a", R32 (T + Off + 12), R32 (T + Off + 4), (Flags & 1) ? "enabled" : ((Flags & 2) ? "online capable" : "disabled"));
        break;
      case 0x0A:
        Row = RwInfoField (Info, 1, RwEmNormal, "x2APIC NMI", "UID %d  LINT%d", R32 (T + Off + 4), T[Off + 8]);
        break;
      default:
        Row = RwInfoField (Info, 1, RwEmDim, "Structure", "type %02X, length %d", Type, L);
        break;
    }

    RwInfoSetReg (Row, Off, L);
  }

  RwInfoText (Info, 1, RwEmStrong, "%d processor entr%a, %d enabled", Cpus, (Cpus == 1) ? "y" : "ies", Enabled);
}

STATIC
VOID
DecodeMcfg (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Len
  )
{
  UINT32  Off;

  RwInfoSection (Info, 0, "PCI Express Memory-mapped Configuration (ECAM)");
  for (Off = 44; Off + 16 <= Len; Off += 16) {
    RwInfoSetReg (
      RwInfoField (Info, 1, RwEmNormal, "ECAM", "Base %lX  segment %d  bus %02X-%02X", R64 (T + Off), R16 (T + Off + 8), T[Off + 10], T[Off + 11]),
      Off,
      16
      );
  }
}

STATIC
VOID
DecodeHpet (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Len
  )
{
  UINT32  Id;

  if (Len < 56) {
    return;
  }

  Id = R32 (T + 36);
  RwInfoSection (Info, 0, "High Precision Event Timer");
  RwInfoSetReg (
    RwInfoField (
      Info,
      1,
      RwEmNormal,
      "Block ID",
      "%08X: vendor %04X, %d comparators, %a, rev %d%a",
      Id,
      Id >> 16,
      ((Id >> 8) & 0x1F) + 1,
      (Id & BIT13) ? "64-bit" : "32-bit",
      Id & 0xFF,
      (Id & BIT15) ? ", legacy replacement" : ""
      ),
    36,
    4
    );
  AddGas (Info, 1, "Base address", T, 40);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "HPET number", "%d", T[52]), 52, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Min tick", "%d", R16 (T + 53)), 53, 2);
}

STATIC
VOID
DecodeDmar (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Len
  )
{
  UINT32       Off;
  UINT16       Type;
  UINT16       L;
  RW_INFO_ROW  *Row;

  if (Len < 48) {
    return;
  }

  RwInfoSection (Info, 0, "DMA Remapping (VT-d)");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Host address width", "%d bits", T[36] + 1), 36, 1);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Flags", "%02X%a%a", T[37], (T[37] & 1) ? " INTR_REMAP" : "", (T[37] & 2) ? " X2APIC_OPT_OUT" : ""), 37, 1);
  for (Off = 48; Off + 4 <= Len; Off += L) {
    Type = R16 (T + Off);
    L    = R16 (T + Off + 2);
    if ((L < 4) || (Off + L > Len)) {
      break;
    }

    switch (Type) {
      case 0:
        Row = RwInfoField (Info, 1, RwEmNormal, "DRHD", "segment %d  register base %lX%a", R16 (T + Off + 6), R64 (T + Off + 8), (T[Off + 4] & 1) ? "  INCLUDE_PCI_ALL" : "");
        break;
      case 1:
        Row = RwInfoField (Info, 1, RwEmNormal, "RMRR", "segment %d  %lX - %lX", R16 (T + Off + 6), R64 (T + Off + 8), R64 (T + Off + 16));
        break;
      case 2:
        Row = RwInfoField (Info, 1, RwEmNormal, "ATSR", "segment %d", R16 (T + Off + 6));
        break;
      case 3:
        Row = RwInfoField (Info, 1, RwEmNormal, "RHSA", "base %lX  proximity domain %d", R64 (T + Off + 8), R32 (T + Off + 16));
        break;
      case 4:
        Row = RwInfoField (Info, 1, RwEmNormal, "ANDD", "device %d", T[Off + 7]);
        break;
      default:
        Row = RwInfoField (Info, 1, RwEmDim, "Structure", "type %d, length %d", Type, L);
        break;
    }

    RwInfoSetReg (Row, Off, L);
  }
}

VOID
RwAcpiDecode (
  IN     CONST RW_ACPI_TABLE  *Table,
  IN OUT RW_INFO              *Info
  )
{
  CHAR8        Sig[5];
  CHAR8        Oem[7];
  CHAR8        OemTab[9];
  CHAR8        Creator[5];
  CONST UINT8  *T;
  UINT32       Len;
  UINT32       Raw;
  BOOLEAN      Ok;

  T   = Table->Ptr;
  Len = Table->Length;
  if ((T == NULL) || (Len < 8)) {
    return;
  }

  AsciiField (Sig, T, 4);
  RwInfoSection (Info, 0, "Header");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Signature", "%a", Sig), 0, 4);
  RwInfoField (Info, 1, RwEmNormal, "Address", "%lX", (UINT64)(UINTN)T);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Length", "%d (%Xh)", Len, Len), 4, 4);
  if (Table->HasHeader && (Len >= 36)) {
    AsciiField (Oem, T + 10, 6);
    AsciiField (OemTab, T + 16, 8);
    AsciiField (Creator, T + 28, 4);
    Ok = (BOOLEAN)(RwAcpiChecksum (T, Len) == 0);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Revision", "%d", T[8]), 8, 1);
    RwInfoSetReg (RwInfoField (Info, 1, Ok ? RwEmNormal : RwEmAlert, "Checksum", "%02X (%a)", T[9], Ok ? "OK" : "BAD"), 9, 1);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "OEM ID", "%a", Oem), 10, 6);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "OEM Table ID", "%a", OemTab), 16, 8);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "OEM Revision", "%08X", R32 (T + 24)), 24, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Creator ID", "%a", Creator), 28, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Creator Revision", "%08X", R32 (T + 32)), 32, 4);
  } else if ((CompareMem (T, "FACS", 4) == 0) && (Len >= 33)) {
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "HW signature", "%08X", R32 (T + 8)), 8, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Waking vector", "%08X", R32 (T + 12)), 12, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "X Waking vector", "%lX", R64 (T + 24)), 24, 8);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Global lock", "%08X", R32 (T + 16)), 16, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Flags", "%08X", R32 (T + 20)), 20, 4);
    RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Version", "%d", T[32]), 32, 1);
  }

  if (CompareMem (T, "FACP", 4) == 0) {
    DecodeFadt (Info, T, Len);
  } else if (CompareMem (T, "APIC", 4) == 0) {
    DecodeMadt (Info, T, Len);
  } else if (CompareMem (T, "MCFG", 4) == 0) {
    DecodeMcfg (Info, T, Len);
  } else if (CompareMem (T, "HPET", 4) == 0) {
    DecodeHpet (Info, T, Len);
  } else if (CompareMem (T, "DMAR", 4) == 0) {
    DecodeDmar (Info, T, Len);
  } else if ((CompareMem (T, "DSDT", 4) == 0) || (CompareMem (T, "SSDT", 4) == 0)) {
    RwInfoText (Info, 0, RwEmDim, "AML definition block: raw bytes below; the ASL source comes from RwAmlDisassemble.");
  }

  Raw = (Len > RW_ACPI_RAW_MAX) ? RW_ACPI_RAW_MAX : Len;
  RwInfoSection (Info, 0, "Raw data%a", (Raw < Len) ? " (first 512 KB)" : "");
  RwInfoAddHex (Info, 1, T, Raw, 0);
}
