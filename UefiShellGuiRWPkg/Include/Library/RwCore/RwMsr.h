/** @file
  RwCoreLib MSR: model specific register access on the boot processor.

  Unsupported MSRs raise #GP. RwMsrGuardInstall registers a #GP handler
  through EFI_CPU_ARCH_PROTOCOL that skips the faulting RDMSR / WRMSR, so
  probing an unknown MSR reports an error instead of hanging the machine.
  RwMsrRead and RwMsrWrite refuse to run without the guard.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWMSR_H_
#define RW_RWMSR_H_

#include <Library/RwCore/RwBase.h>

typedef struct {
  UINT32         Index;
  CONST CHAR8    *Name;
} RW_MSR_KNOWN;

/**
  Install the #GP guard. Nested calls are counted.
  @retval EFI_NOT_FOUND  no CPU architecture protocol.
**/
EFI_STATUS
RwMsrGuardInstall (
  VOID
  );

/**
  Undo one RwMsrGuardInstall; the handler is removed with the last one.
**/
VOID
RwMsrGuardRemove (
  VOID
  );

BOOLEAN
RwMsrGuardActive (
  VOID
  );

/**
  @retval EFI_UNSUPPORTED   the guard is not installed.
  @retval EFI_DEVICE_ERROR  the MSR raised #GP.
**/
EFI_STATUS
RwMsrRead (
  IN  UINT32  Index,
  OUT UINT64  *Value
  );

EFI_STATUS
RwMsrWrite (
  IN UINT32  Index,
  IN UINT64  Value
  );

/**
  The MSRs listed when the MSR window opens.
**/
CONST RW_MSR_KNOWN *
RwMsrKnownList (
  OUT UINTN  *Count
  );

/**
  Name of a known MSR, NULL for others.
**/
CONST CHAR8 *
RwMsrName (
  IN UINT32  Index
  );

/**
  CPUID brand string, leading spaces removed.
**/
VOID
RwCpuBrand (
  OUT CHAR8  *Buffer,
  IN  UINTN  BufferSize
  );

//
// ---- Processors (EFI_MP_SERVICES_PROTOCOL) --------------------------------
//
typedef struct {
  UINT32     ApicId;
  BOOLEAN    IsBsp;
  BOOLEAN    Enabled;
  BOOLEAN    Healthy;
  UINT32     Package;
  UINT32     Core;
  UINT32     Thread;
} RW_CPU_INFO;

/** Logical processors (1 without MP services). Numbers are 0..Count-1. **/
UINTN
RwCpuCount (
  VOID
  );

/** Number of the boot processor. **/
UINTN
RwCpuBsp (
  VOID
  );

EFI_STATUS
RwCpuGetInfo (
  IN  UINTN        Cpu,
  OUT RW_CPU_INFO  *Info
  );

/**
  RwMsrRead / RwMsrWrite on processor Cpu. Needs the guard. On an AP the
  MSR is read on the boot processor first and the AP is not used when that
  faults; the AP call times out after 200 ms.

  @retval EFI_DEVICE_ERROR  #GP (no such MSR or a refused value).
  @retval EFI_NOT_READY     the processor is disabled.
  @retval EFI_TIMEOUT       the AP did not finish.
**/
EFI_STATUS
RwMsrReadOn (
  IN  UINTN   Cpu,
  IN  UINT32  Index,
  OUT UINT64  *Value
  );

EFI_STATUS
RwMsrWriteOn (
  IN UINTN   Cpu,
  IN UINT32  Index,
  IN UINT64  Value
  );

#endif
