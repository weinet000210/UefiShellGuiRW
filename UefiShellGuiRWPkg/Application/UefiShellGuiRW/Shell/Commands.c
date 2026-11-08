/** @file
  Command table: one entry per menu item / toolbar button.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"
#include "../Views/Views.h"

STATIC CONST RW_COMMAND  mCommands[] = {
  { RwCmdNone,       "",                          NULL,       NULL,             NULL      },
  { RwCmdRefresh,    "Refresh",                   "F5",       &gIconRefresh,    NULL      },
  { RwCmdScreenshot, "Save Screenshot",           "F12",      &gIconScreenshot, "Phase 6" },
  { RwCmdDumpAll,    "Dump All to File",          NULL,       &gIconDumpAll,    "Phase 6" },
  { RwCmdExit,       "Exit",                      "Alt+X",    &gIconExit,       NULL      },
  { RwCmdPci,        "PCI Devices",               NULL,       &gIconPci,        NULL      },
  { RwCmdMemory,     "Memory",                    NULL,       &gIconMemory,     NULL      },
  { RwCmdIo,         "I/O Space",                 NULL,       &gIconIo,         NULL      },
  { RwCmdIndexData,  "Index/Data",                NULL,       &gIconIndexData,  NULL      },
  { RwCmdCmos,       "CMOS",                      NULL,       &gIconCmos,       NULL      },
  { RwCmdMsr,        "CPU MSR",                   NULL,       &gIconMsr,        NULL      },
  { RwCmdEc,         "Embedded Controller",       NULL,       &gIconEc,         "Phase 5" },
  { RwCmdSuperIo,    "Super I/O",                 NULL,       &gIconSuperIo,    "Phase 5" },
  { RwCmdSmbus,      "SMBus / SPD",               NULL,       &gIconSmbus,      "Phase 5" },
  { RwCmdStorage,    "ATA / NVMe Identify",       NULL,       &gIconStorage,    "Phase 5" },
  { RwCmdUsb,        "USB Devices",               NULL,       &gIconUsb,        NULL      },
  { RwCmdAcpi,       "ACPI Tables",               NULL,       &gIconAcpi,       NULL      },
  { RwCmdSmbios,     "SMBIOS",                    NULL,       &gIconSmbios,     NULL      },
  { RwCmdPcieTree,   "PCIe Tree",                 NULL,       &gIconPcieTree,   NULL      },
  { RwCmdEdid,       "Monitor EDID",              NULL,       &gIconEdid,       "Phase 5" },
  { RwCmdComPort,    "COM Port Test",             NULL,       &gIconComPort,    "Phase 5" },
  { RwCmdCascade,    "Cascade",                   NULL,       &gIconCascade,    NULL      },
  { RwCmdTileH,      "Tile Horizontally",         NULL,       &gIconTileH,      NULL      },
  { RwCmdTileV,      "Tile Vertically",           NULL,       &gIconTileV,      NULL      },
  { RwCmdCloseAll,   "Close All",                 NULL,       NULL,             NULL      },
  { RwCmdAbout,      "About UefiShellGuiRW",      "F1",       &gIconAbout,      NULL      },
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
    case RwCmdScreenshot:
    case RwCmdDumpAll:
      return FALSE;
    case RwCmdCascade:
    case RwCmdTileH:
    case RwCmdTileV:
    case RwCmdCloseAll:
      return (BOOLEAN)(RwMdiCount () > 0);
    default:
      return TRUE;
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
      RwRequestExit ();
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
          default:
            RwViewOpenPlaceholder (Id);
            break;
        }
      }

      break;
  }
}
