/** @file
  RwCoreLib UART module: polled 16550 access, firmware Serial I/O lookup,
  line settings, send / receive into a stream buffer, status decode and the
  internal loopback self-test. Ported from the hardware parts of uefirrw
  ComView.c (the terminal screen and its keys stay in the UI).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/DevicePathLib.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SuperIo.h>
#include <Protocol/DevicePath.h>

STATIC CONST UINT32  mBauds[RW_UART_BAUD_COUNT]  = { 9600, 19200, 38400, 57600, 115200 };
STATIC CONST UINT16  mStdPorts[RW_UART_STD_COUNT] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };

#define TX_WAIT_US    20000     // ~20 ms: transmitter stuck (no clock, or flow control in hardware)
#define POLL_MAX      4096

STATIC
UINT8
UartIn (
  IN UINT16  Base,
  IN UINTN   Reg
  )
{
  return IoRead8 ((UINTN)Base + Reg);
}

STATIC
VOID
UartOut (
  IN UINT16  Base,
  IN UINTN   Reg,
  IN UINT8   Value
  )
{
  IoWrite8 ((UINTN)Base + Reg, Value);
}

UINT16
RwUartStdBase (
  IN UINTN  Index
  )
{
  return (Index < RW_UART_STD_COUNT) ? mStdPorts[Index] : 0;
}

UINT32
RwUartStdBaud (
  IN UINTN  Index
  )
{
  return (Index < RW_UART_BAUD_COUNT) ? mBauds[Index] : 0;
}

BOOLEAN
RwUartPresent (
  IN UINT16  Base
  )
{
  UINT8    Saved;
  BOOLEAN  Ok;

  if (UartIn (Base, RW_UART_LSR) == 0xFF) {
    return FALSE;
  }

  Saved = UartIn (Base, RW_UART_SCR);
  UartOut (Base, RW_UART_SCR, 0x55);
  Ok = (BOOLEAN)(UartIn (Base, RW_UART_SCR) == 0x55);
  UartOut (Base, RW_UART_SCR, 0xAA);
  Ok = (BOOLEAN)(Ok && (UartIn (Base, RW_UART_SCR) == 0xAA));
  UartOut (Base, RW_UART_SCR, Saved);
  return Ok;
}

UINT16
RwUartSerialIoBase (
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  EFI_HANDLE                Sio;
  EFI_SIO_PROTOCOL          *SioProto;
  ACPI_RESOURCE_HEADER_PTR  Res;
  UINT8                     *P;
  UINTN                     Guard;
  UINT8                     Tag;
  UINTN                     Len;

  if (Path == NULL) {
    return 0;
  }

  if (EFI_ERROR (gBS->LocateDevicePath (&gEfiSioProtocolGuid, &Path, &Sio)) ||
      EFI_ERROR (gBS->HandleProtocol (Sio, &gEfiSioProtocolGuid, (VOID **)&SioProto)) ||
      EFI_ERROR (SioProto->GetResources (SioProto, &Res)) ||
      (Res.SmallHeader == NULL))
  {
    return 0;
  }

  P = (UINT8 *)Res.SmallHeader;
  for (Guard = 0; Guard < 64; Guard++) {
    if (P[0] & BIT7) {
      // large item: 1-byte tag, 2-byte length
      P += 3 + (P[1] | (P[2] << 8));
      continue;
    }

    Tag = (P[0] >> 3) & 0x0F;
    Len = P[0] & 0x07;
    if (Tag == 0x0F) {
      break;                                            // end tag
    }

    if (Tag == 0x08) {
      return (UINT16)(P[2] | (P[3] << 8));              // I/O port descriptor: minimum base
    }

    if (Tag == 0x09) {
      return (UINT16)((P[1] | (P[2] << 8)) & 0x3FF);    // fixed I/O
    }

    P += 1 + Len;
  }

  return 0;
}

EFI_HANDLE
RwUartFirmwareHandle (
  IN UINT16  Base
  )
{
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  UINTN                     Std;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  ACPI_HID_DEVICE_PATH      *Acpi;
  EFI_HANDLE                Found;
  UINT16                    SioBase;

  for (Std = 0; Std < RW_UART_STD_COUNT; Std++) {
    if (mStdPorts[Std] == Base) {
      break;
    }
  }

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSerialIoProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }

  Found = NULL;
  for (Index = 0; (Index < Count) && (Found == NULL); Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path))) {
      continue;
    }

    SioBase = RwUartSerialIoBase (Path);
    if (SioBase != 0) {
      if (SioBase == Base) {
        Found = Handles[Index];
      }

      continue;
    }

    if (Std == RW_UART_STD_COUNT) {
      continue;
    }

    for (Node = Path; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
      if ((DevicePathType (Node) == ACPI_DEVICE_PATH) && (DevicePathSubType (Node) == ACPI_DP)) {
        Acpi = (ACPI_HID_DEVICE_PATH *)Node;
        if ((Acpi->HID == EISA_PNP_ID (0x0501)) && (Acpi->UID == Std)) {
          Found = Handles[Index];
          break;
        }
      }
    }
  }

  FreePool (Handles);
  return Found;
}

VOID
RwUartSave (
  IN  UINT16        Base,
  OUT RW_UART_SAVE  *S
  )
{
  S->Lcr  = UartIn (Base, RW_UART_LCR);
  S->Ier  = UartIn (Base, RW_UART_IER);
  S->Mcr  = UartIn (Base, RW_UART_MCR);
  S->Fifo = (BOOLEAN)((UartIn (Base, RW_UART_IIR) & 0xC0) != 0);
  UartOut (Base, RW_UART_LCR, S->Lcr | BIT7);
  S->Dll = UartIn (Base, RW_UART_DLL);
  S->Dlm = UartIn (Base, RW_UART_DLM);
  UartOut (Base, RW_UART_LCR, S->Lcr);
}

VOID
RwUartRestore (
  IN UINT16              Base,
  IN CONST RW_UART_SAVE  *S
  )
{
  UartOut (Base, RW_UART_LCR, BIT7);
  UartOut (Base, RW_UART_DLL, S->Dll);
  UartOut (Base, RW_UART_DLM, S->Dlm);
  UartOut (Base, RW_UART_LCR, S->Lcr & 0x7F);
  UartOut (Base, RW_UART_FCR, S->Fifo ? 0x07 : 0x00);
  UartOut (Base, RW_UART_IER, S->Ier);
  UartOut (Base, RW_UART_MCR, S->Mcr);
}

EFI_STATUS
RwUartSetup (
  IN UINT16  Base,
  IN UINT32  Baud
  )
{
  UINT16  Divisor;

  if ((Baud == 0) || (Baud > RW_UART_CLOCK) || (RW_UART_CLOCK / Baud > MAX_UINT16)) {
    return EFI_INVALID_PARAMETER;
  }

  Divisor = (UINT16)(RW_UART_CLOCK / Baud);
  UartOut (Base, RW_UART_IER, 0x00);
  UartOut (Base, RW_UART_LCR, BIT7);
  UartOut (Base, RW_UART_DLL, (UINT8)Divisor);
  UartOut (Base, RW_UART_DLM, (UINT8)(Divisor >> 8));
  UartOut (Base, RW_UART_LCR, 0x03);                   // 8N1
  UartOut (Base, RW_UART_FCR, 0x07);                   // FIFO on, clear RX/TX
  UartOut (Base, RW_UART_MCR, 0x03);                   // DTR, RTS
  return EFI_SUCCESS;
}

BOOLEAN
RwUartSendByteRaw (
  IN UINT16  Base,
  IN UINT8   Byte
  )
{
  UINTN  Wait;

  for (Wait = 0; (UartIn (Base, RW_UART_LSR) & RW_UART_LSR_THRE) == 0; Wait++) {
    if (Wait > TX_WAIT_US) {
      return FALSE;
    }

    gBS->Stall (1);
  }

  UartOut (Base, RW_UART_THR, Byte);
  return TRUE;
}

STATIC
VOID
BufPush (
  IN OUT RW_UART  *Uart,
  IN     UINT16   Entry
  )
{
  if (Uart->Buf == NULL) {
    return;
  }

  if (Uart->Len == RW_UART_BUF_MAX) {
    CopyMem (Uart->Buf, Uart->Buf + RW_UART_BUF_MAX / 2, (RW_UART_BUF_MAX / 2) * sizeof (UINT16));
    Uart->Len = RW_UART_BUF_MAX / 2;
  }

  Uart->Buf[Uart->Len++] = Entry;
}

VOID
RwUartClear (
  IN OUT RW_UART  *Uart
  )
{
  Uart->Len     = 0;
  Uart->TxCount = 0;
  Uart->RxCount = 0;
  Uart->Overrun = 0;
  Uart->Framing = 0;
  Uart->Parity  = 0;
}

EFI_STATUS
RwUartOpen (
  OUT RW_UART  *Uart,
  IN  UINT16   Base,
  IN  UINT32   Baud,
  IN  BOOLEAN  PauseFirmware
  )
{
  if ((Uart == NULL) || (Base == 0) || (Base > 0xFFF8) ||
      (Baud == 0) || (Baud > RW_UART_CLOCK) || (RW_UART_CLOCK / Baud > MAX_UINT16))
  {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Uart, sizeof (*Uart));
  Uart->Base = Base;
  Uart->Baud = Baud;
  Uart->Buf  = AllocatePool (RW_UART_BUF_MAX * sizeof (UINT16));
  if (Uart->Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // The firmware's terminal driver polls its UART and would turn received
  // bytes into keystrokes. Detach it while the session owns the port.
  //
  if (PauseFirmware) {
    Uart->FwSerial = RwUartFirmwareHandle (Base);
    if (Uart->FwSerial != NULL) {
      if (!EFI_ERROR (gBS->DisconnectController (Uart->FwSerial, NULL, NULL))) {
        Uart->FwState = RwUartFwPaused;
      } else {
        Uart->FwSerial = NULL;
        Uart->FwState  = RwUartFwPauseFailed;
      }
    }
  }

  RwUartSave (Base, &Uart->Saved);
  RwUartSetup (Base, Baud);
  return EFI_SUCCESS;
}

VOID
RwUartClose (
  IN OUT RW_UART  *Uart
  )
{
  if ((Uart == NULL) || (Uart->Base == 0)) {
    return;
  }

  RwUartRestore (Uart->Base, &Uart->Saved);
  if ((Uart->FwSerial != NULL) && (Uart->FwState == RwUartFwPaused)) {
    gBS->ConnectController (Uart->FwSerial, NULL, NULL, TRUE);
  }

  if (Uart->Buf != NULL) {
    FreePool (Uart->Buf);
  }

  ZeroMem (Uart, sizeof (*Uart));
}

EFI_STATUS
RwUartSetBaud (
  IN OUT RW_UART  *Uart,
  IN     UINT32   Baud
  )
{
  EFI_STATUS  Status;

  if ((Uart == NULL) || (Uart->Base == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = RwUartSetup (Uart->Base, Baud);
  if (!EFI_ERROR (Status)) {
    Uart->Baud = Baud;
  }

  return Status;
}

BOOLEAN
RwUartSend (
  IN OUT RW_UART  *Uart,
  IN     UINT8    Byte
  )
{
  if ((Uart == NULL) || (Uart->Base == 0)) {
    return FALSE;
  }

  if (!RwUartSendByteRaw (Uart->Base, Byte)) {
    return FALSE;
  }

  BufPush (Uart, (UINT16)(Byte | RW_UART_DIR_TX));
  Uart->TxCount++;
  return TRUE;
}

UINTN
RwUartSendBuffer (
  IN OUT RW_UART      *Uart,
  IN     CONST UINT8  *Data,
  IN     UINTN        Count
  )
{
  UINTN  Index;

  if (Data == NULL) {
    return 0;
  }

  for (Index = 0; Index < Count; Index++) {
    if (!RwUartSend (Uart, Data[Index])) {
      break;
    }
  }

  return Index;
}

VOID
RwUartSendNewline (
  IN OUT RW_UART  *Uart,
  IN     UINT8    Mode
  )
{
  if (Mode != RwUartNewlineLf) {
    RwUartSend (Uart, '\r');
  }

  if (Mode != RwUartNewlineCr) {
    RwUartSend (Uart, '\n');
  }
}

STATIC
INTN
HexDigit (
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

UINTN
RwUartParseHex (
  IN  CONST CHAR8  *Text,
  OUT UINT8        *Bytes,
  IN  UINTN        MaxBytes
  )
{
  UINTN  Index;
  UINTN  Len;
  INTN   Hi;
  INTN   Digit;
  UINTN  Count;

  if ((Text == NULL) || (Bytes == NULL)) {
    return 0;
  }

  Len   = AsciiStrLen (Text);
  Hi    = -1;
  Count = 0;
  for (Index = 0; (Index <= Len) && (Count < MaxBytes); Index++) {
    Digit = (Index < Len) ? HexDigit (Text[Index]) : -1;
    if (Digit >= 0) {
      if (Hi < 0) {
        Hi = Digit;
      } else {
        Bytes[Count++] = (UINT8)((Hi << 4) | Digit);
        Hi             = -1;
      }
    } else if (Hi >= 0) {
      // a single digit followed by a separator
      Bytes[Count++] = (UINT8)Hi;
      Hi             = -1;
    }
  }

  return Count;
}

UINTN
RwUartSendHex (
  IN OUT RW_UART      *Uart,
  IN     CONST CHAR8  *Text
  )
{
  UINT8  *Bytes;
  UINTN  Count;
  UINTN  Sent;

  if (Text == NULL) {
    return 0;
  }

  Count = AsciiStrLen (Text) / 2 + 1;
  Bytes = AllocatePool (Count);
  if (Bytes == NULL) {
    return 0;
  }

  Count = RwUartParseHex (Text, Bytes, Count);
  Sent  = RwUartSendBuffer (Uart, Bytes, Count);
  FreePool (Bytes);
  return Sent;
}

UINTN
RwUartPoll (
  IN OUT RW_UART  *Uart
  )
{
  UINT8  Lsr;
  UINTN  Got;

  if ((Uart == NULL) || (Uart->Base == 0)) {
    return 0;
  }

  Got = 0;
  while (Got < POLL_MAX) {
    Lsr = UartIn (Uart->Base, RW_UART_LSR);
    if (Lsr & RW_UART_LSR_OE) {
      Uart->Overrun++;
    }

    if (Lsr & RW_UART_LSR_PE) {
      Uart->Parity++;
    }

    if (Lsr & RW_UART_LSR_FE) {
      Uart->Framing++;
    }

    if ((Lsr & RW_UART_LSR_DR) == 0) {
      break;
    }

    BufPush (Uart, UartIn (Uart->Base, RW_UART_RBR));
    Uart->RxCount++;
    Got++;
  }

  return Got;
}

VOID
RwUartDecodeStatus (
  IN     UINT16         Base,
  IN     CONST RW_UART  *Uart  OPTIONAL,
  IN OUT RW_INFO        *Info
  )
{
  UINT8        Lsr;
  UINT8        Msr;
  RW_INFO_ROW  *Row;

  if (Info == NULL) {
    return;
  }

  Lsr = UartIn (Base, RW_UART_LSR);
  Msr = UartIn (Base, RW_UART_MSR);
  RwInfoSection (Info, 0, "Port %03Xh", Base);
  if (Uart != NULL) {
    RwInfoField (Info, 1, RwEmStrong, "Line", "%d 8N1", Uart->Baud);
  }

  Row = RwInfoField (
          Info,
          1,
          (Lsr & (RW_UART_LSR_OE | RW_UART_LSR_PE | RW_UART_LSR_FE)) ? RwEmAlert : RwEmNormal,
          "LSR",
          "%02X",
          Lsr
          );
  RwInfoSetReg (Row, RW_UART_LSR, 1);
  Row = RwInfoField (
          Info,
          1,
          RwEmNormal,
          "MSR",
          "%02X %a%a%a%a",
          Msr,
          (Msr & BIT4) ? "CTS " : "",
          (Msr & BIT5) ? "DSR " : "",
          (Msr & BIT6) ? "RI " : "",
          (Msr & BIT7) ? "DCD" : ""
          );
  RwInfoSetReg (Row, RW_UART_MSR, 1);
  if (Uart != NULL) {
    RwInfoField (Info, 1, RwEmNormal, "TX", "%Lu", (UINT64)Uart->TxCount);
    RwInfoField (Info, 1, RwEmNormal, "RX", "%Lu", (UINT64)Uart->RxCount);
    RwInfoField (Info, 1, Uart->Overrun ? RwEmAlert : RwEmNormal, "Overrun", "%Lu", (UINT64)Uart->Overrun);
    RwInfoField (Info, 1, Uart->Framing ? RwEmAlert : RwEmNormal, "Framing", "%Lu", (UINT64)Uart->Framing);
    RwInfoField (Info, 1, Uart->Parity ? RwEmAlert : RwEmNormal, "Parity", "%Lu", (UINT64)Uart->Parity);
  }
}

VOID
RwUartLoopbackTest (
  IN  UINT16            Base,
  OUT RW_UART_LOOPBACK  *Result
  )
{
  STATIC CONST CHAR8  Pattern[] = "uefirrw loopback 0123456789 \x00\x55\xAA\xFF";
  UINT8               Mcr;
  UINTN               Index;
  UINTN               Wait;
  UINTN               Bad;

  ZeroMem (Result, sizeof (*Result));
  Mcr = UartIn (Base, RW_UART_MCR);
  UartOut (Base, RW_UART_MCR, 0x10);                   // loopback, all outputs off
  UartOut (Base, RW_UART_FCR, 0x07);
  for (Wait = 0; (UartIn (Base, RW_UART_LSR) & RW_UART_LSR_DR) && (Wait < POLL_MAX); Wait++) {
    UartIn (Base, RW_UART_RBR);
  }

  Bad = 0;
  for (Index = 0; Index < sizeof (Pattern) - 1; Index++) {
    for (Wait = 0; ((UartIn (Base, RW_UART_LSR) & RW_UART_LSR_THRE) == 0) && (Wait < TX_WAIT_US); Wait++) {
      gBS->Stall (1);
    }

    UartOut (Base, RW_UART_THR, (UINT8)Pattern[Index]);
    for (Wait = 0; ((UartIn (Base, RW_UART_LSR) & RW_UART_LSR_DR) == 0) && (Wait < TX_WAIT_US); Wait++) {
      gBS->Stall (1);
    }

    if (((UartIn (Base, RW_UART_LSR) & RW_UART_LSR_DR) == 0) || (UartIn (Base, RW_UART_RBR) != (UINT8)Pattern[Index])) {
      Bad++;
    }
  }

  //
  // In loopback DTR->DSR, RTS->CTS, OUT1->RI, OUT2->DCD.
  //
  Result->Msr0 = UartIn (Base, RW_UART_MSR) & 0xF0;
  UartOut (Base, RW_UART_MCR, 0x1F);
  gBS->Stall (100);
  Result->Msr1 = UartIn (Base, RW_UART_MSR) & 0xF0;
  UartOut (Base, RW_UART_MCR, Mcr);
  UartIn (Base, RW_UART_MSR);                          // clear delta bits

  Result->BytesTotal = sizeof (Pattern) - 1;
  Result->BytesOk    = Result->BytesTotal - Bad;
  Result->DataOk     = (BOOLEAN)(Bad == 0);
  Result->ModemOk    = (BOOLEAN)((Result->Msr0 == 0) && (Result->Msr1 == 0xF0));
  Result->Pass       = (BOOLEAN)(Result->DataOk && Result->ModemOk);
  AsciiSPrint (
    Result->Summary,
    sizeof (Result->Summary),
    "Loopback %a: data %d/%d bytes OK, modem lines %a (MSR %02X -> %02X)",
    Result->Pass ? "PASS" : "FAIL",
    (UINT32)Result->BytesOk,
    (UINT32)Result->BytesTotal,
    Result->ModemOk ? "OK" : "FAIL",
    Result->Msr0,
    Result->Msr1
    );
}
