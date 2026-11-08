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
  }

  CHECK ((Ns >= 0) && (List->Devs[Ns].Parent == Ctrl) && (List->Devs[Ns].Capacity == SIZE_64MB), "NVMe namespace 1 is 64 MiB");
  RwStorageFree (List);
  FreePool (List);
}
