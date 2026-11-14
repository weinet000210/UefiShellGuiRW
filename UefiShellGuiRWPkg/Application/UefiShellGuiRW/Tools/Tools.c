/** @file
  Phase 6 tools: screenshot, dump all, save window, hex grid snapshots.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Tools.h"
#include <Library/UefiGuiDisplayLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define HEX_SAVE_MAX  SIZE_1MB

STATIC CHAR8  mLastSaved[32];

CONST CHAR8 *
RwToolLastSaved (
  VOID
  )
{
  return mLastSaved;
}

STATIC
VOID
Report (
  IN CONST CHAR8  *What,
  IN CONST CHAR8  *Name,
  IN UINTN        Size,
  IN EFI_STATUS   Status
  )
{
  CHAR8  Text[128];

  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "%a: could not save (%r)", What, Status);
    RwDialogMessage (What, Text, NULL, NULL);
  } else {
    AsciiStrCpyS (mLastSaved, sizeof (mLastSaved), Name);
    if (Size >= SIZE_1KB) {
      AsciiSPrint (Text, sizeof (Text), "%a: saved %a (%d KB) next to %a.efi", What, Name, Size / SIZE_1KB, RW_APP_NAME);
    } else {
      AsciiSPrint (Text, sizeof (Text), "%a: saved %a (%d bytes) next to %a.efi", What, Name, Size, RW_APP_NAME);
    }
  }

  RwStatusSetText (Text);
  RwSerialLog ("%a\r\n", Text);
}

//
// ---- Screenshot ----------------------------------------------------------------
//

EFI_STATUS
RwToolScreenshot (
  VOID
  )
{
  UEFI_GUI_DISPLAY_INFO  Info;
  UINT32                 *Pixels;
  UINT8                  *Bmp = NULL;
  UINTN                  Size;
  CHAR8                  Name[16];
  EFI_STATUS             Status;

  Name[0] = '\0';
  Size    = 0;
  Status  = UefiGuiGetDisplayInfo (&Info);
  if (!EFI_ERROR (Status)) {
    Pixels = AllocatePool ((UINTN)Info.HorizontalResolution * Info.VerticalResolution * sizeof (UINT32));
    if (Pixels == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
    } else {
      Status = UefiGuiDisplayReadRect (Pixels, 0, 0, Info.HorizontalResolution, Info.VerticalResolution);
      if (!EFI_ERROR (Status)) {
        Status = RwBmpEncode (Pixels, Info.HorizontalResolution, Info.VerticalResolution, &Bmp, &Size);
      }

      FreePool (Pixels);
      if (!EFI_ERROR (Status)) {
        Status = RwSaveNext ("SCR", "BMP", Bmp, Size, Name, sizeof (Name));
        FreePool (Bmp);
      }
    }
  }

  Report ("Screenshot", Name, Size, Status);
  return Status;
}

//
// ---- Dump all ------------------------------------------------------------------
//

EFI_STATUS
RwToolDumpAll (
  VOID
  )
{
  RW_STR      Text;
  EFI_TIME    Time;
  CHAR8       Name[16];
  EFI_STATUS  Status;

  Name[0] = '\0';
  RwStatusSetText ("Dumping PCI, ACPI, SMBIOS, USB, Super I/O, SPD, storage, EDID and MSRs...");
  UefiGuiProcessEvents ();
  RwStrInit (&Text);
  RwStrAppendF (&Text, "%a %a full dump\r\n", RW_APP_NAME, RW_APP_VERSION);
  if (!EFI_ERROR (gRT->GetTime (&Time, NULL))) {
    RwStrAppendF (
      &Text,
      "Taken %04d-%02d-%02d %02d:%02d:%02d\r\n",
      Time.Year,
      Time.Month,
      Time.Day,
      Time.Hour,
      Time.Minute,
      Time.Second
      );
  }

  RwDumpAll (RW_DUMP_ALL, TRUE, &Text);
  Status = Text.Oom ? EFI_OUT_OF_RESOURCES : RwSaveNext ("DUMP", "TXT", Text.Buf, Text.Len, Name, sizeof (Name));
  Report ("Dump All", Name, Text.Len, Status);
  RwStrFree (&Text);
  return Status;
}

EFI_STATUS
RwToolSaveText (
  IN CONST CHAR8  *Prefix,
  IN CONST CHAR8  *Text,
  IN UINTN        Length
  )
{
  CHAR8       Name[16];
  EFI_STATUS  Status;

  Name[0] = '\0';
  Status  = RwSaveNext (Prefix, "TXT", Text, Length, Name, sizeof (Name));
  Report ("Save Window", Name, Length, Status);
  return Status;
}

//
// ---- Save window -----------------------------------------------------------------
//

STATIC struct {
  RW_HEXGRID    *Grid;
  UINT64        Start;
} mHexSave;

STATIC
VOID
HexLengthDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_DATA_SOURCE  *Src;
  UINT8           *Data;
  UINT8           *Bits;
  BOOLEAN         *Valid;
  UINTN           Size;
  UINTN           Index;
  RW_STR          Text;
  CHAR8           Base[16];
  CHAR8           Name[20];
  EFI_STATUS      Status;

  if (!Accepted || (mHexSave.Grid == NULL)) {
    return;
  }

  Src = RwHexGridGetSource (mHexSave.Grid);
  if ((Src == NULL) || (Value == 0)) {
    return;
  }

  //
  // Clip to the source and to 1 MB.
  //
  Size = (UINTN)MIN (Value, HEX_SAVE_MAX);
  if ((mHexSave.Start < Src->Base) || (mHexSave.Start > Src->Limit)) {
    RwDialogMessage ("Save Window", "The start address is outside this space.", NULL, NULL);
    return;
  }

  if (Size - 1 > Src->Limit - mHexSave.Start) {
    Size = (UINTN)(Src->Limit - mHexSave.Start + 1);
  }

  Data  = AllocateZeroPool (Size);
  Bits  = AllocateZeroPool ((Size + 7) / 8);
  Valid = AllocateZeroPool (Size * sizeof (BOOLEAN));
  RwStrInit (&Text);
  Status = EFI_OUT_OF_RESOURCES;
  Name[0] = '\0';
  if ((Data != NULL) && (Bits != NULL) && (Valid != NULL)) {
    RwStatusSetText ("Reading...");
    UefiGuiProcessEvents ();
    RwDsReadBlock (Src, mHexSave.Start, Size, Data, Bits);
    for (Index = 0; Index < Size; Index++) {
      Valid[Index] = (BOOLEAN)((Bits[Index / 8] & (1 << (Index % 8))) != 0);
    }

    RwStrAppendF (&Text, "%a\r\n%0*lX-%0*lX (%d bytes)\r\n\r\n", Src->Name, (UINTN)MAX (Src->AddrDigits, 4), mHexSave.Start,
      (UINTN)MAX (Src->AddrDigits, 4), mHexSave.Start + Size - 1, Size);
    RwHexText (Data, Valid, Size, mHexSave.Start, MAX (Src->AddrDigits, 4), TRUE, &Text);
    Status = Text.Oom ? EFI_OUT_OF_RESOURCES : RwSaveFindFree ("HEX", "TXT", "BIN", Base, sizeof (Base));
    if (!EFI_ERROR (Status)) {
      AsciiSPrint (Name, sizeof (Name), "%a.BIN", Base);
      Status = RwSaveWrite (Name, Data, Size);
    }

    if (!EFI_ERROR (Status)) {
      AsciiSPrint (Name, sizeof (Name), "%a.TXT", Base);
      Status = RwSaveWrite (Name, Text.Buf, Text.Len);
    }
  }

  Report ("Save Window", Name, Size, Status);
  RwStrFree (&Text);
  if (Data != NULL) {
    FreePool (Data);
  }

  if (Bits != NULL) {
    FreePool (Bits);
  }

  if (Valid != NULL) {
    FreePool (Valid);
  }
}

STATIC
VOID
HexStartDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (!Accepted) {
    return;
  }

  mHexSave.Start = Value;
  RwDialogInputHex ("Save Window", "Length in bytes (hex, up to 100000):", RW_HEXGRID_PAGE, 6, HexLengthDone, NULL);
}

/**
  The hex grid of a window: the focused widget when it is one, else the
  first visible one.
**/
STATIC
RW_HEXGRID *
WindowGrid (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_WIDGET  *Focus;
  UINTN      Index;

  Focus = RwFocusGet (Window);
  if ((Focus != NULL) && (Focus->Kind != NULL) && (AsciiStrCmp (Focus->Kind, "HexGrid") == 0)) {
    return (RW_HEXGRID *)Focus;
  }

  for (Index = 0; Index < Window->FocusCount; Index++) {
    Focus = Window->Focus[Index];
    if ((Focus->Kind != NULL) && (AsciiStrCmp (Focus->Kind, "HexGrid") == 0) && !RwWidgetIsHidden (Focus)) {
      return (RW_HEXGRID *)Focus;
    }
  }

  return NULL;
}

VOID
RwToolSaveWindow (
  IN RW_MDI_WINDOW  *Window
  )
{
  RW_HEXGRID  *Grid;
  RW_STR      Text;
  UINTN       Index;
  UINTN       Parts;
  RW_WIDGET   *Widget;
  EFI_STATUS  Status;

  if (Window == NULL) {
    return;
  }

  if (Window->OnSave != NULL) {
    Window->OnSave (Window);
    return;
  }

  Grid = WindowGrid (Window);
  if ((Grid != NULL) && (RwHexGridGetSource (Grid) != NULL)) {
    mHexSave.Grid = Grid;
    RwDialogInputHex (
      "Save Window",
      "Start address (hex):",
      RwHexGridGetPage (Grid),
      MIN (MAX (RwHexGridGetSource (Grid)->AddrDigits, 4), 16),
      HexStartDone,
      NULL
      );
    return;
  }

  //
  // Every visible list of the window, in focus order.
  //
  RwStrInit (&Text);
  RwStrAppendF (&Text, "%a\r\n\r\n", lv_label_get_text (Window->Title));
  Parts = 0;
  for (Index = 0; Index < Window->FocusCount; Index++) {
    Widget = Window->Focus[Index];
    if (RwWidgetIsHidden (Widget)) {
      continue;
    }

    if (Parts > 0) {
      RwStrAppend (&Text, "\r\n");
    }

    if (RwWidgetToText (Widget, &Text, TRUE)) {
      Parts++;
    }
  }

  if (Parts == 0) {
    RwStatusSetText ("Save Window: nothing to save in this window");
  } else {
    Status = Text.Oom ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
    if (EFI_ERROR (Status)) {
      Report ("Save Window", "", 0, Status);
    } else {
      RwToolSaveText ((Window->SavePrefix != NULL) ? Window->SavePrefix : "SAVE", Text.Buf, Text.Len);
    }
  }

  RwStrFree (&Text);
}

//
// ---- Snapshots -----------------------------------------------------------------
//

VOID
RwToolSnapshotToggle (
  IN RW_HEXGRID  *Grid
  )
{
  UINT8           Data[RW_HEXGRID_PAGE];
  BOOLEAN         Valid[RW_HEXGRID_PAGE];
  RW_DATA_SOURCE  *Src;
  UINT8           *File;
  UINTN           FileSize;
  CHAR8           Name[16];
  CHAR8           Text[128];
  EFI_STATUS      Status;

  if (RwHexGridHasSnapshot (Grid, NULL)) {
    RwHexGridClearSnapshot (Grid);
    RwStatusSetText ("Snapshot cleared");
    return;
  }

  Src = RwHexGridGetSource (Grid);
  if (Src == NULL) {
    return;
  }

  RwHexGridGetData (Grid, Data, Valid);
  RwHexGridSetSnapshot (Grid, RwHexGridGetPage (Grid), Data, Valid);

  //
  // Keep a copy on disk for comparing later (Ctrl+L); the snapshot works
  // without it when no volume is writable.
  //
  Name[0] = '\0';
  Status  = RwSnapEncode (Src->Name, RwHexGridGetPage (Grid), Data, Valid, RW_HEXGRID_PAGE, &File, &FileSize);
  if (!EFI_ERROR (Status)) {
    Status = RwSaveNext ("SNAP", "BIN", File, FileSize, Name, sizeof (Name));
    FreePool (File);
  }

  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "Snapshot taken (not saved: %r); changes show in red, Ctrl+N clears", Status);
  } else {
    AsciiStrCpyS (mLastSaved, sizeof (mLastSaved), Name);
    AsciiSPrint (Text, sizeof (Text), "Snapshot taken, saved as %a; changes show in red, Ctrl+N clears", Name);
  }

  RwStatusSetText (Text);
}

STATIC
VOID
SnapLoadDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  RW_HEXGRID      *Grid;
  RW_DATA_SOURCE  *Src;
  UINT8           *File;
  UINTN           FileSize;
  UINT8           Data[RW_HEXGRID_PAGE];
  BOOLEAN         Valid[RW_HEXGRID_PAGE];
  CHAR8           SnapName[RW_SNAP_NAME_MAX];
  CHAR8           Name[16];
  CHAR8           Text[200];
  UINT64          Page;
  EFI_STATUS      Status;
  UINTN           Index;

  Grid = (RW_HEXGRID *)Context;
  if (!Accepted || (Grid == NULL)) {
    return;
  }

  //
  // The entry field takes hex digits; read them as the decimal number.
  //
  AsciiSPrint (Name, sizeof (Name), "%lX", Value);
  for (Index = 0; Name[Index] != '\0'; Index++) {
    if ((Name[Index] < '0') || (Name[Index] > '9')) {
      RwDialogMessage ("Load Snapshot", "Type the decimal number of the snapshot (SNAP0001.BIN = 1).", NULL, NULL);
      return;
    }
  }

  AsciiSPrint (Name, sizeof (Name), "SNAP%04d.BIN", AsciiStrDecimalToUintn (Name));
  Status = RwSaveRead (Name, SIZE_4KB, &File, &FileSize);
  if (!EFI_ERROR (Status)) {
    Status = RwSnapDecode (File, FileSize, RW_HEXGRID_PAGE, SnapName, &Page, Data, Valid);
    FreePool (File);
  }

  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text), "Cannot load %a: %r", Name, Status);
    RwDialogMessage ("Load Snapshot", Text, NULL, NULL);
    return;
  }

  Src = RwHexGridGetSource (Grid);
  RwHexGridGoTo (Grid, Page);
  RwHexGridSetSnapshot (Grid, Page, Data, Valid);
  RwHexGridRefresh (Grid);
  if ((Src != NULL) && (AsciiStrCmp (Src->Name, SnapName) != 0)) {
    AsciiSPrint (Text, sizeof (Text), "Comparing with %a, taken of %a", Name, SnapName);
  } else {
    AsciiSPrint (Text, sizeof (Text), "Comparing with %a; changes show in red, Ctrl+N clears", Name);
  }

  RwStatusSetText (Text);
}

VOID
RwToolSnapshotLoad (
  IN RW_HEXGRID  *Grid
  )
{
  RwDialogInputHex ("Load Snapshot", "Snapshot number (SNAPnnnn.BIN, decimal digits):", 1, 4, SnapLoadDone, Grid);
}
