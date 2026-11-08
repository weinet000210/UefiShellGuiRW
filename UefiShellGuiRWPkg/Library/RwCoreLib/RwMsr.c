/** @file
  RwCoreLib MSR: #GP-guarded MSR access on the boot processor and, through
  EFI_MP_SERVICES_PROTOCOL, on the application processors.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Protocol/Cpu.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>

STATIC CONST RW_MSR_KNOWN  mKnown[] = {
  { 0x00000010, "IA32_TIME_STAMP_COUNTER" },
  { 0x00000017, "IA32_PLATFORM_ID"        },
  { 0x0000001B, "IA32_APIC_BASE"          },
  { 0x0000003A, "IA32_FEATURE_CONTROL"    },
  { 0x0000008B, "IA32_BIOS_SIGN_ID"       },
  { 0x000000CE, "MSR_PLATFORM_INFO"       },
  { 0x000000FE, "IA32_MTRRCAP"            },
  { 0x00000198, "IA32_PERF_STATUS"        },
  { 0x00000199, "IA32_PERF_CTL"           },
  { 0x0000019C, "IA32_THERM_STATUS"       },
  { 0x000001A0, "IA32_MISC_ENABLE"        },
  { 0x000001A2, "MSR_TEMPERATURE_TARGET"  },
  { 0x00000200, "IA32_MTRR_PHYSBASE0"     },
  { 0x00000201, "IA32_MTRR_PHYSMASK0"     },
  { 0x00000277, "IA32_PAT"                },
  { 0x000002FF, "IA32_MTRR_DEF_TYPE"      },
  { 0xC0000080, "IA32_EFER"               },
  { 0xC0000081, "IA32_STAR"               },
  { 0xC0000100, "IA32_FS_BASE"            },
  { 0xC0000101, "IA32_GS_BASE"            },
};

STATIC EFI_CPU_ARCH_PROTOCOL  *mCpu;
STATIC UINTN                  mGuardDepth;
STATIC BOOLEAN                mGuardInstalled;
STATIC volatile BOOLEAN       mFaulted;

STATIC
VOID
EFIAPI
GpFaultHandler (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  UINT8  *Ip;

  Ip = (UINT8 *)(UINTN)SystemContext.SystemContextX64->Rip;
  if ((Ip[0] == 0x0F) && ((Ip[1] == 0x32) || (Ip[1] == 0x30))) {
    SystemContext.SystemContextX64->Rip += 2;   // skip RDMSR / WRMSR
    mFaulted                             = TRUE;
  }
}

EFI_STATUS
RwMsrGuardInstall (
  VOID
  )
{
  if (mCpu == NULL) {
    gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&mCpu);
  }

  if (mCpu == NULL) {
    return EFI_NOT_FOUND;
  }

  if (!mGuardInstalled) {
    if (EFI_ERROR (mCpu->RegisterInterruptHandler (mCpu, EXCEPT_X64_GP_FAULT, GpFaultHandler))) {
      return EFI_DEVICE_ERROR;
    }

    mGuardInstalled = TRUE;
  }

  mGuardDepth++;
  return EFI_SUCCESS;
}

VOID
RwMsrGuardRemove (
  VOID
  )
{
  if (mGuardDepth == 0) {
    return;
  }

  mGuardDepth--;
  if ((mGuardDepth == 0) && (mCpu != NULL) && mGuardInstalled) {
    mCpu->RegisterInterruptHandler (mCpu, EXCEPT_X64_GP_FAULT, NULL);
    mGuardInstalled = FALSE;
  }
}

BOOLEAN
RwMsrGuardActive (
  VOID
  )
{
  return mGuardInstalled;
}

EFI_STATUS
RwMsrRead (
  IN  UINT32  Index,
  OUT UINT64  *Value
  )
{
  *Value = 0;
  if (!mGuardInstalled) {
    return EFI_UNSUPPORTED;   // never probe without the guard
  }

  mFaulted = FALSE;
  *Value   = AsmReadMsr64 (Index);
  if (mFaulted) {
    *Value = 0;
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RwMsrWrite (
  IN UINT32  Index,
  IN UINT64  Value
  )
{
  if (!mGuardInstalled) {
    return EFI_UNSUPPORTED;
  }

  mFaulted = FALSE;
  AsmWriteMsr64 (Index, Value);
  return mFaulted ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

CONST RW_MSR_KNOWN *
RwMsrKnownList (
  OUT UINTN  *Count
  )
{
  *Count = ARRAY_SIZE (mKnown);
  return mKnown;
}

CONST CHAR8 *
RwMsrName (
  IN UINT32  Index
  )
{
  UINTN  I;

  for (I = 0; I < ARRAY_SIZE (mKnown); I++) {
    if (mKnown[I].Index == Index) {
      return mKnown[I].Name;
    }
  }

  return NULL;
}

VOID
RwCpuBrand (
  OUT CHAR8  *Buffer,
  IN  UINTN  BufferSize
  )
{
  UINT32  Regs[12];
  UINT32  MaxExt;
  CHAR8   Brand[49];
  UINTN   Index;
  CHAR8   *Start;

  AsmCpuid (0x80000000, &MaxExt, NULL, NULL, NULL);
  if (MaxExt < 0x80000004) {
    AsciiStrCpyS (Buffer, BufferSize, "(brand string not available)");
    return;
  }

  for (Index = 0; Index < 3; Index++) {
    AsmCpuid (
      0x80000002 + (UINT32)Index,
      &Regs[Index * 4],
      &Regs[Index * 4 + 1],
      &Regs[Index * 4 + 2],
      &Regs[Index * 4 + 3]
      );
  }

  CopyMem (Brand, Regs, 48);
  Brand[48] = '\0';
  Start     = Brand;
  while (*Start == ' ') {
    Start++;
  }

  AsciiStrnCpyS (Buffer, BufferSize, Start, BufferSize - 1);
}

//
// ---- Processors ------------------------------------------------------------
//
// AP accesses run one at a time (StartupThisAP, blocking) under the same
// #GP guard: the AP uses the boot processor's IDT, so the handler applies.
// An MSR is only touched on an AP after the same MSR was read on the boot
// processor without a fault, and every AP call has a timeout.
//
#define RW_AP_TIMEOUT_US  200000

typedef struct {
  UINT32       Index;
  BOOLEAN      Write;
  UINT64       Value;
  EFI_STATUS   Status;
} MSR_AP_ARG;

STATIC EFI_MP_SERVICES_PROTOCOL  *mMp;
STATIC BOOLEAN                   mMpLooked;

STATIC
EFI_MP_SERVICES_PROTOCOL *
Mp (
  VOID
  )
{
  if (!mMpLooked) {
    mMpLooked = TRUE;
    if (EFI_ERROR (gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&mMp))) {
      mMp = NULL;
    }
  }

  return mMp;
}

UINTN
RwCpuCount (
  VOID
  )
{
  UINTN  Total;
  UINTN  Enabled;

  if ((Mp () == NULL) || EFI_ERROR (mMp->GetNumberOfProcessors (mMp, &Total, &Enabled)) || (Total == 0)) {
    return 1;
  }

  return Total;
}

UINTN
RwCpuBsp (
  VOID
  )
{
  UINTN  Self;

  if ((Mp () == NULL) || EFI_ERROR (mMp->WhoAmI (mMp, &Self))) {
    return 0;
  }

  return Self;
}

EFI_STATUS
RwCpuGetInfo (
  IN  UINTN        Cpu,
  OUT RW_CPU_INFO  *Info
  )
{
  EFI_PROCESSOR_INFORMATION  Pi;

  ZeroMem (Info, sizeof (*Info));
  if (Mp () == NULL) {
    if (Cpu != 0) {
      return EFI_NOT_FOUND;
    }

    Info->IsBsp   = TRUE;
    Info->Enabled = TRUE;
    Info->Healthy = TRUE;
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (mMp->GetProcessorInfo (mMp, Cpu, &Pi))) {
    return EFI_NOT_FOUND;
  }

  Info->ApicId  = (UINT32)Pi.ProcessorId;
  Info->IsBsp   = (BOOLEAN)((Pi.StatusFlag & PROCESSOR_AS_BSP_BIT) != 0);
  Info->Enabled = (BOOLEAN)((Pi.StatusFlag & PROCESSOR_ENABLED_BIT) != 0);
  Info->Healthy = (BOOLEAN)((Pi.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT) != 0);
  Info->Package = Pi.Location.Package;
  Info->Core    = Pi.Location.Core;
  Info->Thread  = Pi.Location.Thread;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
MsrOnAp (
  IN OUT VOID  *Buffer
  )
{
  MSR_AP_ARG  *Arg;

  Arg      = (MSR_AP_ARG *)Buffer;
  mFaulted = FALSE;
  if (Arg->Write) {
    AsmWriteMsr64 (Arg->Index, Arg->Value);
  } else {
    Arg->Value = AsmReadMsr64 (Arg->Index);
  }

  Arg->Status = mFaulted ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
RunOn (
  IN     UINTN       Cpu,
  IN OUT MSR_AP_ARG  *Arg
  )
{
  UINT64       Probe;
  EFI_STATUS   Status;
  RW_CPU_INFO  Info;

  if (!mGuardInstalled) {
    return EFI_UNSUPPORTED;
  }

  if ((Mp () == NULL) || (Cpu == RwCpuBsp ())) {
    return Arg->Write ? RwMsrWrite (Arg->Index, Arg->Value) : RwMsrRead (Arg->Index, &Arg->Value);
  }

  if (EFI_ERROR (RwCpuGetInfo (Cpu, &Info)) || !Info.Enabled) {
    return EFI_NOT_READY;
  }

  //
  // Never send an AP to an MSR the boot processor faults on.
  //
  Status = RwMsrRead (Arg->Index, &Probe);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Arg->Status = EFI_DEVICE_ERROR;
  Status      = mMp->StartupThisAP (mMp, MsrOnAp, Cpu, NULL, RW_AP_TIMEOUT_US, Arg, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Arg->Status;
}

EFI_STATUS
RwMsrReadOn (
  IN  UINTN   Cpu,
  IN  UINT32  Index,
  OUT UINT64  *Value
  )
{
  MSR_AP_ARG  Arg;
  EFI_STATUS  Status;

  ZeroMem (&Arg, sizeof (Arg));
  Arg.Index = Index;
  Status    = RunOn (Cpu, &Arg);
  *Value    = EFI_ERROR (Status) ? 0 : Arg.Value;
  return Status;
}

EFI_STATUS
RwMsrWriteOn (
  IN UINTN   Cpu,
  IN UINT32  Index,
  IN UINT64  Value
  )
{
  MSR_AP_ARG  Arg;

  ZeroMem (&Arg, sizeof (Arg));
  Arg.Index = Index;
  Arg.Write = TRUE;
  Arg.Value = Value;
  return RunOn (Cpu, &Arg);
}
