/** @file
  Write log: the newest RW_WLOG_MAX writes, in memory only.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC RW_WLOG_ENTRY  mLog[RW_WLOG_MAX];
STATIC UINTN          mFirst;      // ring index of the oldest entry
STATIC UINTN          mCount;
STATIC UINT64         mTotal;

VOID
RwWlogAdd (
  IN CONST CHAR8  *Space,
  IN UINT64       Address,
  IN UINTN        Width,
  IN BOOLEAN      OldValid,
  IN UINT64       Old,
  IN UINT64       New,
  IN EFI_STATUS   Status
  )
{
  RW_WLOG_ENTRY  *Entry;

  if (mCount < RW_WLOG_MAX) {
    Entry = &mLog[(mFirst + mCount) % RW_WLOG_MAX];
    mCount++;
  } else {
    Entry  = &mLog[mFirst];
    mFirst = (mFirst + 1) % RW_WLOG_MAX;
  }

  ZeroMem (Entry, sizeof (*Entry));
  if ((gRT != NULL) && !EFI_ERROR (gRT->GetTime (&Entry->Time, NULL))) {
    Entry->TimeValid = TRUE;
  }

  AsciiStrnCpyS (Entry->Space, sizeof (Entry->Space), (Space != NULL) ? Space : "?", sizeof (Entry->Space) - 1);
  Entry->Address  = Address;
  Entry->Width    = (UINT8)Width;
  Entry->OldValid = OldValid;
  Entry->Old      = OldValid ? Old : 0;
  Entry->New      = New;
  Entry->Status   = Status;
  Entry->Serial   = ++mTotal;
}

UINTN
RwWlogCount (
  VOID
  )
{
  return mCount;
}

CONST RW_WLOG_ENTRY *
RwWlogGet (
  IN UINTN  Index
  )
{
  if (Index >= mCount) {
    return NULL;
  }

  return &mLog[(mFirst + Index) % RW_WLOG_MAX];
}

UINT64
RwWlogTotal (
  VOID
  )
{
  return mTotal;
}

VOID
RwWlogClear (
  VOID
  )
{
  mFirst = 0;
  mCount = 0;
}

STATIC
VOID
FormatValue (
  IN  UINT64  Value,
  IN  UINTN   Width,
  OUT CHAR8   *Buffer,
  IN  UINTN   Size
  )
{
  if ((Width < 1) || (Width > 8)) {
    Width = 8;
  }

  AsciiSPrint (Buffer, Size, "%0*lX", (UINTN)(Width * 2), Value);
}

VOID
RwWlogFormat (
  IN  CONST RW_WLOG_ENTRY  *Entry,
  OUT CHAR8                *Time,
  IN  UINTN                TimeSize,
  OUT CHAR8                *Address,
  IN  UINTN                AddressSize,
  OUT CHAR8                *Old,
  IN  UINTN                OldSize,
  OUT CHAR8                *New,
  IN  UINTN                NewSize,
  OUT CHAR8                *Status,
  IN  UINTN                StatusSize
  )
{
  if (Entry->TimeValid) {
    AsciiSPrint (Time, TimeSize, "%02d:%02d:%02d", Entry->Time.Hour, Entry->Time.Minute, Entry->Time.Second);
  } else {
    AsciiStrCpyS (Time, TimeSize, "--:--:--");
  }

  if (Entry->Address > MAX_UINT32) {
    AsciiSPrint (Address, AddressSize, "%016lX", Entry->Address);
  } else {
    AsciiSPrint (Address, AddressSize, "%08lX", Entry->Address);
  }

  if (Entry->OldValid) {
    FormatValue (Entry->Old, Entry->Width, Old, OldSize);
  } else {
    AsciiStrCpyS (Old, OldSize, "--");
  }

  FormatValue (Entry->New, Entry->Width, New, NewSize);
  if (EFI_ERROR (Entry->Status)) {
    AsciiSPrint (Status, StatusSize, "%r", Entry->Status);
  } else {
    AsciiStrCpyS (Status, StatusSize, "OK");
  }
}

VOID
RwWlogToText (
  IN OUT RW_STR   *Out,
  IN     BOOLEAN  Crlf
  )
{
  UINTN                Index;
  CONST RW_WLOG_ENTRY  *Entry;
  CHAR8                Time[16];
  CHAR8                Address[20];
  CHAR8                Old[20];
  CHAR8                New[20];
  CHAR8                Status[40];
  CONST CHAR8          *Eol;

  Eol = Crlf ? "\r\n" : "\n";
  RwStrAppendF (Out, "Write log: %d entries (%ld writes since start)%a", mCount, mTotal, Eol);
  RwStrAppendF (Out, "%-6a %-8a  %-40a %-16a %-5a %-16a %-16a %a%a", "#", "Time", "Space", "Address", "Width", "Old", "New", "Status", Eol);
  for (Index = 0; Index < mCount; Index++) {
    Entry = RwWlogGet (Index);
    RwWlogFormat (Entry, Time, sizeof (Time), Address, sizeof (Address), Old, sizeof (Old), New, sizeof (New), Status, sizeof (Status));
    RwStrAppendF (
      Out,
      "%-6ld %-8a  %-40a %-16a %-5d %-16a %-16a %a%a",
      Entry->Serial,
      Time,
      Entry->Space,
      Address,
      (UINTN)Entry->Width,
      Old,
      New,
      Status,
      Eol
      );
  }
}
