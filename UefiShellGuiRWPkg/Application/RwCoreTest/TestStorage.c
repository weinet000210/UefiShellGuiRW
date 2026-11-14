/** @file
  RwCoreTest: ATA IDENTIFY of the q35 AHCI disk (the ESP) and NVMe Identify
  of the test NVMe drive (serial RWNVME01, 64 MiB).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

VOID
TestStorage (
  VOID
  )
{
  RW_STORAGE_LIST  *List;
  EFI_STATUS       Status;
  UINTN            Index;
  INTN             Ata;
  INTN             Ctrl;
  INTN             Ns;
  CHAR8            Serial[24];
  RW_INFO          Info;
  RW_STORAGE_DEV   *D;

  List = AllocateZeroPool (sizeof (*List));
  if (List == NULL) {
    CHECK (FALSE, "memory for the storage list");
    return;
  }

  Status = RwStorageScan (List);
  CHECK (!EFI_ERROR (Status) && (List->Count >= 1), "storage scan finds drives");
  Ata  = -1;
  Ctrl = -1;
  Ns   = -1;
  for (Index = 0; Index < List->Count; Index++) {
    D = &List->Devs[Index];
    TestLog ("    %-16a %-40a %Ld bytes  %a", RwStorageKindName (D->Kind), D->Model, D->Capacity, D->Location);
    if ((D->Kind == RwStorAta) && (AsciiStrStr (D->Model, "QEMU HARDDISK") != NULL) && (Ata < 0)) {
      Ata = (INTN)Index;
    } else if ((D->Kind == RwStorNvmeCtrl) && (Ctrl < 0)) {
      Ctrl = (INTN)Index;
    } else if ((D->Kind == RwStorNvmeNs) && (Ns < 0)) {
      Ns = (INTN)Index;
    }
  }

  CHECK ((Ata >= 0) && (List->Devs[Ata].Capacity > 0), "ATA IDENTIFY of the AHCI disk");
  if (Ata >= 0) {
    RwInfoInit (&Info);
    RwStorageDecode (&List->Devs[Ata], &Info);
    CHECK (Info.Count > 15, "ATA identify decodes");
    RwInfoFree (&Info);
    D = &List->Devs[Ata];
    TestLog ("    ATA SMART: %r, %d bytes", D->HealthStatus, D->HealthSize);
    CHECK (!EFI_ERROR (D->HealthStatus) && (D->HealthSize == 1024), "ATA SMART READ DATA and THRESHOLDS");
    RwInfoInit (&Info);
    RwStorageHealthDecode (D, &Info);
    CHECK ((Info.Count > 10) && (Info.Rows[0].Kind == RwKindSection) && (AsciiStrStr (Info.Rows[0].Name, "SMART attributes") != NULL), "ATA SMART attributes decode");
    CHECK (AsciiStrCmp (RwAtaSmartAttrName (194), "Temperature") == 0, "SMART attribute names");
    RwInfoFree (&Info);
  }

  CHECK (Ctrl >= 0, "NVMe Identify Controller");
  if (Ctrl >= 0) {
    CopyMem (Serial, &List->Devs[Ctrl].Data[4], 8);
    Serial[8] = '\0';
    CHECK (AsciiStrCmp (Serial, "RWNVME01") == 0, "NVMe serial number");
    RwInfoInit (&Info);
    RwStorageDecode (&List->Devs[Ctrl], &Info);
    CHECK (Info.Count > 15, "NVMe controller decodes");
    RwInfoFree (&Info);
    D = &List->Devs[Ctrl];
    TestLog ("    NVMe logs: %r, %d bytes, %d error entries", D->HealthStatus, D->HealthSize, D->HealthErrors);
    CHECK (!EFI_ERROR (D->HealthStatus) && (D->HealthSize >= 1024), "NVMe Health, Firmware Slot and Error logs");
    CHECK ((D->Health != NULL) && ((D->Health[1] | (D->Health[2] << 8)) > 273), "NVMe composite temperature");
    RwInfoInit (&Info);
    RwStorageHealthDecode (D, &Info);
    CHECK ((Info.Count > 15) && (AsciiStrStr (Info.Rows[0].Name, "Health") != NULL), "NVMe health decodes");
    RwInfoFree (&Info);
  }

  CHECK ((Ns >= 0) && (List->Devs[Ns].Parent == Ctrl) && (List->Devs[Ns].Capacity == SIZE_64MB), "NVMe namespace 1 is 64 MiB");
  CHECK ((Ns >= 0) && (List->Devs[Ns].HealthStatus == EFI_NOT_FOUND), "NVMe namespace points to the controller for health");
  RwStorageFree (List);

  //
  // A second scan right after the SMART reads still finds the ATA drive.
  //
  Status = RwStorageScan (List);
  Ata    = -1;
  for (Index = 0; Index < List->Count; Index++) {
    if (List->Devs[Index].Kind == RwStorAta) {
      Ata = (INTN)Index;
    }
  }

  CHECK (!EFI_ERROR (Status) && (Ata >= 0) && (List->Devs[Ata].Health != NULL), "second scan finds the ATA drive and its SMART data");
  RwStorageFree (List);
  FreePool (List);
}
