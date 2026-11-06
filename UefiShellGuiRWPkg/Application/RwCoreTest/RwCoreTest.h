/** @file
  RwCoreTest: checks RwCoreLib against the known hardware of a QEMU q35
  machine running OVMF. Results go to the serial port; the last line is
  "RWCORETEST: PASS" or "RWCORETEST: FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_CORE_TEST_H_
#define RW_CORE_TEST_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/RwCoreLib.h>

/**
  Print one line to the serial log (CHAR8 format, %a for strings).
**/
VOID
EFIAPI
TestLog (
  IN CONST CHAR8  *Format,
  ...
  );

/**
  Record a check. Prints "  ok   What" or "  FAIL What (File:Line)".
**/
BOOLEAN
TestCheck (
  IN BOOLEAN      Ok,
  IN CONST CHAR8  *What,
  IN CONST CHAR8  *File,
  IN UINTN        Line
  );

#define CHECK(Cond, What)  TestCheck ((BOOLEAN)((Cond) ? TRUE : FALSE), (What), __FILE__, __LINE__)

//
// One function per module, each in its own Test<Module>.c.
//
VOID TestBase   (VOID);
VOID TestPci    (VOID);
VOID TestSpace  (VOID);
VOID TestMsr    (VOID);
VOID TestSio    (VOID);
VOID TestSmbios (VOID);
VOID TestAcpi   (VOID);
VOID TestUsb    (VOID);
VOID TestUart   (VOID);

#endif
