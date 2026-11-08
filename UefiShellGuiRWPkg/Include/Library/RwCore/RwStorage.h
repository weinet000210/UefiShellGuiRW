/** @file
  RwCoreLib storage module: IDENTIFY data of the drives the firmware drives,
  through EFI_ATA_PASS_THRU_PROTOCOL (ATA IDENTIFY DEVICE / IDENTIFY PACKET
  DEVICE) and EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL (Identify Controller and
  Identify Namespace), and their decoders.

  Only identify commands are sent; nothing is written to a drive.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWSTORAGE_H_
#define RW_RWSTORAGE_H_

#include <Library/RwCore/RwBase.h>

#define RW_STORAGE_MAX  32

typedef enum {
  RwStorAta       = 0,   // IDENTIFY DEVICE, 512 bytes
  RwStorAtapi     = 1,   // IDENTIFY PACKET DEVICE, 512 bytes
  RwStorNvmeCtrl  = 2,   // Identify Controller, 4096 bytes
  RwStorNvmeNs    = 3    // Identify Namespace, 4096 bytes
} RW_STORAGE_KIND;

typedef struct {
  UINT8      Kind;            // RW_STORAGE_KIND
  INT32      Parent;          // NVMe namespace: index of its controller, else -1
  UINT16     Port;            // ATA port / port multiplier port
  UINT16     PmPort;
  UINT32     Nsid;            // NVMe namespace ID (0 for the controller)
  UINTN      Size;            // bytes in Data
  UINT8      *Data;           // identify data (AllocatePool)
  CHAR8      Model[48];       // model / product, trimmed
  CHAR8      Location[96];    // device path of the controller, shortened
  UINT64     Capacity;        // bytes (0 for ATAPI and controllers without TNVMCAP)
} RW_STORAGE_DEV;

typedef struct {
  RW_STORAGE_DEV    Devs[RW_STORAGE_MAX];
  UINTN             Count;
} RW_STORAGE_LIST;

/**
  Identify every ATA / ATAPI device and every NVMe controller with its
  active namespaces. Release with RwStorageFree.

  @retval EFI_NOT_FOUND  no ATA or NVMe pass-thru protocol.
**/
EFI_STATUS
RwStorageScan (
  OUT RW_STORAGE_LIST  *List
  );

VOID
RwStorageFree (
  IN OUT RW_STORAGE_LIST  *List
  );

/** "ATA", "ATAPI", "NVMe controller", "NVMe namespace". **/
CONST CHAR8 *
RwStorageKindName (
  IN UINT8  Kind
  );

/** ATA string field (byte-swapped words) to ASCII, trimmed. **/
VOID
RwAtaString (
  IN  CONST UINT8  *Identify,
  IN  UINTN        FirstWord,
  IN  UINTN        Words,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  );

/** Decode 512 bytes of ATA IDENTIFY (DEVICE or PACKET DEVICE when Atapi). **/
VOID
RwAtaIdentifyDecode (
  IN     CONST UINT8  *Identify,
  IN     BOOLEAN      Atapi,
  IN OUT RW_INFO      *Info
  );

/** Decode 4096 bytes of NVMe Identify Controller. **/
VOID
RwNvmeIdCtrlDecode (
  IN     CONST UINT8  *Identify,
  IN OUT RW_INFO      *Info
  );

/** Decode 4096 bytes of NVMe Identify Namespace. **/
VOID
RwNvmeIdNsDecode (
  IN     CONST UINT8  *Identify,
  IN OUT RW_INFO      *Info
  );

/** Decode one entry (by kind), with its identify data as hex at the end. **/
VOID
RwStorageDecode (
  IN     CONST RW_STORAGE_DEV  *Dev,
  IN OUT RW_INFO               *Info
  );

/** The identify data of Dev as a read-only source. **/
VOID
RwStorageSourceInit (
  OUT RW_DATA_SOURCE        *Source,
  IN  CONST RW_STORAGE_DEV  *Dev
  );

#endif
