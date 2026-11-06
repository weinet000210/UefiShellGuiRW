/** @file
  RwCoreTest: Super I/O. QEMU q35 has no Super I/O chip on 2Eh/4Eh, so the
  checks cover that the probe comes back cleanly and reports none.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

VOID
TestSio (
  VOID
  )
{
  RW_SIO_LIST     List;
  RW_INFO         Info;
  EFI_STATUS      Status;
  UINTN           Index;
  CHAR8           Text[80];
  RW_SIO_CHIP     Fake;
  RW_DATA_SOURCE  Ds;

  Status = RwSioScan (&List);
  TestLog ("    RwSioScan: %r, %d chip(s)", Status, List.ChipCount);
  for (Index = 0; Index < List.ChipCount; Index++) {
    RwSioDescribe (&List.Chips[Index].Chip, Text, sizeof (Text));
    TestLog ("    %a, %d LDNs", Text, List.Chips[Index].LdnCount);
  }

  CHECK ((Status == EFI_NOT_FOUND) == (List.ChipCount == 0), "scan status matches the chip count");
  RwInfoInit (&Info);
  RwSioDecode (&List, &Info);
  CHECK (!Info.Oom && (Info.Count >= 1), "RwSioDecode reports chips or why there are none");
  RwInfoFree (&Info);

  ZeroMem (&Fake, sizeof (Fake));
  Fake.Port = 0x2E;
  Fake.Kind = RW_SIO_ITE;
  CHECK (AsciiStrCmp (RwSioLdnName (&Fake, 4), "Environment controller") == 0, "ITE LDN names");
  RwSioSourceInit (&Ds, &Fake, RW_SIO_GLOBAL);
  CHECK ((Ds.Limit == 0xFF) && (AsciiStrStr (Ds.Name, "global") != NULL), "Super I/O source for the global registers");
}
