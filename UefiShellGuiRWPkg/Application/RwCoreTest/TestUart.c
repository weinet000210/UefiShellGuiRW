/** @file
  RwCoreTest: UART module.

  COM1 (3F8h) is present on QEMU q35 and carries the test log, so it is
  only detected and its status read: no loopback, no line setting changes,
  nothing sent. COM2..COM4 are only logged.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

STATIC
VOID
TestUartParseHex (
  VOID
  )
{
  UINT8  Bytes[8];
  UINTN  Count;

  Count = RwUartParseHex ("55 aa 1 0203", Bytes, sizeof (Bytes));
  CHECK (
    (Count == 5) && (Bytes[0] == 0x55) && (Bytes[1] == 0xAA) && (Bytes[2] == 0x01) &&
    (Bytes[3] == 0x02) && (Bytes[4] == 0x03),
    "UART hex parse: pairs, single digit, run of digits"
    );
  Count = RwUartParseHex ("F", Bytes, sizeof (Bytes));
  CHECK ((Count == 1) && (Bytes[0] == 0x0F), "UART hex parse: trailing single digit");
  Count = RwUartParseHex ("112233", Bytes, 2);
  CHECK (Count == 2, "UART hex parse: respects MaxBytes");
}

VOID
TestUart (
  VOID
  )
{
  UINTN       Index;
  UINT16      Base;
  BOOLEAN     Present;
  EFI_HANDLE  Fw;
  RW_INFO     Info;

  TestLog ("UART");
  TestUartParseHex ();

  CHECK (RwUartStdBase (0) == 0x3F8, "UART COM1 base is 3F8h");
  CHECK (RwUartStdBase (RW_UART_STD_COUNT) == 0, "UART std base out of range is 0");
  CHECK (RwUartStdBaud (RW_UART_BAUD_COUNT - 1) == 115200, "UART fastest std baud 115200");
  CHECK (RwUartSetup (0x3F8, 0) == EFI_INVALID_PARAMETER, "UART setup rejects baud 0 (nothing written)");

  CHECK (RwUartPresent (0x3F8), "UART COM1 at 3F8h detected");

  for (Index = 0; Index < RW_UART_STD_COUNT; Index++) {
    Base    = RwUartStdBase (Index);
    Present = RwUartPresent (Base);
    Fw      = RwUartFirmwareHandle (Base);
    TestLog (
      "  COM%d %03Xh: %a, firmware Serial I/O %a",
      (UINT32)(Index + 1),
      Base,
      Present ? "present" : "not found",
      (Fw != NULL) ? "yes" : "no"
      );
  }

  //
  // Status of COM1: reads LSR / MSR only.
  //
  RwInfoInit (&Info);
  RwUartDecodeStatus (0x3F8, NULL, &Info);
  CHECK ((Info.Count >= 3) && (AsciiStrCmp (Info.Rows[0].Name, "Port 3F8h") == 0), "UART COM1 status decode");
  if (Info.Count >= 3) {
    TestLog ("  COM1 LSR %a, MSR %a", Info.Rows[1].Value, Info.Rows[2].Value);
  }

  RwInfoFree (&Info);
}
