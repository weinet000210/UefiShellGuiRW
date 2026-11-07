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

#endif
