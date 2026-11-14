/** @file
  Dump all: every decoder's output as one text document.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

STATIC
VOID
Heading (
  IN OUT RW_STR       *Out,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Eol
  )
{
  RwStrAppendF (Out, "%a==== %a ====%a%a", Eol, Name, Eol, Eol);
}

STATIC
VOID
AppendInfo (
  IN OUT RW_STR   *Out,
  IN OUT RW_INFO  *Info,
  IN     BOOLEAN  Crlf
  )
{
  RwInfoToText (Info, Out, Crlf);
  RwInfoFree (Info);
  RwInfoInit (Info);
}

STATIC
VOID
DumpPci (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_PCI_LIST  List;
  RW_INFO      Info;

  RwInfoInit (&Info);
  if (EFI_ERROR (RwPciScan (&List))) {
    RwInfoText (&Info, 0, RwEmDim, "No PCI root bridge.");
  } else {
    RwPciDump (&List, &Info);
    RwPciFree (&List);
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpAcpi (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_ACPI_TABLES  Tables;
  RW_AML_CTX      Aml;
  RW_INFO         Info;
  UINTN           Index;
  RW_ACPI_TABLE   *Table;
  CONST CHAR8     *Eol;

  Eol = Crlf ? "\r\n" : "\n";
  RwInfoInit (&Info);
  if (EFI_ERROR (RwAcpiScan (&Tables)) || (Tables.Rsdp == NULL)) {
    RwInfoText (&Info, 0, RwEmDim, "The firmware publishes no ACPI tables.");
    AppendInfo (Out, &Info, Crlf);
    RwInfoFree (&Info);
    return;
  }

  RwAcpiListInfo (&Tables, &Info);
  AppendInfo (Out, &Info, Crlf);
  RwAmlInit (&Aml);
  RwAmlPrepareTables (&Aml, &Tables);
  for (Index = 0; Index < Tables.Count; Index++) {
    Table = &Tables.Tables[Index];
    RwStrAppendF (Out, "%a---- %a at %016lX, %d bytes ----%a", Eol, Table->Signature, Table->Address, (UINTN)Table->Length, Eol);
    if (RwAcpiIsAml (Table)) {
      RwAmlDisassemble (&Aml, Table->Ptr, Table->Length, Crlf, Out);
    } else {
      RwAcpiDecode (Table, &Info);
      AppendInfo (Out, &Info, Crlf);
    }
  }

  RwAmlFree (&Aml);
  RwAcpiFree (&Tables);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpSmbios (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_INFO  Info;

  RwInfoInit (&Info);
  if (EFI_ERROR (RwSmbiosDump (&Info))) {
    RwInfoText (&Info, 0, RwEmDim, "The firmware publishes no SMBIOS table.");
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpUsb (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_USB_SCAN  Scan;
  RW_USB_TREE  Tree;
  RW_INFO      Info;
  UINTN        Index;

  RwInfoInit (&Info);
  ZeroMem (&Scan, sizeof (Scan));
  if (EFI_ERROR (RwUsbScan (&Scan)) || (Scan.HcCount == 0)) {
    RwInfoText (&Info, 0, RwEmDim, "No USB host controller.");
    AppendInfo (Out, &Info, Crlf);
    RwUsbScanFree (&Scan);
    RwInfoFree (&Info);
    return;
  }

  if (!EFI_ERROR (RwUsbBuildTree (&Scan, NULL, &Tree))) {
    RwUsbTreeToInfo (&Scan, &Tree, &Info);
    RwUsbTreeFree (&Tree);
  }

  for (Index = 0; Index < Scan.HcCount; Index++) {
    RwUsbDecodeController (&Scan, Index, &Info);
  }

  for (Index = 0; Index < Scan.DevCount; Index++) {
    RwUsbDecodeDevice (&Scan, Index, &Info);
  }

  AppendInfo (Out, &Info, Crlf);
  RwUsbScanFree (&Scan);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpSio (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_SIO_LIST  *List;
  RW_INFO      Info;

  RwInfoInit (&Info);
  List = AllocateZeroPool (sizeof (*List));
  if ((List == NULL) || EFI_ERROR (RwSioScan (List)) || (List->ChipCount == 0)) {
    RwInfoText (&Info, 0, RwEmDim, "No Super I/O chip answers at 2Eh / 4Eh.");
  } else {
    RwSioDecode (List, &Info);
  }

  if (List != NULL) {
    FreePool (List);
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpSpd (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_SMBUS  Bus;
  RW_SPD    *Spd;
  RW_INFO   Info;
  UINT8     Addr;
  UINTN     Found;

  RwInfoInit (&Info);
  if (EFI_ERROR (RwSmbusFind (&Bus))) {
    RwInfoText (&Info, 0, RwEmDim, "No supported SMBus host controller.");
  } else if (!Bus.HostEnabled || !Bus.IoDecode) {
    RwInfoText (&Info, 0, RwEmDim, "%a: host controller disabled (the dump does not enable it).", Bus.Name);
  } else {
    RwInfoSection (&Info, 0, "%a", Bus.Name);
    Spd   = AllocateZeroPool (sizeof (*Spd));
    Found = 0;
    for (Addr = 0x50; (Spd != NULL) && (Addr <= 0x57); Addr++) {
      if (!EFI_ERROR (RwSpdRead (&Bus, Addr, Spd)) && (Spd->Type != 0)) {
        RwSpdDecode (Spd, &Info);
        Found++;
      }
    }

    if (Found == 0) {
      RwInfoText (&Info, 1, RwEmDim, "No SPD EEPROM at 50h-57h.");
    }

    if (Spd != NULL) {
      FreePool (Spd);
    }
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpStorage (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_STORAGE_LIST  *List;
  RW_INFO          Info;
  UINTN            Index;

  RwInfoInit (&Info);
  List = AllocateZeroPool (sizeof (*List));
  if ((List == NULL) || EFI_ERROR (RwStorageScan (List)) || (List->Count == 0)) {
    RwInfoText (&Info, 0, RwEmDim, "No ATA or NVMe device answers identify.");
  } else {
    for (Index = 0; Index < List->Count; Index++) {
      RwStorageDecode (&List->Devs[Index], &Info);
    }

    RwStorageFree (List);
  }

  if (List != NULL) {
    FreePool (List);
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpEdid (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_EDID_LIST  *List;
  RW_INFO       Info;
  UINTN         Index;

  RwInfoInit (&Info);
  List = AllocateZeroPool (sizeof (*List));
  if ((List == NULL) || EFI_ERROR (RwEdidScan (List)) || (List->Count == 0)) {
    RwInfoText (&Info, 0, RwEmDim, "No EDID published.");
  } else {
    for (Index = 0; Index < List->Count; Index++) {
      RwInfoSection (&Info, 0, "%a (%a)", List->Displays[Index].Name, List->Displays[Index].Source);
      RwEdidDecode (List->Displays[Index].Data, List->Displays[Index].Size, &Info);
    }
  }

  if (List != NULL) {
    FreePool (List);
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

STATIC
VOID
DumpMsr (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  RW_INFO             Info;
  CONST RW_MSR_KNOWN  *Known;
  UINTN               KnownCount;
  UINTN               Index;
  UINTN               Cpu;
  UINTN               Bsp;
  BOOLEAN             Installed;
  BOOLEAN             *BspOk;
  UINT64              Value;
  CHAR8               Brand[64];

  RwInfoInit (&Info);
  Installed = FALSE;
  if (!RwMsrGuardActive ()) {
    Installed = (BOOLEAN)!EFI_ERROR (RwMsrGuardInstall ());
  }

  if (!RwMsrGuardActive ()) {
    RwInfoText (&Info, 0, RwEmDim, "MSR reads need the CPU exception guard, which could not be installed.");
    AppendInfo (Out, &Info, Crlf);
    RwInfoFree (&Info);
    return;
  }

  RwCpuBrand (Brand, sizeof (Brand));
  RwInfoField (&Info, 0, RwEmNormal, "Processor", "%a", Brand);
  RwInfoField (&Info, 0, RwEmNormal, "Logical CPUs", "%d", RwCpuCount ());
  Known = RwMsrKnownList (&KnownCount);
  Bsp   = RwCpuBsp ();
  BspOk = AllocateZeroPool (KnownCount * sizeof (BOOLEAN));
  //
  // An AP only reads an MSR the boot processor read without #GP.
  //
  for (Index = 0; (BspOk != NULL) && (Index < KnownCount); Index++) {
    BspOk[Index] = (BOOLEAN)!EFI_ERROR (RwMsrReadOn (Bsp, Known[Index].Index, &Value));
  }

  for (Cpu = 0; (BspOk != NULL) && (Cpu < RwCpuCount ()); Cpu++) {
    RwInfoSection (&Info, 0, "CPU %d%a", Cpu, (Cpu == Bsp) ? " (BSP)" : "");
    for (Index = 0; Index < KnownCount; Index++) {
      if (BspOk[Index] && !EFI_ERROR (RwMsrReadOn (Cpu, Known[Index].Index, &Value))) {
        RwInfoField (&Info, 1, RwEmNormal, Known[Index].Name, "%08Xh = %016lX", Known[Index].Index, Value);
      }
    }
  }

  if (BspOk != NULL) {
    FreePool (BspOk);
  }

  if (Installed) {
    RwMsrGuardRemove ();
  }

  AppendInfo (Out, &Info, Crlf);
  RwInfoFree (&Info);
}

typedef struct {
  UINT32         Part;
  CONST CHAR8    *Name;
  VOID           (*Dump)(RW_STR *Out, BOOLEAN Crlf);
} DUMP_PART;

STATIC CONST DUMP_PART  mParts[] = {
  { RW_DUMP_PCI,     "PCI",             DumpPci     },
  { RW_DUMP_ACPI,    "ACPI",            DumpAcpi    },
  { RW_DUMP_SMBIOS,  "SMBIOS",          DumpSmbios  },
  { RW_DUMP_USB,     "USB",             DumpUsb     },
  { RW_DUMP_SIO,     "Super I/O",       DumpSio     },
  { RW_DUMP_SPD,     "SMBus / SPD",     DumpSpd     },
  { RW_DUMP_STORAGE, "ATA / NVMe",      DumpStorage },
  { RW_DUMP_EDID,    "Monitor EDID",    DumpEdid    },
  { RW_DUMP_MSR,     "CPU MSR",         DumpMsr     },
};

VOID
RwDumpAll (
  IN     UINT32   Parts,
  IN     BOOLEAN  Crlf,
  IN OUT RW_STR   *Out
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mParts); Index++) {
    if ((Parts & mParts[Index].Part) != 0) {
      Heading (Out, mParts[Index].Name, Crlf ? "\r\n" : "\n");
      mParts[Index].Dump (Out, Crlf);
    }
  }
}
