/** @file
  Write Log window: every write made in this session (newest last), kept in
  memory by RwCoreLib. F5 refreshes, Del clears, Ctrl+S saves WLOGnnnn.TXT.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"

#define REFRESH_MS  500

STATIC RW_WLOG_VIEW  mWlog;

STATIC
VOID
Fill (
  VOID
  )
{
  UINTN                Index;
  UINTN                Count;
  CONST RW_WLOG_ENTRY  *Entry;
  CHAR8                Number[24];
  CHAR8                Time[16];
  CHAR8                Address[20];
  CHAR8                Width[8];
  CHAR8                Old[20];
  CHAR8                New[20];
  CHAR8                Status[40];
  CHAR8                Heading[128];
  CONST CHAR8          *Cells[8];

  RwTableClear (mWlog.Table);
  Count = RwWlogCount ();
  for (Index = 0; Index < Count; Index++) {
    Entry = RwWlogGet (Index);
    RwWlogFormat (Entry, Time, sizeof (Time), Address, sizeof (Address), Old, sizeof (Old), New, sizeof (New), Status, sizeof (Status));
    AsciiSPrint (Number, sizeof (Number), "%ld", Entry->Serial);
    AsciiSPrint (Width, sizeof (Width), "%d", (UINTN)Entry->Width);
    Cells[0] = Number;
    Cells[1] = Time;
    Cells[2] = Entry->Space;
    Cells[3] = Address;
    Cells[4] = Width;
    Cells[5] = Old;
    Cells[6] = New;
    Cells[7] = Status;
    RwTableAddRow (mWlog.Table, Cells, (INT32)Index, EFI_ERROR (Entry->Status) ? RwEmAlert : RwEmNormal);
  }

  if (Count > 0) {
    RwVListSelect (RwTableList (mWlog.Table), Count - 1);
  }

  AsciiSPrint (
    Heading,
    sizeof (Heading),
    "%d writes since start, newest %d kept in memory   (Del: clear, Ctrl+S: save)",
    RwWlogTotal (),
    (UINTN)RW_WLOG_MAX
    );
  lv_label_set_text (mWlog.Heading, Heading);
  mWlog.Shown = RwWlogTotal ();
}

STATIC
VOID
OnRefresh (
  IN RW_MDI_WINDOW  *Window
  )
{
  Fill ();
}

STATIC
VOID
OnTimer (
  IN lv_timer_t  *Timer
  )
{
  if ((mWlog.Window != NULL) && (mWlog.Shown != RwWlogTotal ())) {
    Fill ();
  }
}

STATIC
VOID
ClearDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted && (mWlog.Window != NULL)) {
    RwWlogClear ();
    Fill ();
  }
}

STATIC
BOOLEAN
ExtraKey (
  IN RW_VLIST                  *List,
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  )
{
  if (Key->ScanCode == SCAN_DELETE) {
    RwDialogConfirm ("Write Log", "Clear the write log?", 0, ClearDone, NULL);
    return TRUE;
  }

  return FALSE;
}

STATIC
VOID
OnSave (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_STR  Text;

  RwStrInit (&Text);
  RwStrAppendF (&Text, "%a %a\r\n", RW_APP_NAME, RW_APP_VERSION);
  RwWlogToText (&Text, TRUE);
  if (Text.Oom) {
    RwStatusSetText ("Save Window: out of memory");
  } else {
    RwToolSaveText ("WLOG", Text.Buf, Text.Len);
  }

  RwStrFree (&Text);
}

STATIC
VOID
OnClose (
  IN RW_MDI_WINDOW  *Window
  )
{
  if (mWlog.Timer != NULL) {
    lv_timer_delete (mWlog.Timer);
  }

  ZeroMem (&mWlog, sizeof (mWlog));
}

RW_MDI_WINDOW *
RwViewOpenWriteLog (
  VOID
  )
{
  RW_MDI_WINDOW                 *Window;
  lv_obj_t                      *Column;
  STATIC CONST RW_VLIST_COLUMN  Columns[] = {
    { "#",       50  },
    { "Time",    74  },
    { "Space",   200 },
    { "Address", 140 },
    { "W",       24  },
    { "Old",     140 },
    { "New",     140 },
    { "Status",  0   }
  };

  if (mWlog.Window != NULL) {
    RwMdiActivate (mWlog.Window);
    return mWlog.Window;
  }

  Window = RwMdiCreate ("Write Log", RwCmdWriteLog, 900, 400);
  if (Window == NULL) {
    return NULL;
  }

  ZeroMem (&mWlog, sizeof (mWlog));
  mWlog.Window      = Window;
  Window->OnClose   = OnClose;
  Window->OnRefresh = OnRefresh;
  Window->OnSave    = OnSave;
  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_ROW);
  Column            = RwViewColumn (Window->Content, 0);
  mWlog.Heading     = RwViewLabel (Column, "");
  mWlog.Table       = RwTableCreate (RwViewPane (Column, 1), Window, Columns, ARRAY_SIZE (Columns), TRUE);
  RwTableList (mWlog.Table)->ExtraKey = ExtraKey;
  Fill ();
  mWlog.Timer = lv_timer_create (OnTimer, REFRESH_MS, NULL);
  RwMdiFitContent (Window, 900, 360);
  RwFocusSet (&RwTableList (mWlog.Table)->Base);
  return Window;
}

RW_WLOG_VIEW *
RwWlogViewGet (
  VOID
  )
{
  return &mWlog;
}
