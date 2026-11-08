/** @file
  RwCoreLib EDID module: monitor EDID from the firmware (EFI_EDID_ACTIVE /
  EFI_EDID_DISCOVERED on the graphics output handles) or, when no driver
  publishes one, from the QEMU / Bochs standard VGA EDID window; and the
  EDID 1.x decoder (base block, detailed timings, descriptors, extension
  list).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWEDID_H_
#define RW_RWEDID_H_

#include <Library/RwCore/RwBase.h>

#define RW_EDID_MAX_DISPLAYS  8
#define RW_EDID_MAX_SIZE      1024

typedef struct {
  CHAR8    Source[64];          // "EDID Active (GOP handle 1)", "QEMU stdvga 00:01.0"
  CHAR8    Name[32];            // monitor name descriptor, else "PNP xxxx"
  UINTN    Size;
  UINT8    Data[RW_EDID_MAX_SIZE];
} RW_EDID;

typedef struct {
  RW_EDID    Displays[RW_EDID_MAX_DISPLAYS];
  UINTN      Count;
} RW_EDID_LIST;

/**
  Collect every EDID: EFI_EDID_ACTIVE first, EFI_EDID_DISCOVERED for handles
  without one, then the QEMU standard VGA (1234:1111) EDID window when the
  firmware published none. Only blocks with the EDID header are kept.

  @retval EFI_NOT_FOUND  no EDID anywhere.
**/
EFI_STATUS
RwEdidScan (
  OUT RW_EDID_LIST  *List
  );

/** TRUE for the 8-byte EDID header 00 FF FF FF FF FF FF 00. **/
BOOLEAN
RwEdidHasHeader (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  );

/** Three-letter PNP manufacturer ID of bytes 8-9 ("SAM", "QEM"). **/
VOID
RwEdidPnpId (
  IN  CONST UINT8  *Data,
  OUT CHAR8        Id[4]
  );

/**
  Decode: header, vendor / product, version, display parameters, colour
  characteristics, established and standard timings, the four descriptors
  (detailed timings, name, serial, range limits, text), extensions with
  their checksums; then the raw bytes. Field rows carry their offsets.
**/
VOID
RwEdidDecode (
  IN     CONST UINT8  *Data,
  IN     UINTN        Size,
  IN OUT RW_INFO      *Info
  );

/** The EDID bytes as a read-only source (Edid must stay valid). **/
VOID
RwEdidSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_EDID   *Edid
  );

#endif
