/** @file
  RwCoreTest: #GP-guarded MSR access.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

//
// Processors: the test machine runs with -smp 2.
//
STATIC
VOID
TestMp (
  VOID
  )
{
  UINTN        Count;
  UINTN        Bsp;
  UINTN        Ap;
  RW_CPU_INFO  BspInfo;
  RW_CPU_INFO  ApInfo;
  UINT64       Value;
  UINT64       Saved;
  UINT64       BspCs;
  EFI_STATUS   Status;

  Count = RwCpuCount ();
  Bsp   = RwCpuBsp ();
  TestLog ("    %d processors, boot processor %d", Count, Bsp);
  CHECK ((Count >= 2) && (Bsp < Count), "MP services report two processors");
  if (Count < 2) {
    return;
  }

  Ap = (Bsp == 0) ? 1 : 0;
  CHECK (!EFI_ERROR (RwCpuGetInfo (Bsp, &BspInfo)) && BspInfo.IsBsp && BspInfo.Enabled, "boot processor info");
  CHECK (!EFI_ERROR (RwCpuGetInfo (Ap, &ApInfo)) && !ApInfo.IsBsp && (ApInfo.ApicId != BspInfo.ApicId), "AP info, own APIC ID");

  CHECK (!EFI_ERROR (RwMsrReadOn (Bsp, 0x1B, &Value)) && ((Value & BIT8) != 0), "IA32_APIC_BASE on the boot processor");
  Status = RwMsrReadOn (Ap, 0x1B, &Value);
  TestLog ("    AP %d IA32_APIC_BASE: %r %016lX", Ap, Status, Value);
  CHECK (!EFI_ERROR (Status) && ((Value & BIT8) == 0) && ((Value & 0xFFFFF000ULL) == 0xFEE00000ULL), "IA32_APIC_BASE on the AP (not BSP)");

  //
  // IA32_SYSENTER_CS is unused by UEFI: write it on the AP only, read back,
  // and check the boot processor's copy did not change.
  //
  CHECK (!EFI_ERROR (RwMsrReadOn (Ap, 0x174, &Saved)) && !EFI_ERROR (RwMsrRead (0x174, &BspCs)), "IA32_SYSENTER_CS reads on both");
  CHECK (!EFI_ERROR (RwMsrWriteOn (Ap, 0x174, 0x5A)), "write on the AP");
  CHECK (!EFI_ERROR (RwMsrReadOn (Ap, 0x174, &Value)) && (Value == 0x5A), "AP reads back its own value");
  CHECK (!EFI_ERROR (RwMsrRead (0x174, &Value)) && (Value == BspCs), "boot processor copy unchanged");
  RwMsrWriteOn (Ap, 0x174, Saved);
}

VOID
TestMsr (
  VOID
  )
{
  UINT64              Value;
  UINT64              Again;
  EFI_STATUS          Status;
  CHAR8               Brand[64];
  CONST RW_MSR_KNOWN  *Known;
  UINTN               Count;
  UINTN               Index;
  UINTN               Valid;

  CHECK (RwMsrRead (0x1B, &Value) == EFI_UNSUPPORTED, "no MSR access without the guard");
  Status = RwMsrGuardInstall ();
  CHECK (!EFI_ERROR (Status) && RwMsrGuardActive (), "guard installs");
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = RwMsrRead (0x1B, &Value);
  TestLog ("    IA32_APIC_BASE %016lX", Value);
  CHECK (!EFI_ERROR (Status), "IA32_APIC_BASE reads");
  CHECK ((Value & BIT8) != 0, "IA32_APIC_BASE marks the boot processor");
  CHECK ((Value & 0xFFFFF000ULL) == 0xFEE00000ULL, "local APIC at FEE00000h");

  Status = RwMsrRead (0x4A5A3C00, &Value);
  //
  // KVM raises #GP for an unknown MSR; QEMU's TCG returns 0 instead. Either
  // way the read has to come back.
  //
  TestLog ("    unknown MSR 4A5A3C00h: %r", Status);
  CHECK ((Status == EFI_DEVICE_ERROR) || (Status == EFI_SUCCESS), "an unknown MSR read comes back");
  CHECK (!EFI_ERROR (RwMsrRead (0x1B, &Again)) && ((Again & 0xFFFFF000ULL) == 0xFEE00000ULL), "reads still work after a #GP");

  Known = RwMsrKnownList (&Count);
  Valid = 0;
  for (Index = 0; Index < Count; Index++) {
    Status = RwMsrRead (Known[Index].Index, &Value);
    if (!EFI_ERROR (Status)) {
      Valid++;
    }

    TestLog ("    %08X %-24a %a %016lX", Known[Index].Index, Known[Index].Name, EFI_ERROR (Status) ? "#GP" : "   ", Value);
  }

  CHECK ((Count == 20) && (Valid >= 3), "known MSR list reads");
  CHECK (AsciiStrCmp (RwMsrName (0xC0000080), "IA32_EFER") == 0, "RwMsrName");

  TestMp ();

  RwMsrGuardInstall ();
  RwMsrGuardRemove ();
  CHECK (RwMsrGuardActive (), "nested guard stays until the last remove");
  RwMsrGuardRemove ();
  CHECK (!RwMsrGuardActive (), "guard removed");
  CHECK (RwMsrRead (0x1B, &Value) == EFI_UNSUPPORTED, "no MSR access after the guard is removed");

  RwCpuBrand (Brand, sizeof (Brand));
  TestLog ("    CPU: %a", Brand);
  CHECK (Brand[0] != '\0', "CPU brand string");
}
