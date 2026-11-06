/** @file
  RwCoreLib UART module: polled 16550 access on 3F8/2F8/3E8/2E8 or any I/O
  base. Detection, the firmware's Serial I/O instance on a port, saving /
  setting / restoring line settings, sending, polling the receive FIFO into
  a byte stream buffer, status decode and an internal loopback self-test.

  A session (RW_UART) owns the port between RwUartOpen and RwUartClose: it
  can pause the firmware's console driver on the port, saves the line
  settings, sets the baud rate 8N1, and restores everything on close.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_RWUART_H_
#define RW_RWUART_H_

#include <Library/RwCore/RwBase.h>
#include <Protocol/DevicePath.h>

//
// 16550 registers (offsets from the I/O base).
//
#define RW_UART_RBR  0
#define RW_UART_THR  0
#define RW_UART_DLL  0
#define RW_UART_IER  1
#define RW_UART_DLM  1
#define RW_UART_IIR  2
#define RW_UART_FCR  2
#define RW_UART_LCR  3
#define RW_UART_MCR  4
#define RW_UART_LSR  5
#define RW_UART_MSR  6
#define RW_UART_SCR  7

#define RW_UART_LSR_DR    BIT0
#define RW_UART_LSR_OE    BIT1
#define RW_UART_LSR_PE    BIT2
#define RW_UART_LSR_FE    BIT3
#define RW_UART_LSR_THRE  BIT5

#define RW_UART_CLOCK          115200   // divisor base
#define RW_UART_STD_COUNT      4        // COM1..COM4
#define RW_UART_BAUD_COUNT     5        // 9600 .. 115200
#define RW_UART_DEFAULT_BAUD   115200

//
// Stream buffer entries: low byte = data, RW_UART_DIR_TX set for bytes sent.
// The buffer holds RW_UART_BUF_MAX entries; when full, the older half is
// dropped.
//
#define RW_UART_BUF_MAX  65536
#define RW_UART_DIR_TX   0x100

//
// Line ending sent by RwUartSendNewline.
//
typedef enum {
  RwUartNewlineCr   = 0,
  RwUartNewlineCrLf = 1,
  RwUartNewlineLf   = 2
} RW_UART_NEWLINE;

//
// The firmware's Serial I/O console on the port while a session runs.
//
typedef enum {
  RwUartFwNone        = 0,    // the firmware has no Serial I/O instance on this port
  RwUartFwPaused      = 1,    // disconnected; reconnected by RwUartClose
  RwUartFwPauseFailed = 2     // could not be paused: received bytes may be taken by it
} RW_UART_FW_STATE;

//
// Saved line settings.
//
typedef struct {
  UINT8      Dll;
  UINT8      Dlm;
  UINT8      Lcr;
  UINT8      Ier;
  UINT8      Mcr;
  BOOLEAN    Fifo;
} RW_UART_SAVE;

//
// A session on one port. Caller-owned; RwUartOpen fills it, RwUartClose
// restores the port and frees it.
//
typedef struct {
  UINT16          Base;
  UINT32          Baud;
  RW_UART_SAVE    Saved;
  EFI_HANDLE      FwSerial;     // firmware Serial I/O handle, paused, or NULL
  UINT8           FwState;      // RW_UART_FW_STATE
  UINT16          *Buf;         // stream: RW_UART_BUF_MAX entries
  UINTN           Len;
  UINTN           TxCount;
  UINTN           RxCount;
  UINTN           Overrun;
  UINTN           Framing;
  UINTN           Parity;
} RW_UART;

//
// Loopback self-test result.
//
typedef struct {
  UINTN      BytesOk;
  UINTN      BytesTotal;
  UINT8      Msr0;          // MSR & F0h with all outputs off (expect 00h)
  UINT8      Msr1;          // MSR & F0h with all outputs on (expect F0h)
  BOOLEAN    DataOk;
  BOOLEAN    ModemOk;
  BOOLEAN    Pass;
  CHAR8      Summary[120];  // "Loopback PASS: data 32/32 bytes OK, modem lines OK (MSR 00 -> F0)"
} RW_UART_LOOPBACK;

/**
  I/O base of COM1..COM4 (Index 0..3): 3F8h, 2F8h, 3E8h, 2E8h; 0 out of range.
**/
UINT16
RwUartStdBase (
  IN UINTN  Index
  );

/**
  Selectable baud rates (Index 0..4): 9600, 19200, 38400, 57600, 115200;
  0 out of range.
**/
UINT32
RwUartStdBaud (
  IN UINTN  Index
  );

/**
  A 16550 keeps whatever is written to its scratch register; an empty I/O
  decode reads back FFh. The scratch register is restored. Line settings
  are not touched.
**/
BOOLEAN
RwUartPresent (
  IN UINT16  Base
  );

/**
  I/O base of a Serial I/O instance from the EFI_SIO_PROTOCOL resources of
  the Super I/O device it sits on (I/O port or fixed I/O descriptor). 0 when
  the firmware does not say.
**/
UINT16
RwUartSerialIoBase (
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  );

/**
  The firmware's Serial I/O handle for the port at Base, or NULL. Its base
  comes from the Super I/O resources when published; otherwise an ACPI
  PNP0501 UART whose UID matches the usual COMn order (UID 0 = 3F8h ...)
  is taken to be it.
**/
EFI_HANDLE
RwUartFirmwareHandle (
  IN UINT16  Base
  );

/**
  Save IER, LCR, MCR, FIFO state and the divisor latch (briefly sets DLAB).
**/
VOID
RwUartSave (
  IN  UINT16        Base,
  OUT RW_UART_SAVE  *Save
  );

/**
  Restore what RwUartSave saved.
**/
VOID
RwUartRestore (
  IN UINT16              Base,
  IN CONST RW_UART_SAVE  *Save
  );

/**
  Program Baud 8N1 (divisor RW_UART_CLOCK / Baud), interrupts off, FIFO on
  and cleared, DTR + RTS. EFI_INVALID_PARAMETER when Baud is 0 or above
  RW_UART_CLOCK; nothing is written then.
**/
EFI_STATUS
RwUartSetup (
  IN UINT16  Base,
  IN UINT32  Baud
  );

/**
  Wait for the transmitter (up to ~20 ms) and send one byte. FALSE on
  timeout (no clock, or hardware flow control).
**/
BOOLEAN
RwUartSendByteRaw (
  IN UINT16  Base,
  IN UINT8   Byte
  );

/**
  Start a session: allocate the stream buffer, pause the firmware console
  on the port when PauseFirmware (see FwState), save the line settings and
  set Baud 8N1. Close it with RwUartClose.
**/
EFI_STATUS
RwUartOpen (
  OUT RW_UART  *Uart,
  IN  UINT16   Base,
  IN  UINT32   Baud,
  IN  BOOLEAN  PauseFirmware
  );

/**
  Restore the saved line settings, reconnect a paused firmware console and
  free the buffer.
**/
VOID
RwUartClose (
  IN OUT RW_UART  *Uart
  );

/**
  Change the baud rate of an open session (8N1).
**/
EFI_STATUS
RwUartSetBaud (
  IN OUT RW_UART  *Uart,
  IN     UINT32   Baud
  );

/**
  Send one byte and record it in the stream as TX. FALSE when the
  transmitter did not empty in ~20 ms (the byte is not recorded).
**/
BOOLEAN
RwUartSend (
  IN OUT RW_UART  *Uart,
  IN     UINT8    Byte
  );

/**
  Send Count bytes. Returns how many went out (stops at the first timeout).
**/
UINTN
RwUartSendBuffer (
  IN OUT RW_UART      *Uart,
  IN     CONST UINT8  *Data,
  IN     UINTN        Count
  );

/**
  Send the line ending Mode (RW_UART_NEWLINE).
**/
VOID
RwUartSendNewline (
  IN OUT RW_UART  *Uart,
  IN     UINT8    Mode
  );

/**
  Parse hex byte text: pairs of hex digits form bytes; a single digit
  followed by a separator (anything not a hex digit, or the end) is a byte
  on its own. "55 aa 1 0203" -> 55 AA 01 02 03.

  @return number of bytes stored (at most MaxBytes).
**/
UINTN
RwUartParseHex (
  IN  CONST CHAR8  *Text,
  OUT UINT8        *Bytes,
  IN  UINTN        MaxBytes
  );

/**
  Parse Text with RwUartParseHex and send the bytes. Returns how many were
  sent (uefirrw counted attempted bytes; this counts bytes that went out).
**/
UINTN
RwUartSendHex (
  IN OUT RW_UART      *Uart,
  IN     CONST CHAR8  *Text
  );

/**
  Drain the receive FIFO into the stream (at most 4096 bytes a call),
  counting overrun, parity and framing errors.

  @return number of bytes received.
**/
UINTN
RwUartPoll (
  IN OUT RW_UART  *Uart
  );

/**
  Clear the stream and every counter.
**/
VOID
RwUartClear (
  IN OUT RW_UART  *Uart
  );

/**
  Line status of a port: LSR, MSR with the modem input lines (CTS DSR RI
  DCD). Uart (optional) adds its baud rate and TX / RX / error counters.
  Reads LSR and MSR only (clears their latched error / delta bits).
**/
VOID
RwUartDecodeStatus (
  IN     UINT16         Base,
  IN     CONST RW_UART  *Uart  OPTIONAL,
  IN OUT RW_INFO        *Info
  );

/**
  Internal loopback self-test (needs no cable): MCR loopback, FIFO cleared,
  a test pattern sent and read back, then the modem lines (DTR->DSR,
  RTS->CTS, OUT1->RI, OUT2->DCD) checked. MCR is restored afterwards; the
  divisor and LCR are left as they are, so call it on an open session's
  port (RwUartOpen). It sends nothing on the wire.
**/
VOID
RwUartLoopbackTest (
  IN  UINT16            Base,
  OUT RW_UART_LOOPBACK  *Result
  );

#endif
