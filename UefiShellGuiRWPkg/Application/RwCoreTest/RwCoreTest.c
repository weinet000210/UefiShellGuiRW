/** @file
  RwCoreTest entry point and log / check helpers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"
#include <Library/UefiLib.h>
#include <Protocol/SerialIo.h>

STATIC UINTN  mChecks;
STATIC UINTN  mFailed;

VOID
EFIAPI
TestLog (
  IN CONST CHAR8  *Format,
  ...
  )
{
  STATIC EFI_SERIAL_IO_PROTOCOL  *Serial;
  STATIC BOOLEAN                 Looked;
  CHAR8                          Line[320];
  VA_LIST                        Args;
  UINTN                          Size;

  if (!Looked) {
    Looked = TRUE;
    gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&Serial);
  }

  VA_START (Args, Format);
  Size = AsciiVSPrint (Line, sizeof (Line) - 2, Format, Args);
  VA_END (Args);
  Line[Size++] = '\r';
  Line[Size++] = '\n';
  Line[Size]   = '\0';
  if (Serial != NULL) {
    Serial->Write (Serial, &Size, Line);
  }
}

BOOLEAN
TestCheck (
  IN BOOLEAN      Ok,
  IN CONST CHAR8  *What,
  IN CONST CHAR8  *File,
  IN UINTN        Line
  )
{
  CONST CHAR8  *Base;
  CONST CHAR8  *P;

  mChecks++;
  if (Ok) {
    TestLog ("  ok   %a", What);
    return TRUE;
  }

  mFailed++;
  Base = File;
  for (P = File; *P != '\0'; P++) {
    if ((*P == '/') || (*P == '\\')) {
      Base = P + 1;
    }
  }

  TestLog ("  FAIL %a (%a:%d)", What, Base, Line);
  return FALSE;
}

typedef struct {
  CONST CHAR8    *Name;
  VOID           (*Run)(VOID);
} TEST_MODULE;

STATIC CONST TEST_MODULE  mModules[] = {
  { "Base",   TestBase   },
  { "Pci",    TestPci    },
  { "Space",  TestSpace  },
  { "Msr",    TestMsr    },
  { "Sio",    TestSio    },
  { "Smbios", TestSmbios },
  { "Acpi",   TestAcpi   },
  { "Usb",    TestUsb    },
  { "Uart",   TestUart   },
};

EFI_STATUS
EFIAPI
RwCoreTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Index;
  UINTN  Before;

  TestLog ("");
  TestLog ("RwCoreTest: RwCoreLib checks");
  for (Index = 0; Index < ARRAY_SIZE (mModules); Index++) {
    TestLog ("[%a]", mModules[Index].Name);
    Before = mFailed;
    mModules[Index].Run ();
    TestLog ("[%a] %a", mModules[Index].Name, (mFailed == Before) ? "done" : "FAILED");
  }

  TestLog ("RwCoreTest: %d checks, %d failed", mChecks, mFailed);
  TestLog ("RWCORETEST: %a", (mFailed == 0) ? "PASS" : "FAIL");
  return (mFailed == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
