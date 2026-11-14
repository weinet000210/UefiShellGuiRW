/** @file
  RwCoreLib calculator: a recursive descent parser over 64-bit integers.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define CALC_MAX_DEPTH  64

typedef struct {
  CONST CHAR8    *Start;
  CONST CHAR8    *P;
  BOOLEAN        Failed;
  CHAR8          *Error;
  UINTN          ErrorSize;
  UINTN          Depth;
} CALC;

STATIC
VOID
Fail (
  IN OUT CALC         *C,
  IN     CONST CHAR8  *What
  )
{
  if (!C->Failed) {
    C->Failed = TRUE;
    if ((C->Error != NULL) && (C->ErrorSize > 0)) {
      AsciiSPrint (C->Error, C->ErrorSize, "%a at column %d", What, (UINTN)(C->P - C->Start) + 1);
    }
  }
}

STATIC
VOID
Skip (
  IN OUT CALC  *C
  )
{
  while ((*C->P == ' ') || (*C->P == '\t')) {
    C->P++;
  }
}

STATIC
INTN
Digit (
  IN CHAR8  Ch
  )
{
  if ((Ch >= '0') && (Ch <= '9')) {
    return Ch - '0';
  }

  if ((Ch >= 'a') && (Ch <= 'f')) {
    return Ch - 'a' + 10;
  }

  if ((Ch >= 'A') && (Ch <= 'F')) {
    return Ch - 'A' + 10;
  }

  return -1;
}

STATIC
UINT64
Number (
  IN OUT CALC  *C
  )
{
  CONST CHAR8  *Begin;
  CONST CHAR8  *End;
  UINT64       Value;
  UINTN        Base;
  INTN         D;

  Base = 16;
  if ((C->P[0] == '0') && ((C->P[1] == 'x') || (C->P[1] == 'X'))) {
    C->P += 2;
  } else if ((C->P[0] == '0') && ((C->P[1] == 'y') || (C->P[1] == 'Y'))) {
    C->P += 2;
    Base  = 2;
  }

  Begin = C->P;
  while (Digit (*C->P) >= 0) {
    C->P++;
  }

  End = C->P;
  if (Base == 16) {
    if ((*C->P == 'n') || (*C->P == 'N')) {
      Base = 10;
      C->P++;
    } else if ((*C->P == 'h') || (*C->P == 'H')) {
      C->P++;
    }
  }

  if (End == Begin) {
    Fail (C, "number expected");
    return 0;
  }

  Value = 0;
  for ( ; Begin < End; Begin++) {
    D = Digit (*Begin);
    if ((UINTN)D >= Base) {
      C->P = Begin;
      Fail (C, (Base == 10) ? "not a decimal digit" : "not a binary digit");
      return 0;
    }

    Value = MultU64x32 (Value, (UINT32)Base) + (UINT64)D;
  }

  return Value;
}

STATIC
UINT64
Or (
  IN OUT CALC  *C
  );

STATIC
UINT64
Unary (
  IN OUT CALC  *C
  )
{
  UINT64  Value;

  Skip (C);
  if (C->Failed) {
    return 0;
  }

  if (++C->Depth > CALC_MAX_DEPTH) {
    Fail (C, "expression too deep");
    return 0;
  }

  switch (*C->P) {
    case '-':
      C->P++;
      Value = 0 - Unary (C);
      break;
    case '~':
      C->P++;
      Value = ~Unary (C);
      break;
    case '!':
      C->P++;
      Value = (Unary (C) == 0) ? 1 : 0;
      break;
    case '+':
      C->P++;
      Value = Unary (C);
      break;
    case '(':
      C->P++;
      Value = Or (C);
      Skip (C);
      if (*C->P != ')') {
        Fail (C, "')' expected");
      } else {
        C->P++;
      }

      break;
    default:
      Value = Number (C);
      break;
  }

  C->Depth--;
  Skip (C);
  return Value;
}

STATIC
UINT64
Mul (
  IN OUT CALC  *C
  )
{
  UINT64  Value;
  UINT64  Right;
  UINT64  Rem;
  CHAR8   Op;

  Value = Unary (C);
  while (!C->Failed && ((*C->P == '*') || (*C->P == '/') || (*C->P == '%'))) {
    Op = *C->P++;
    Right = Unary (C);
    if (Op == '*') {
      Value = MultU64x64 (Value, Right);
    } else if (Right == 0) {
      Fail (C, "division by zero");
    } else {
      Value = DivU64x64Remainder (Value, Right, &Rem);
      if (Op == '%') {
        Value = Rem;
      }
    }
  }

  return Value;
}

STATIC
UINT64
Add (
  IN OUT CALC  *C
  )
{
  UINT64  Value;
  CHAR8   Op;

  Value = Mul (C);
  while (!C->Failed && ((*C->P == '+') || (*C->P == '-'))) {
    Op    = *C->P++;
    Value = (Op == '+') ? Value + Mul (C) : Value - Mul (C);
  }

  return Value;
}

STATIC
UINT64
Shift (
  IN OUT CALC  *C
  )
{
  UINT64  Value;
  UINT64  Count;
  BOOLEAN Left;

  Value = Add (C);
  while (!C->Failed && (((C->P[0] == '<') && (C->P[1] == '<')) || ((C->P[0] == '>') && (C->P[1] == '>')))) {
    Left  = (BOOLEAN)(C->P[0] == '<');
    C->P += 2;
    Count = Add (C);
    if (Count >= 64) {
      Value = 0;
    } else {
      Value = Left ? LShiftU64 (Value, (UINTN)Count) : RShiftU64 (Value, (UINTN)Count);
    }
  }

  return Value;
}

STATIC
UINT64
And (
  IN OUT CALC  *C
  )
{
  UINT64  Value;

  Value = Shift (C);
  while (!C->Failed && (*C->P == '&')) {
    C->P++;
    Value &= Shift (C);
  }

  return Value;
}

STATIC
UINT64
Xor (
  IN OUT CALC  *C
  )
{
  UINT64  Value;

  Value = And (C);
  while (!C->Failed && (*C->P == '^')) {
    C->P++;
    Value ^= And (C);
  }

  return Value;
}

STATIC
UINT64
Or (
  IN OUT CALC  *C
  )
{
  UINT64  Value;

  Value = Xor (C);
  while (!C->Failed && (*C->P == '|')) {
    C->P++;
    Value |= Xor (C);
  }

  return Value;
}

EFI_STATUS
RwCalcEval (
  IN  CONST CHAR8  *Expression,
  OUT UINT64       *Value,
  OUT CHAR8        *Error,
  IN  UINTN        ErrorSize
  )
{
  CALC  C;

  ZeroMem (&C, sizeof (C));
  C.Start     = Expression;
  C.P         = Expression;
  C.Error     = Error;
  C.ErrorSize = ErrorSize;
  if ((Error != NULL) && (ErrorSize > 0)) {
    Error[0] = '\0';
  }

  *Value = 0;
  Skip (&C);
  if (*C.P == '\0') {
    Fail (&C, "empty expression");
    return EFI_INVALID_PARAMETER;
  }

  *Value = Or (&C);
  if (!C.Failed && (*C.P != '\0')) {
    Fail (&C, "unexpected character");
  }

  if (C.Failed) {
    *Value = 0;
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

VOID
RwCalcDecode (
  IN     UINT64   Value,
  IN OUT RW_INFO  *Info
  )
{
  CHAR8  Text[96];
  UINTN  Bit;
  UINTN  Pos;
  UINTN  Index;
  UINT8  Byte;
  UINT64 Octal;

  RwInfoField (Info, 0, RwEmStrong, "Hex", "%016lX", Value);
  RwInfoField (Info, 0, RwEmNormal, "Unsigned", "%lu", Value);
  if ((INT64)Value < 0) {
    RwInfoField (Info, 0, RwEmNormal, "Signed", "-%lu", 0 - Value);
  } else {
    RwInfoField (Info, 0, RwEmNormal, "Signed", "%lu", Value);
  }

  Pos = sizeof (Text) - 1;
  Text[Pos] = '\0';
  Octal     = Value;
  do {
    Text[--Pos] = (CHAR8)('0' + (Octal & 7));
    Octal       = RShiftU64 (Octal, 3);
  } while (Octal != 0);

  RwInfoField (Info, 0, RwEmNormal, "Octal", "%a", &Text[Pos]);

  Pos = 0;
  for (Bit = 64; Bit > 0; Bit--) {
    Text[Pos++] = (CHAR8)(((RShiftU64 (Value, Bit - 1) & 1) != 0) ? '1' : '0');
    if (((Bit - 1) % 4 == 0) && (Bit > 1)) {
      Text[Pos++] = ((Bit - 1) % 16 == 0) ? ':' : ' ';
    }
  }

  Text[Pos] = '\0';
  RwInfoField (Info, 0, RwEmNormal, "Binary", "%a", Text);

  for (Index = 0; Index < 8; Index++) {
    Byte        = (UINT8)RShiftU64 (Value, (7 - Index) * 8);
    Text[Index] = ((Byte >= 0x20) && (Byte <= 0x7E)) ? (CHAR8)Byte : '.';
  }

  Text[8] = '\0';
  RwInfoField (Info, 0, RwEmNormal, "ASCII", "%a", Text);
}
