/** @file
  RwCoreLib storage module: IDENTIFY data of the drives the firmware drives,
  through EFI_ATA_PASS_THRU_PROTOCOL (ATA IDENTIFY DEVICE / IDENTIFY PACKET
  DEVICE) and EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL (Identify Controller and
  Identify Namespace), and their decoders.

  Health data comes from read-only commands as well: ATA SMART READ DATA and
  SMART READ THRESHOLDS (only when SMART is already enabled), and the NVMe
  SMART / Health (02h), Error Information (01h) and Firmware Slot (03h) log
  pages. Nothing is written to a drive and SMART is never enabled.

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
  //
  // Health data (ATA: SMART data + thresholds, 1024 bytes; NVMe controller:
  // Health log, Firmware Slot log, then HealthErrors Error log entries).
  //
  EFI_STATUS    HealthStatus;  // EFI_UNSUPPORTED: no SMART / not enabled, EFI_NOT_FOUND: namespace
  UINT8         *Health;
  UINTN         HealthSize;
  UINTN         HealthErrors;  // NVMe error log entries in Health
} RW_STORAGE_DEV;

#define RW_NVME_LOG_HEALTH     0      // offsets in RW_STORAGE_DEV.Health
#define RW_NVME_LOG_FW_SLOT    512
#define RW_NVME_LOG_ERRORS     1024
#define RW_NVME_ERROR_ENTRY    64
#define RW_NVME_ERRORS_MAX     16
#define RW_ATA_SMART_DATA      0
#define RW_ATA_SMART_THRESH    512

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

/**
  Decode the health data of Dev: SMART attributes with thresholds (ATA) or
  the Health, Firmware Slot and Error log pages (NVMe controller), raw bytes
  at the end. Explains when there is none.
**/
VOID
RwStorageHealthDecode (
  IN     CONST RW_STORAGE_DEV  *Dev,
  IN OUT RW_INFO               *Info
  );

/** Name of an ATA SMART attribute ID, "" when unknown. **/
CONST CHAR8 *
RwAtaSmartAttrName (
  IN UINT8  Id
  );

/** The identify data of Dev as a read-only source. **/
VOID
RwStorageSourceInit (
  OUT RW_DATA_SOURCE        *Source,
  IN  CONST RW_STORAGE_DEV  *Dev
  );

#endif
