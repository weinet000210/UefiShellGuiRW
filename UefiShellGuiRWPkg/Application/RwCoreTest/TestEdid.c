/** @file
  RwCoreTest: EDID of the QEMU standard VGA (1024x768 preferred).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

VOID
TestEdid (
  VOID
  )
{
  STATIC RW_EDID_LIST  List;
  EFI_STATUS           Status;
  RW_EDID              *E;
  CHAR8                Pnp[4];
  RW_INFO              Info;
  UINTN                Index;
  BOOLEAN              Preferred;
  BOOLEAN              Checksum;

  Status = RwEdidScan (&List);
  CHECK (!EFI_ERROR (Status) && (List.Count >= 1), "an EDID is found");
  if (List.Count == 0) {
    return;
  }

  E = &List.Displays[0];
  RwEdidPnpId (E->Data, Pnp);
  TestLog ("    %a: %a (%a), %d bytes", E->Source, E->Name, Pnp, E->Size);
  CHECK (RwEdidHasHeader (E->Data, E->Size), "EDID header");
  CHECK (AsciiStrCmp (Pnp, "RHT") == 0, "QEMU EDID vendor RHT");
  CHECK (AsciiStrStr (E->Name, "QEMU") != NULL, "monitor name descriptor");

  RwInfoInit (&Info);
  RwEdidDecode (E->Data, E->Size, &Info);
  Preferred = FALSE;
  Checksum  = FALSE;
  for (Index = 0; Index < Info.Count; Index++) {
    if ((AsciiStrCmp (Info.Rows[Index].Name, "Preferred timing") == 0) && (AsciiStrStr (Info.Rows[Index].Value, "1024x768") != NULL)) {
      Preferred = TRUE;
    }

    if ((AsciiStrCmp (Info.Rows[Index].Name, "Base block checksum") == 0) && (AsciiStrStr (Info.Rows[Index].Value, "OK") != NULL)) {
      Checksum = TRUE;
    }
  }

  CHECK (Preferred, "preferred timing 1024x768");
  CHECK (Checksum, "base block checksum");
  RwInfoFree (&Info);
}
