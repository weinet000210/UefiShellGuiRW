/** @file
  RwCoreLib CPUID: every standard, hypervisor and extended leaf (with their
  subleaves) of one logical processor, and decoders for them. CPUID never
  faults and changes nothing, so it runs on any processor without a guard.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWCPUID_H_
#define RW_RWCPUID_H_

#include <Library/RwCore/RwBase.h>

#define RW_CPUID_MAX  320

typedef struct {
  UINT32    Leaf;
  UINT32    Sub;
  UINT32    Eax;
  UINT32    Ebx;
  UINT32    Ecx;
  UINT32    Edx;
} RW_CPUID_ENTRY;

typedef struct {
  RW_CPUID_ENTRY    Entries[RW_CPUID_MAX];
  UINTN             Count;
  UINTN             Cpu;
} RW_CPUID_LIST;

/**
  Run CPUID on processor Cpu (RwCpuCount numbering) for every leaf it
  reports. Leaves with subleaves (4, 7, 0Bh, 0Dh, 0Fh, 10h, 12h, 14h, 17h,
  18h, 1Fh, 8000001Dh, 80000020h, 80000026h) list each valid subleaf.
  @retval EFI_NOT_READY  the processor is disabled.
  @retval EFI_TIMEOUT    the AP did not finish.
**/
EFI_STATUS
RwCpuidCollect (
  IN  UINTN          Cpu,
  OUT RW_CPUID_LIST  *List
  );

/** The entry for Leaf.Sub, NULL when the processor does not report it. **/
CONST RW_CPUID_ENTRY *
RwCpuidFind (
  IN CONST RW_CPUID_LIST  *List,
  IN UINT32               Leaf,
  IN UINT32               Sub
  );

/** Short name of a leaf ("Feature information"), "" when unknown. **/
CONST CHAR8 *
RwCpuidLeafName (
  IN UINT32  Leaf
  );

/**
  Processor summary: vendor, brand, signature, features by group, caches,
  topology and address sizes.
**/
VOID
RwCpuidDecode (
  IN     CONST RW_CPUID_LIST  *List,
  IN OUT RW_INFO              *Info
  );

/** One leaf: its four registers with their fields and named bits. **/
VOID
RwCpuidDecodeLeaf (
  IN     CONST RW_CPUID_LIST   *List,
  IN     CONST RW_CPUID_ENTRY  *Entry,
  IN OUT RW_INFO               *Info
  );

#endif
