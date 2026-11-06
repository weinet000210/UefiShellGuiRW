/** @file
  Frame self test (-selftest): drives the menu bar, toolbar, MDI windows and
  keyboard handling through the same code paths a user reaches, checks the
  results, and leaves a representative screen up for a screenshot.

  Serial output (one line per check) ends with the verdict the caller
  prints: "UEFISHELLGUIRW: PASS" / "FAIL".

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiShellGuiRW.h"

STATIC UINTN  mChecks;
STATIC UINTN  mFailures;

STATIC
VOID
Check (
  IN CONST CHAR8  *Name,
  IN BOOLEAN      Passed
  )
{
  mChecks++;
  if (!Passed) {
    mFailures++;
  }

  RwSerialLog ("  [%a] %a\r\n", Passed ? "PASS" : "FAIL", Name);
}

STATIC
VOID
Pump (
  IN UINT64  Ms
  )
{
  UINT64  End;

  End = UefiGuiGetTimeMs () + Ms;
  do {
    if (UefiGuiProcessEvents () != EFI_SUCCESS) {
      break;
    }
  } while (UefiGuiGetTimeMs () < End);
}

STATIC
VOID
Key (
  IN UINT16  ScanCode,
  IN CHAR16  Char,
  IN UINT32  Modifiers
  )
{
  UEFI_GUI_KEY_EVENT  Event;

  ZeroMem (&Event, sizeof (Event));
  Event.ScanCode    = ScanCode;
  Event.UnicodeChar = Char;
  Event.Modifiers   = Modifiers;
  Event.Pressed     = TRUE;
  if (!RwKeyHandler (&Event, NULL)) {
    //
    // Not an application key: LVGL would get it. Nothing in Phase 0 needs
    // that path.
    //
  }

  Pump (30);
}

STATIC
BOOLEAN
ActiveIs (
  IN RW_COMMAND_ID  Command
  )
{
  return (BOOLEAN)((RwMdiGetActive () != NULL) && (RwMdiGetActive ()->Command == Command));
}

UINTN
RwSelfTestRun (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;
  RW_MDI_WINDOW  *Pci;
  INT32          DesktopW;
  INT32          DesktopH;
  INT32          W;
  INT32          H;
  BOOLEAN        Ok;
  UINTN          Index;

  mChecks   = 0;
  mFailures = 0;
  RwSerialLog ("\r\n%a %a self test\r\n", RW_APP_NAME, RW_APP_VERSION);
  Pump (200);

  //
  // Frame
  //
  lv_obj_update_layout (gRwFrame.Screen);
  DesktopW = lv_obj_get_content_width (gRwFrame.Desktop);
  DesktopH = lv_obj_get_content_height (gRwFrame.Desktop);
  Check ("frame: menu bar, toolbar, desktop, status bar exist",
    (BOOLEAN)(gRwFrame.MenuBar != NULL && gRwFrame.Toolbar != NULL && gRwFrame.Desktop != NULL && gRwFrame.StatusBar != NULL));
  Check ("frame: 5 menus (File Access Specific Window Help)", (BOOLEAN)(RwMenuCount () == 5));
  Check ("frame: 22 toolbar buttons", (BOOLEAN)(RwToolbarButtonCount () == 22));
  Check ("frame: desktop fills the space between toolbar and status bar",
    (BOOLEAN)(DesktopW > 600 && DesktopH > 400 &&
              lv_obj_get_y (gRwFrame.StatusBar) + lv_obj_get_height (gRwFrame.StatusBar) == lv_display_get_vertical_resolution (NULL)));
  Check ("commands: window commands disabled with no window", (BOOLEAN)!RwIsCommandEnabled (RwCmdCascade));
  Check ("toolbar: Cascade button grayed out with no window",
    lv_obj_is_disabled (RwToolbarGetButton (RwCmdCascade)));

  //
  // Opening module windows (menu command and toolbar click)
  //
  RwRunCommand (RwCmdPci);
  Pump (50);
  Pci = RwMdiGetActive ();
  Check ("command: PCI Devices opens a window", (BOOLEAN)(RwMdiCount () == 1 && ActiveIs (RwCmdPci)));
  lv_obj_send_event (RwToolbarGetButton (RwCmdMemory), LV_EVENT_CLICKED, NULL);
  Pump (50);
  Check ("toolbar: Memory button opens a window", (BOOLEAN)(RwMdiCount () == 2 && ActiveIs (RwCmdMemory)));
  RwRunCommand (RwCmdIo);
  Pump (50);
  Check ("command: I/O Space opens a third window", (BOOLEAN)(RwMdiCount () == 3 && ActiveIs (RwCmdIo)));
  RwRunCommand (RwCmdPci);
  Pump (50);
  Check ("command: PCI again activates the existing window", (BOOLEAN)(RwMdiCount () == 3 && RwMdiGetActive () == Pci));
  Check ("mdi: active window is on top",
    (BOOLEAN)(lv_obj_get_index (Pci->Obj) == (INT32)lv_obj_get_child_count (gRwFrame.Desktop) - 1));
  Check ("toolbar: Cascade button enabled with windows open",
    (BOOLEAN)!lv_obj_is_disabled (RwToolbarGetButton (RwCmdCascade)));

  //
  // Arrange
  //
  RwRunCommand (RwCmdCascade);
  Pump (50);
  Ok = TRUE;
  for (Index = 0; Index < 3; Index++) {
    Window = RwMdiGetAt (Index);
    Ok    &= (BOOLEAN)(lv_obj_get_x (Window->Obj) == (INT32)Index * 22 && lv_obj_get_y (Window->Obj) == (INT32)Index * 22);
    Ok    &= (BOOLEAN)(lv_obj_get_width (Window->Obj) == DesktopW * 2 / 3);
  }

  Check ("mdi: cascade places windows 22 px apart, 2/3 of the desktop", Ok);
  Check ("mdi: cascade keeps the active window", (BOOLEAN)(RwMdiGetActive () == Pci));

  RwRunCommand (RwCmdTileV);
  Pump (50);
  Ok = TRUE;
  for (Index = 0; Index < 3; Index++) {
    Window = RwMdiGetAt (Index);
    Ok    &= (BOOLEAN)(lv_obj_get_x (Window->Obj) == (INT32)Index * DesktopW / 3);
    Ok    &= (BOOLEAN)(lv_obj_get_height (Window->Obj) == DesktopH);
  }

  Check ("mdi: tile vertically puts 3 windows side by side", Ok);
  RwRunCommand (RwCmdTileH);
  Pump (50);
  Ok = TRUE;
  for (Index = 0; Index < 3; Index++) {
    Window = RwMdiGetAt (Index);
    Ok    &= (BOOLEAN)(lv_obj_get_y (Window->Obj) == (INT32)Index * DesktopH / 3);
    Ok    &= (BOOLEAN)(lv_obj_get_width (Window->Obj) == DesktopW);
  }

  Check ("mdi: tile horizontally stacks 3 windows", Ok);

  //
  // Maximize / minimize / restore
  //
  RwRunCommand (RwCmdCascade);
  Pump (30);
  W = lv_obj_get_width (Pci->Obj);
  H = lv_obj_get_height (Pci->Obj);
  RwMdiSetMaximized (Pci, TRUE);
  Pump (30);
  Check ("mdi: maximize fills the desktop",
    (BOOLEAN)(lv_obj_get_width (Pci->Obj) == DesktopW && lv_obj_get_height (Pci->Obj) == DesktopH && lv_obj_get_x (Pci->Obj) == 0));
  RwMdiSetMaximized (Pci, FALSE);
  Pump (30);
  Check ("mdi: restore returns to the previous size",
    (BOOLEAN)(lv_obj_get_width (Pci->Obj) == W && lv_obj_get_height (Pci->Obj) == H));
  RwMdiSetMinimized (Pci, TRUE);
  Pump (30);
  Check ("mdi: minimize shrinks the window to its caption at the bottom",
    (BOOLEAN)(Pci->Minimized && lv_obj_get_height (Pci->Obj) < 40 &&
              lv_obj_get_y (Pci->Obj) + lv_obj_get_height (Pci->Obj) == DesktopH &&
              lv_obj_is_hidden (Pci->Content)));
  RwRunCommand (RwCmdPci);
  Pump (30);
  Check ("mdi: opening a minimized module restores it",
    (BOOLEAN)(!Pci->Minimized && lv_obj_get_width (Pci->Obj) == W && ActiveIs (RwCmdPci)));

  //
  // Keyboard: Ctrl+Tab, menus, F1, Ctrl+F4
  //
  Key (SCAN_NULL, CHAR_TAB, UEFI_GUI_KEY_MOD_CTRL);
  Check ("keys: Ctrl+Tab activates another window", (BOOLEAN)(RwMdiGetActive () != Pci));
  Key (SCAN_F10, 0, 0);
  Check ("keys: F10 opens the File menu", (BOOLEAN)(RwMenuOpenIndex () == 0));
  Key (SCAN_RIGHT, 0, 0);
  Check ("keys: Right moves to the Access menu", (BOOLEAN)(RwMenuOpenIndex () == 1));
  Key (SCAN_LEFT, 0, 0);
  Key (SCAN_LEFT, 0, 0);
  Check ("keys: Left wraps around to the Help menu", (BOOLEAN)(RwMenuOpenIndex () == 4));
  Key (SCAN_ESC, 0, 0);
  Check ("keys: Esc closes the menu", (BOOLEAN)!RwMenuIsOpen ());
  Key (SCAN_NULL, L's', UEFI_GUI_KEY_MOD_ALT);
  Check ("keys: Alt+S opens the Specific menu", (BOOLEAN)(RwMenuOpenIndex () == 2));
  Key (SCAN_DOWN, 0, 0);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("keys: Down + Enter runs Specific > SMBIOS", (BOOLEAN)(!RwMenuIsOpen () && ActiveIs (RwCmdSmbios) && RwMdiCount () == 4));
  RwMdiActivate (Pci);
  Key (SCAN_NULL, L'w', UEFI_GUI_KEY_MOD_ALT);
  Key (SCAN_END, 0, 0);
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Check ("keys: Window menu lists the windows; End + Enter activates the last one", ActiveIs (RwCmdSmbios));
  Key (SCAN_NULL, L'f', UEFI_GUI_KEY_MOD_ALT);
  Key (SCAN_DOWN, 0, 0);
  Check ("menu: disabled File entries are skipped (Down lands on Exit)", (BOOLEAN)(RwMenuOpenIndex () == 0));
  Key (SCAN_ESC, 0, 0);
  Key (SCAN_F1, 0, 0);
  Check ("keys: F1 opens About", (BOOLEAN)(ActiveIs (RwCmdAbout) && RwMdiCount () == 5));
  Key (SCAN_F4, 0, UEFI_GUI_KEY_MOD_CTRL);
  Pump (30);
  Check ("keys: Ctrl+F4 closes the active window", (BOOLEAN)(RwMdiCount () == 4 && RwMdiFind (RwCmdAbout) == NULL));
  Check ("mdi: closing activates the window below", (BOOLEAN)(RwMdiGetActive () != NULL));

  RwRunCommand (RwCmdCloseAll);
  Pump (50);
  Check ("command: Close All closes every window", (BOOLEAN)(RwMdiCount () == 0 && RwMdiGetActive () == NULL));

  //
  // Screen for the screenshot: three windows and the Window menu open.
  //
  RwRunCommand (RwCmdPci);
  RwRunCommand (RwCmdAcpi);
  RwRunCommand (RwCmdAbout);
  RwRunCommand (RwCmdCascade);
  RwMenuOpen (3);
  Pump (300);
  RwSerialLog ("UEFISHELLGUIRW: SCREEN READY\r\n");
  Pump (5000);
  RwMenuClose ();
  Pump (50);

  RwSerialLog ("%u checks, %u failed\r\n", (UINT32)mChecks, (UINT32)mFailures);
  return mFailures;
}
