/** @file
  RwCoreLib calculator: 64-bit integer expressions as RW-Everything's
  calculator takes them, and the value in every base.

  Numbers are hexadecimal by default ("1F", "0x1F", "1Fh"); a trailing 'n'
  makes a number decimal ("31n") and "0y" starts a binary one ("0y1010").
  Operators, from the tightest: unary - ~ !, then * / %, + -, << >>, &, ^, |.
  Parentheses group. Arithmetic wraps at 64 bits; / and % are unsigned.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWCALC_H_
#define RW_RWCALC_H_

#include <Library/RwCore/RwBase.h>

/**
  Evaluate Expression.
  @param[out] Error  why it failed ("division by zero", "unexpected ')' at 4"), may be NULL.
  @retval EFI_INVALID_PARAMETER  a syntax error or division by zero.
**/
EFI_STATUS
RwCalcEval (
  IN  CONST CHAR8  *Expression,
  OUT UINT64       *Value,
  OUT CHAR8        *Error,
  IN  UINTN        ErrorSize
  );

/**
  Value as hex, unsigned and signed decimal, octal, binary (grouped by 4,
  64 bits) and ASCII bytes, as fields.
**/
VOID
RwCalcDecode (
  IN     UINT64   Value,
  IN OUT RW_INFO  *Info
  );

#endif
