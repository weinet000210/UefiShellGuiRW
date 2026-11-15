/** @file
  RwCoreTest: GUID names, handle database, system table, GOP and TPM.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"
#include <Protocol/PciIo.h>

STATIC
INTN
FindRow (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name,
  IN CONST CHAR8    *ValuePart
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if ((AsciiStrStr (Info->Rows[Index].Name, Name) != NULL) &&
        ((ValuePart == NULL) || (AsciiStrStr (Info->Rows[Index].Value, ValuePart) != NULL)))
    {
      return (INTN)Index;
    }
  }

  return -1;
}

VOID
TestUefi (
  VOID
  )
{
  RW_INFO     Info;
  UINTN       Handles;
  UINTN       Protocols;
  EFI_STATUS  Status;
  INTN        Row;
  UINT64      Address;
  CHAR8       Text[64];

  CHECK (AsciiStrCmp (RwGuidName (&gEfiPciIoProtocolGuid), "EfiPciIoProtocol") == 0, "GUID name table");
  RwGuidText (&gEfiCallerIdGuid, Text, sizeof (Text));
  CHECK (AsciiStrLen (Text) == 36, "unknown GUIDs in registry format");

  RwInfoInit (&Info);
  Status = RwHandleDbDecode (FALSE, &Info, &Handles, &Protocols);
  TestLog ("    %d handles, %d protocols, %d rows", Handles, Protocols, Info.Count);
  CHECK (!EFI_ERROR (Status) && (Handles > 50) && (Protocols > 20), "handle database");
  CHECK (FindRow (&Info, "EfiPciIoProtocol", NULL) >= 0, "PCI I/O protocol listed under a handle");
  CHECK (FindRow (&Info, "Opened by", "BY_DRIVER") >= 0, "who opened a protocol");
  CHECK (FindRow (&Info, "EfiDevicePathProtocol", "PciRoot(0x0)") >= 0, "device paths as text");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  RwHandleDbDecode (TRUE, &Info, &Handles, &Protocols);
  Row = FindRow (&Info, "EfiPciIoProtocol", "handles");
  CHECK ((Row >= 0) && (Row + 1 < (INTN)Info.Count) && (Info.Rows[Row + 1].Level == 1) && (Info.Rows[Row + 1].Tag > 0), "by protocol: handles under each protocol");
  RwInfoFree (&Info);

  RwInfoInit (&Info);
  RwSystemDecode (&Info);
  CHECK (FindRow (&Info, "Firmware vendor", "EDK II") >= 0, "system table: firmware vendor");
  Row = FindRow (&Info, "EfiAcpi", NULL);
  CHECK ((Row >= 0) && RwSystemTagAddress (Info.Rows[Row].Tag, &Address) && (Address != 0), "configuration table addresses");
  Row = FindRow (&Info, "Frame buffer", NULL);
  CHECK ((Row >= 0) && RwSystemTagAddress (Info.Rows[Row].Tag, &Address) && (Address != 0), "GOP frame buffer");
  CHECK (FindRow (&Info, "Mode 0", NULL) >= 0, "GOP modes listed");
  //
  // The test machine has a swtpm TPM 2.0 when the runner starts one.
  //
  Row = FindRow (&Info, "Manufacturer", NULL);
  if (Row >= 0) {
    TestLog ("    TPM: %a", Info.Rows[Row].Value);
    CHECK (FindRow (&Info, "PCR  0", NULL) >= 0, "TPM PCR values");
    CHECK (FindRow (&Info, "Spec ID event", "SHA") >= 0, "TPM event log Spec ID event");
    CHECK (FindRow (&Info, "PCR", "EV_") >= 0, "TPM event log events");
  } else {
    TestLog ("    no TPM on this machine: TPM checks skipped");
  }

  RwInfoFree (&Info);
}
