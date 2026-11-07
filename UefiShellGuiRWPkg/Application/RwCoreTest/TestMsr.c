/** @file
  RwCoreTest: #GP-guarded MSR access.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

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
