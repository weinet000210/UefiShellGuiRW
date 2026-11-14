/** @file
  RwCoreTest: CPUID on both processors of the test machine.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

VOID
TestCpuid (
  VOID
  )
{
  RW_CPUID_LIST         *Bsp;
  RW_CPUID_LIST         *Ap;
  CONST RW_CPUID_ENTRY  *E;
  CONST RW_CPUID_ENTRY  *F;
  RW_INFO               Info;
  UINT32                Eax;
  UINT32                Ebx;
  UINT32                Ecx;
  UINT32                Edx;
  UINTN                 Cpu;
  EFI_STATUS            Status;

  Bsp = AllocateZeroPool (sizeof (*Bsp));
  Ap  = AllocateZeroPool (sizeof (*Ap));
  if ((Bsp == NULL) || (Ap == NULL)) {
    CHECK (FALSE, "memory for CPUID lists");
    return;
  }

  Status = RwCpuidCollect (RwCpuBsp (), Bsp);
  TestLog ("    BSP: %d leaves", Bsp->Count);
  CHECK (!EFI_ERROR (Status) && (Bsp->Count >= 8), "CPUID collects leaves on the BSP");
  E = RwCpuidFind (Bsp, 0, 0);
  AsmCpuid (0, &Eax, &Ebx, &Ecx, &Edx);
  CHECK ((E != NULL) && (E->Eax == Eax) && (E->Ebx == Ebx), "leaf 0 matches a direct CPUID");
  CHECK ((RwCpuidFind (Bsp, 0x80000000, 0) != NULL) && (RwCpuidFind (Bsp, 1, 0) != NULL), "standard and extended leaves");
  CHECK (AsciiStrCmp (RwCpuidLeafName (1), "Signature and features") == 0, "leaf names");

  RwInfoInit (&Info);
  RwCpuidDecode (Bsp, &Info);
  CHECK ((Info.Count > 10) && (AsciiStrCmp (Info.Rows[1].Name, "Vendor") == 0), "CPUID summary decodes");
  RwInfoFree (&Info);
  RwInfoInit (&Info);
  RwCpuidDecodeLeaf (Bsp, RwCpuidFind (Bsp, 1, 0), &Info);
  CHECK ((Info.Count >= 8) && (AsciiStrStr (Info.Rows[Info.Count - 2].Value, "FPU") != NULL), "leaf 1 names its feature bits");
  RwInfoFree (&Info);

  //
  // The AP's own leaf 1 differs from the BSP's in the initial APIC ID only.
  //
  Cpu = (RwCpuBsp () == 0) ? 1 : 0;
  if (RwCpuCount () > 1) {
    Status = RwCpuidCollect (Cpu, Ap);
    TestLog ("    AP %d: %r, %d leaves", Cpu, Status, Ap->Count);
    E = RwCpuidFind (Bsp, 1, 0);
    F = RwCpuidFind (Ap, 1, 0);
    CHECK (!EFI_ERROR (Status) && (Ap->Count == Bsp->Count), "CPUID on the AP");
    CHECK ((E != NULL) && (F != NULL) && (E->Eax == F->Eax) && ((E->Ebx >> 24) != (F->Ebx >> 24)), "AP has its own APIC ID");
  } else {
    CHECK (FALSE, "test machine has two processors");
  }

  FreePool (Bsp);
  FreePool (Ap);
}
