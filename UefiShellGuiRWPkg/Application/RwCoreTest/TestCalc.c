/** @file
  RwCoreTest: calculator expressions.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

STATIC
BOOLEAN
Is (
  IN CONST CHAR8  *Expression,
  IN UINT64       Expected
  )
{
  UINT64  Value;
  CHAR8   Error[64];

  return (BOOLEAN)(!EFI_ERROR (RwCalcEval (Expression, &Value, Error, sizeof (Error))) && (Value == Expected));
}

VOID
TestCalc (
  VOID
  )
{
  UINT64   Value;
  CHAR8    Error[64];
  RW_INFO  Info;

  CHECK (Is ("1F", 0x1F) && Is ("0x1F", 0x1F) && Is ("1Fh", 0x1F) && Is ("31n", 31) && Is ("0y1010", 10), "number formats");
  CHECK (Is ("1 + 2 * 3", 7) && Is ("(1 + 2) * 3", 9) && Is ("10 - 1", 0xF), "precedence and parentheses");
  CHECK (Is ("1 << 4 | 3", 0x13) && Is ("FF & ~F", 0xF0) && Is ("A ^ 3", 9) && Is ("100n / 7n", 14) && Is ("100n % 7n", 2), "bit and division operators");
  CHECK (Is ("-1", MAX_UINT64) && Is ("FFFFFFFFFFFFFFFF + 2", 1) && Is ("!0", 1), "unary operators and 64-bit wrap");
  CHECK (EFI_ERROR (RwCalcEval ("1 / 0", &Value, Error, sizeof (Error))) && (AsciiStrStr (Error, "division by zero") != NULL), "division by zero is an error");
  CHECK (EFI_ERROR (RwCalcEval ("(1 + 2", &Value, Error, sizeof (Error))) && (AsciiStrStr (Error, "')' expected") != NULL), "unbalanced parenthesis");
  CHECK (EFI_ERROR (RwCalcEval ("12n9", &Value, Error, sizeof (Error))) && EFI_ERROR (RwCalcEval ("0y12", &Value, Error, sizeof (Error))), "bad digits");
  RwInfoInit (&Info);
  RwCalcDecode (0x41424344, &Info);
  CHECK ((Info.Count == 6) && (AsciiStrCmp (Info.Rows[2].Value, "1094861636") == 0) && (AsciiStrCmp (Info.Rows[3].Value, "10120441504") == 0) && (AsciiStrCmp (Info.Rows[5].Value, "....ABCD") == 0), "value in every base");
  RwInfoFree (&Info);
  RwInfoInit (&Info);
  RwCalcDecode (MAX_UINT64, &Info);
  CHECK (AsciiStrCmp (Info.Rows[2].Value, "-1") == 0, "signed value");
  RwInfoFree (&Info);
}
