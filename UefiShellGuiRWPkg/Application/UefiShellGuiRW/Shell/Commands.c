/** @file
  Command table: one entry per menu item / toolbar button.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"
#include "../Tools/Tools.h"

STATIC CONST RW_COMMAND  mCommands[] = {
  { RwCmdNone,       "",                          NULL,       NULL,             NULL      },
  { RwCmdRefresh,    "Refresh",                   "F5",       &gIconRefresh,    NULL      },
  { RwCmdScreenshot, "Save Screenshot",           "F12",      &gIconScreenshot, NULL      },
  { RwCmdSaveWindow, "Save Window to File",       "Ctrl+S",   NULL,             NULL      },
  { RwCmdDumpAll,    "Dump All to File",          NULL,       &gIconDumpAll,    NULL      },
  { RwCmdWriteLog,   "Write Log",                 NULL,       &gIconWriteLog,   NULL      },
  { RwCmdFiles,      "File Browser",              NULL,       &gIconFiles,      NULL      },
  { RwCmdExit,       "Exit",                      "Alt+X",    &gIconExit,       NULL      },
  { RwCmdPci,        "PCI Devices",               NULL,       &gIconPci,        NULL      },
  { RwCmdMemory,     "Memory",                    NULL,       &gIconMemory,     NULL      },
  { RwCmdIo,         "I/O Space",                 NULL,       &gIconIo,         NULL      },
  { RwCmdIndexData,  "Index/Data",                NULL,       &gIconIndexData,  NULL      },
  { RwCmdCmos,       "CMOS",                      NULL,       &gIconCmos,       NULL      },
  { RwCmdMsr,        "CPU MSR",                   NULL,       &gIconMsr,        NULL      },
  { RwCmdEc,         "Embedded Controller",       NULL,       &gIconEc,         NULL      },
  { RwCmdSuperIo,    "Super I/O",                 NULL,       &gIconSuperIo,    NULL      },
  { RwCmdSmbus,      "SMBus / SPD",               NULL,       &gIconSmbus,      NULL      },
  { RwCmdStorage,    "ATA / NVMe Identify",       NULL,       &gIconStorage,    NULL      },
  { RwCmdUsb,        "USB Devices",               NULL,       &gIconUsb,        NULL      },
  { RwCmdAcpi,       "ACPI Tables",               NULL,       &gIconAcpi,       NULL      },
  { RwCmdSmbios,     "SMBIOS",                    NULL,       &gIconSmbios,     NULL      },
  { RwCmdPcieTree,   "PCIe Tree",                 NULL,       &gIconPcieTree,   NULL      },
  { RwCmdEdid,       "Monitor EDID",              NULL,       &gIconEdid,       NULL      },
  { RwCmdComPort,    "COM Port Test",             NULL,       &gIconComPort,    NULL      },
  { RwCmdCascade,    "Cascade",                   NULL,       &gIconCascade,    NULL      },
  { RwCmdTileH,      "Tile Horizontally",         NULL,       &gIconTileH,      NULL      },
  { RwCmdTileV,      "Tile Vertically",           NULL,       &gIconTileV,      NULL      },
  { RwCmdCloseAll,   "Close All",                 NULL,       NULL,             NULL      },
  { RwCmdAbout,      "About UefiShellGuiRW",      "F1",       &gIconAbout,      NULL      },
  { RwCmdFileEdit,   "File Editor",               NULL,       &gIconFiles,      NULL      },
};

CONST RW_COMMAND *
RwGetCommand (
  IN RW_COMMAND_ID  Id
  )
{
  if ((UINTN)Id >= ARRAY_SIZE (mCommands)) {
    return &mCommands[0];
  }

  ASSERT (mCommands[Id].Id == Id);
  return &mCommands[Id];
}

/**
  Commands without a target are disabled (shown gray), like RW-Everything
  grays out what the machine does not have. Refresh needs an active window
  that can refresh; the window commands need a window.
**/
BOOLEAN
RwIsCommandEnabled (
  IN RW_COMMAND_ID  Id
  )
{
  switch (Id) {
    case RwCmdRefresh:
      return (BOOLEAN)((RwMdiGetActive () != NULL) && (RwMdiGetActive ()->OnRefresh != NULL));
    case RwCmdSaveWindow:
      return (BOOLEAN)(RwMdiGetActive () != NULL);
    case RwCmdFileEdit:
      return FALSE;      // opened from the file browser
    case RwCmdCascade:
    case RwCmdTileH:
    case RwCmdTileV:
    case RwCmdCloseAll:
      return (BOOLEAN)(RwMdiCount () > 0);
    default:
      return TRUE;
  }
}

STATIC
VOID
ExitDone (
  IN BOOLEAN  Accepted,
  IN UINT64   Value,
  IN VOID     *Context
  )
{
  if (Accepted) {
    RwRequestExit ();
  }
}

VOID
RwRunCommand (
  IN RW_COMMAND_ID  Id
  )
{
  RW_MDI_WINDOW  *Window;

  if (!RwIsCommandEnabled (Id)) {
    return;
  }

  switch (Id) {
    case RwCmdExit:
      if (RwFileEditDirtyCount () > 0) {
        RwDialogConfirm (
          "Exit",
          "A file editor has unsaved changes.\nExit and discard them?",
          RW_DIALOG_WARNING,
          ExitDone,
          NULL
          );
      } else {
        RwRequestExit ();
      }

      break;
    case RwCmdScreenshot:
      RwToolScreenshot ();
      break;
    case RwCmdSaveWindow:
      RwToolSaveWindow (RwMdiGetActive ());
      break;
    case RwCmdDumpAll:
      RwToolDumpAll ();
      break;
    case RwCmdRefresh:
      Window = RwMdiGetActive ();
      Window->OnRefresh (Window);
      break;
    case RwCmdCascade:
      RwMdiCascade ();
      break;
    case RwCmdTileH:
      RwMdiTile (TRUE);
      break;
    case RwCmdTileV:
      RwMdiTile (FALSE);
      break;
    case RwCmdCloseAll:
      RwMdiCloseAll ();
      break;
    case RwCmdAbout:
      RwViewOpenAbout ();
      break;
    default:
      //
      // Like RW-Everything, a module opens one window; asking again brings
      // the existing one to the front.
      //
      Window = RwMdiFind (Id);
      if (Window != NULL) {
        if (Window->Minimized) {
          RwMdiSetMinimized (Window, FALSE);
        }

        RwMdiActivate (Window);
      } else {
        switch (Id) {
          case RwCmdPci:
            RwViewOpenPci ();
            break;
          case RwCmdMemory:
            RwViewOpenMemory ();
            break;
          case RwCmdIo:
            RwViewOpenIo ();
            break;
          case RwCmdIndexData:
            RwViewOpenIndexData ();
            break;
          case RwCmdCmos:
            RwViewOpenCmos ();
            break;
          case RwCmdMsr:
            RwViewOpenMsr ();
            break;
          case RwCmdPcieTree:
            RwViewOpenPcieTree ();
            break;
          case RwCmdAcpi:
            RwViewOpenAcpi ();
            break;
          case RwCmdSmbios:
            RwViewOpenSmbios ();
            break;
          case RwCmdUsb:
            RwViewOpenUsb ();
            break;
          case RwCmdEc:
            RwViewOpenEc ();
            break;
          case RwCmdSuperIo:
            RwViewOpenSio ();
            break;
          case RwCmdSmbus:
            RwViewOpenSmbus ();
            break;
          case RwCmdStorage:
            RwViewOpenStorage ();
            break;
          case RwCmdEdid:
            RwViewOpenEdid ();
            break;
          case RwCmdComPort:
            RwViewOpenCom ();
            break;
          case RwCmdWriteLog:
            RwViewOpenWriteLog ();
            break;
          case RwCmdFiles:
            RwViewOpenFiles ();
            break;
          default:
            RwViewOpenPlaceholder (Id);
            break;
        }
      }

      break;
  }
}
