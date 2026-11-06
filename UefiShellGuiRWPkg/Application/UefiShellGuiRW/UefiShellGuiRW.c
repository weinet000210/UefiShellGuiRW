/** @file
  UefiShellGuiRW - read & write hardware resources under the UEFI Shell,
  with an LVGL GUI in the style of RW-Everything.

    UefiShellGuiRW.efi            interactive
    UefiShellGuiRW.efi -selftest  build the GUI, run the frame self test,
                                  print "UEFISHELLGUIRW: PASS" or "FAIL"

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiShellGuiRW.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SerialIo.h>

/**
  Write a line straight to the first serial port (the GUI owns ConOut).
  Used for the markers the QEMU scripts wait for.
**/
VOID
EFIAPI
RwSerialLog (
  IN CONST CHAR8  *Format,
  ...
  )
{
  STATIC EFI_SERIAL_IO_PROTOCOL  *Serial;
  STATIC BOOLEAN                 Looked;
  CHAR8                          Line[256];
  VA_LIST                        Args;
  UINTN                          Size;

  if (!Looked) {
    Looked = TRUE;
    gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&Serial);
  }

  VA_START (Args, Format);
  AsciiVSPrint (Line, sizeof (Line), Format, Args);
  VA_END (Args);
  DEBUG ((DEBUG_INFO, "%a", Line));
  if (Serial != NULL) {
    Size = AsciiStrLen (Line);
    Serial->Write (Serial, &Size, Line);
  }
}

VOID
RwRequestExit (
  VOID
  )
{
  UefiGuiRequestExit ();
}

/**
  Application key handler; sees every key before LVGL.
**/
BOOLEAN
EFIAPI
RwKeyHandler (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  )
{
  RW_MDI_WINDOW  *Window;
  BOOLEAN        Ctrl;
  BOOLEAN        Alt;

  if (RwMenuHandleKey (Key)) {
    return TRUE;
  }

  Ctrl = (BOOLEAN)((Key->Modifiers & UEFI_GUI_KEY_MOD_CTRL) != 0);
  Alt  = (BOOLEAN)((Key->Modifiers & UEFI_GUI_KEY_MOD_ALT) != 0);

  if (Alt && ((Key->UnicodeChar == L'x') || (Key->UnicodeChar == L'X'))) {
    RwRunCommand (RwCmdExit);
    return TRUE;
  }

  if (Ctrl && (Key->ScanCode == SCAN_F4)) {
    Window = RwMdiGetActive ();
    if (Window != NULL) {
      RwMdiClose (Window);
    }

    return TRUE;
  }

  if (Ctrl && (Key->UnicodeChar == CHAR_TAB)) {
    RwMdiActivateNext ();
    return TRUE;
  }

  switch (Key->ScanCode) {
    case SCAN_F1:
      RwRunCommand (RwCmdAbout);
      return TRUE;
    case SCAN_F5:
      RwRunCommand (RwCmdRefresh);
      return TRUE;
    case SCAN_F12:
      RwRunCommand (RwCmdScreenshot);
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
BOOLEAN
HasArgument (
  IN EFI_HANDLE    ImageHandle,
  IN CONST CHAR16  *Argument
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  CHAR16                     *Options;
  UINTN                      Length;
  UINTN                      ArgLength;
  UINTN                      Index;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image)) ||
      (Image->LoadOptions == NULL))
  {
    return FALSE;
  }

  Options   = (CHAR16 *)Image->LoadOptions;
  Length    = Image->LoadOptionsSize / sizeof (CHAR16);
  ArgLength = StrLen (Argument);
  for (Index = 0; Index + ArgLength <= Length; Index++) {
    if ((CompareMem (&Options[Index], Argument, ArgLength * sizeof (CHAR16)) == 0) &&
        ((Index == 0) || (Options[Index - 1] == L' ')) &&
        ((Index + ArgLength == Length) || (Options[Index + ArgLength] == L' ') ||
         (Options[Index + ArgLength] == L'\0')))
    {
      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
EFIAPI
UefiShellGuiRWMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UEFI_GUI_CONFIG  Config;
  EFI_STATUS       Status;
  BOOLEAN          SelfTest;
  UINTN            Failures;

  SelfTest = HasArgument (ImageHandle, L"-selftest");
  Failures = 0;

  UefiGuiConfigInitDefaults (&Config);
  Status = UefiGuiInitialize (&Config);
  if (EFI_ERROR (Status)) {
    Print (L"%a: cannot start the GUI: %r\n", RW_APP_NAME, Status);
    if (SelfTest) {
      Print (L"UEFISHELLGUIRW: FAIL\n");
    }

    return Status;
  }

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  RwMainFrameCreate ();
  UefiGuiSetKeyHandler (RwKeyHandler, NULL);

  if (SelfTest) {
    Failures = RwSelfTestRun ();
  } else {
    UefiGuiRun ();
  }

  UefiGuiSetKeyHandler (NULL, NULL);
  RwMenuClose ();
  RwMdiCloseAll ();
  UefiGuiProcessEvents ();   // run the pending asynchronous deletes
  UefiGuiShutdown ();
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  gST->ConOut->ClearScreen (gST->ConOut);

  if (SelfTest) {
    Print (L"UEFISHELLGUIRW: %s\n", (Failures == 0) ? L"PASS" : L"FAIL");
  }

  return EFI_SUCCESS;
}
