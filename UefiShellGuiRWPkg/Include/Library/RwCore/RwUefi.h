/** @file
  RwCoreLib UEFI module: the firmware's own structures. GUID names, the
  handle database (handles, their protocols and who opened them), the
  system table with its configuration tables, graphics output modes and the
  TPM 2.0 event log with PCR values. Everything here only reads: no mode is
  set, no protocol is opened BY_DRIVER, and the only TPM command sent is
  TPM2_PCR_Read.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWUEFI_H_
#define RW_RWUEFI_H_

#include <Library/RwCore/RwBase.h>

typedef struct {
  EFI_GUID       Guid;
  CONST CHAR8    *Name;
} RW_GUID_NAME;

/** Name of a GUID from edk2's DEC files ("EfiPciIoProtocol"), NULL if unknown. **/
CONST CHAR8 *
RwGuidName (
  IN CONST EFI_GUID  *Guid
  );

/** The GUID's name, else its registry format text. **/
VOID
RwGuidText (
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR8           *Buffer,
  IN  UINTN           BufferSize
  );

/**
  Handle database as a tree. By handle: "Handle <n>" (n = position in
  LocateHandle(AllHandles), hex, from 1) with a short description; under it
  each protocol with its details (device path text, image file, driver
  name), and under each protocol who opened it. By protocol: each protocol
  installed anywhere with its handles. Rows of handles carry Tag = n.
  @param[out] Handles    number of handles (may be NULL).
  @param[out] Protocols  number of distinct protocols (may be NULL).
**/
EFI_STATUS
RwHandleDbDecode (
  IN     BOOLEAN  ByProtocol,
  IN OUT RW_INFO  *Info,
  OUT    UINTN    *Handles,
  OUT    UINTN    *Protocols
  );

#define RW_SYSTEM_TAG_CONFIG  0x1000      // + configuration table index
#define RW_SYSTEM_TAG_FB      0x2000      // + GOP index

/**
  System table, configuration tables (Tag RW_SYSTEM_TAG_CONFIG + index),
  graphics output (frame buffer rows Tag RW_SYSTEM_TAG_FB + index) and TPM.
**/
VOID
RwSystemDecode (
  IN OUT RW_INFO  *Info
  );

/** Physical address a RwSystemDecode tag points at. **/
BOOLEAN
RwSystemTagAddress (
  IN  INT32   Tag,
  OUT UINT64  *Address
  );

/**
  TPM 2.0 (EFI_TCG2_PROTOCOL): capability, PCR values of every active bank
  (TPM2_PCR_Read) and the TCG2 event log decoded.
  @retval EFI_NOT_FOUND  no TPM.
**/
EFI_STATUS
RwTpmDecode (
  IN OUT RW_INFO  *Info
  );

#endif
