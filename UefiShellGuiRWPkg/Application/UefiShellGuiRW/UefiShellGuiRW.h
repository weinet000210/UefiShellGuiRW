/** @file
  UefiShellGuiRW - shared declarations of the application.

  The application is split into
    Shell/   main frame: menu bar, toolbar, MDI desktop, status bar, commands
    Views/   the content of each child window (one file per module)
  and, from Phase 1 on, the UI-free hardware library RwCoreLib.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_SHELL_GUI_RW_H_
#define UEFI_SHELL_GUI_RW_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/LvglLib.h>
#include <Library/UefiGuiLib.h>
#include <Library/UefiGuiRuntimeLib.h>

#define RW_APP_NAME     "UefiShellGuiRW"
#define RW_APP_VERSION  "0.0.2"
#define RW_APP_PHASE    "Phase 2"

//
// Colors of the classic Windows look RW-Everything uses.
//
#define RW_COLOR_FACE         0xF0F0F0   // 3D face (menu bar, toolbar, dialogs)
#define RW_COLOR_SHADOW       0xA0A0A0   // 3D shadow
#define RW_COLOR_DARK         0x696969
#define RW_COLOR_WORKSPACE    0xABABAB   // MDI client area
#define RW_COLOR_HILITE       0x3399FF   // menu / selection highlight
#define RW_COLOR_HILITE_SOFT  0xCCE8FF   // hover on toolbar buttons
#define RW_COLOR_HILITE_EDGE  0x99D1FF
#define RW_COLOR_TITLE_A1     0x0A246A   // active caption gradient
#define RW_COLOR_TITLE_A2     0xA6CAF0
#define RW_COLOR_TITLE_I1     0x808080   // inactive caption gradient
#define RW_COLOR_TITLE_I2     0xC0C0C0
#define RW_COLOR_TEXT         0x000000
#define RW_COLOR_TEXT_GRAY    0x6D6D6D

#define RW_MENUBAR_HEIGHT   22
#define RW_TOOLBAR_HEIGHT   30
#define RW_STATUSBAR_HEIGHT 22
#define RW_CAPTION_HEIGHT   20

//
// Commands. Every menu entry and toolbar button runs one of these.
//
typedef enum {
  RwCmdNone = 0,
  // File
  RwCmdRefresh,
  RwCmdScreenshot,
  RwCmdDumpAll,
  RwCmdExit,
  // Access
  RwCmdPci,
  RwCmdMemory,
  RwCmdIo,
  RwCmdIndexData,
  RwCmdCmos,
  RwCmdMsr,
  RwCmdEc,
  RwCmdSuperIo,
  RwCmdSmbus,
  RwCmdStorage,
  RwCmdUsb,
  // Specific
  RwCmdAcpi,
  RwCmdSmbios,
  RwCmdPcieTree,
  RwCmdEdid,
  RwCmdComPort,
  // Window
  RwCmdCascade,
  RwCmdTileH,
  RwCmdTileV,
  RwCmdCloseAll,
  // Help
  RwCmdAbout,
  RwCmdCount
} RW_COMMAND_ID;

typedef struct {
  RW_COMMAND_ID           Id;
  CONST CHAR8             *Name;      ///< Menu text and window title.
  CONST CHAR8             *Shortcut;  ///< Shown right-aligned in the menu, or NULL.
  CONST lv_image_dsc_t    *Icon;      ///< Toolbar / menu icon, or NULL.
  CONST CHAR8             *Planned;   ///< Phase that implements it ("Phase 3"), NULL when done.
} RW_COMMAND;

//
// Commands.c
//
CONST RW_COMMAND *
RwGetCommand (
  IN RW_COMMAND_ID  Id
  );

BOOLEAN
RwIsCommandEnabled (
  IN RW_COMMAND_ID  Id
  );

VOID
RwRunCommand (
  IN RW_COMMAND_ID  Id
  );

//
// MainFrame.c
//
typedef struct {
  lv_obj_t    *Screen;
  lv_obj_t    *MenuBar;
  lv_obj_t    *Toolbar;
  lv_obj_t    *Desktop;
  lv_obj_t    *StatusBar;
} RW_MAIN_FRAME;

extern RW_MAIN_FRAME  gRwFrame;

EFI_STATUS
RwMainFrameCreate (
  VOID
  );

//
// MenuBar.c
//
VOID
RwMenuBarCreate (
  IN lv_obj_t  *Parent
  );

/** Open menu Index (0 = File) with its first enabled entry selected. **/
VOID
RwMenuOpen (
  IN UINTN  Index
  );

VOID
RwMenuClose (
  VOID
  );

BOOLEAN
RwMenuIsOpen (
  VOID
  );

/** Index of the open menu, or -1. **/
INTN
RwMenuOpenIndex (
  VOID
  );

UINTN
RwMenuCount (
  VOID
  );

/** Keyboard handling while a menu is open, and F10 / Alt+letter. TRUE = consumed. **/
BOOLEAN
RwMenuHandleKey (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  );

//
// Toolbar.c
//
VOID
RwToolbarCreate (
  IN lv_obj_t  *Parent
  );

UINTN
RwToolbarButtonCount (
  VOID
  );

/** Toolbar button of Id, or NULL. **/
lv_obj_t *
RwToolbarGetButton (
  IN RW_COMMAND_ID  Id
  );

/** Gray out the buttons whose command is disabled right now. **/
VOID
RwToolbarUpdate (
  VOID
  );

//
// StatusBar.c
//
VOID
RwStatusBarCreate (
  IN lv_obj_t  *Parent
  );

VOID
RwStatusSetText (
  IN CONST CHAR8  *Text
  );

VOID
RwStatusSetWindow (
  IN CONST CHAR8  *Text  OPTIONAL
  );

//
// Mdi.c - child windows inside the desktop.
//
typedef struct _RW_MDI_WINDOW RW_MDI_WINDOW;

typedef
VOID
(*RW_MDI_CLOSE_CALLBACK)(
  IN RW_MDI_WINDOW  *Window
  );

typedef
VOID
(*RW_MDI_CALLBACK)(
  IN RW_MDI_WINDOW  *Window
  );

#define RW_MDI_MAX_FOCUS  8

struct _RW_WIDGET;

struct _RW_MDI_WINDOW {
  lv_obj_t                 *Obj;        ///< Frame.
  lv_obj_t                 *Caption;
  lv_obj_t                 *Title;
  lv_obj_t                 *Content;    ///< Client area; views build here.
  lv_obj_t                 *MaxButton;
  RW_COMMAND_ID            Command;     ///< Command that opened it.
  BOOLEAN                  Maximized;
  BOOLEAN                  Minimized;
  lv_area_t                Restore;     ///< Normal geometry (desktop coordinates).
  RW_MDI_CLOSE_CALLBACK    OnClose;
  RW_MDI_CALLBACK          OnRefresh;   ///< File > Refresh (F5) while active, or NULL.
  VOID                     *Context;
  struct _RW_WIDGET        *Focus[RW_MDI_MAX_FOCUS];   ///< focus chain (Widgets/Focus.c)
  UINTN                    FocusCount;
  UINTN                    FocusIndex;
  RW_MDI_WINDOW            *Next;
};

VOID
RwMdiInitialize (
  IN lv_obj_t  *Desktop
  );

RW_MDI_WINDOW *
RwMdiCreate (
  IN CONST CHAR8     *Title,
  IN RW_COMMAND_ID   Command,
  IN INT32           Width,
  IN INT32           Height
  );

VOID
RwMdiClose (
  IN RW_MDI_WINDOW  *Window
  );

VOID
RwMdiCloseAll (
  VOID
  );

VOID
RwMdiActivate (
  IN RW_MDI_WINDOW  *Window
  );

RW_MDI_WINDOW *
RwMdiGetActive (
  VOID
  );

/** First window opened by Command, or NULL. **/
RW_MDI_WINDOW *
RwMdiFind (
  IN RW_COMMAND_ID  Command
  );

UINTN
RwMdiCount (
  VOID
  );

/** Window number Index in creation order (for the Window menu). **/
RW_MDI_WINDOW *
RwMdiGetAt (
  IN UINTN  Index
  );

VOID
RwMdiActivateNext (
  VOID
  );

VOID
RwMdiSetMaximized (
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        Maximized
  );

VOID
RwMdiSetMinimized (
  IN RW_MDI_WINDOW  *Window,
  IN BOOLEAN        Minimized
  );

VOID
RwMdiCascade (
  VOID
  );

VOID
RwMdiTile (
  IN BOOLEAN  Horizontal
  );

//
// Views
//
RW_MDI_WINDOW *
RwViewOpenPlaceholder (
  IN RW_COMMAND_ID  Command
  );

RW_MDI_WINDOW *
RwViewOpenAbout (
  VOID
  );

/** Widget test window (self test only): hex grid, bit view, tree, tables. **/
RW_MDI_WINDOW *
RwViewOpenWidgetTest (
  VOID
  );

//
// UefiShellGuiRW.c
//
BOOLEAN
EFIAPI
RwKeyHandler (
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  );

VOID
RwRequestExit (
  VOID
  );

VOID
EFIAPI
RwSerialLog (
  IN CONST CHAR8  *Format,
  ...
  );

//
// SelfTest.c, SelfTestWidgets.c
//
UINTN
RwSelfTestRun (
  VOID
  );

VOID
RwTestCheck (
  IN CONST CHAR8  *Name,
  IN BOOLEAN      Passed
  );

VOID
RwTestPump (
  IN UINT64  Ms
  );

/** Send one key through RwKeyHandler, as the keyboard would. **/
VOID
RwTestKey (
  IN UINT16  ScanCode,
  IN CHAR16  Char,
  IN UINT32  Modifiers
  );

VOID
RwTestType (
  IN CONST CHAR8  *Text
  );

VOID
RwSelfTestWidgets (
  VOID
  );

//
// Icons.c (generated by scripts/gen-icons.py)
//
extern CONST lv_image_dsc_t  gIconPci;
extern CONST lv_image_dsc_t  gIconMemory;
extern CONST lv_image_dsc_t  gIconIo;
extern CONST lv_image_dsc_t  gIconIndexData;
extern CONST lv_image_dsc_t  gIconCmos;
extern CONST lv_image_dsc_t  gIconMsr;
extern CONST lv_image_dsc_t  gIconEc;
extern CONST lv_image_dsc_t  gIconSuperIo;
extern CONST lv_image_dsc_t  gIconSmbus;
extern CONST lv_image_dsc_t  gIconStorage;
extern CONST lv_image_dsc_t  gIconUsb;
extern CONST lv_image_dsc_t  gIconAcpi;
extern CONST lv_image_dsc_t  gIconSmbios;
extern CONST lv_image_dsc_t  gIconEdid;
extern CONST lv_image_dsc_t  gIconComPort;
extern CONST lv_image_dsc_t  gIconPcieTree;
extern CONST lv_image_dsc_t  gIconRefresh;
extern CONST lv_image_dsc_t  gIconScreenshot;
extern CONST lv_image_dsc_t  gIconDumpAll;
extern CONST lv_image_dsc_t  gIconCascade;
extern CONST lv_image_dsc_t  gIconTileH;
extern CONST lv_image_dsc_t  gIconTileV;
extern CONST lv_image_dsc_t  gIconAbout;
extern CONST lv_image_dsc_t  gIconExit;

#endif
