/** @file
  Command table: one entry per menu item / toolbar button.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

STATIC CONST RW_COMMAND  mCommands[] = {
  { RwCmdNone,       "",                          NULL,       NULL,             NULL      },
  { RwCmdRefresh,    "Refresh",                   "F5",       &gIconRefresh,    NULL      },
  { RwCmdScreenshot, "Save Screenshot",           "F12",      &gIconScreenshot, "Phase 6" },
  { RwCmdDumpAll,    "Dump All to File",          NULL,       &gIconDumpAll,    "Phase 6" },
  { RwCmdExit,       "Exit",                      "Alt+X",    &gIconExit,       NULL      },
  { RwCmdPci,        "PCI Devices",               NULL,       &gIconPci,        "Phase 3" },
  { RwCmdMemory,     "Memory",                    NULL,       &gIconMemory,     "Phase 3" },
  { RwCmdIo,         "I/O Space",                 NULL,       &gIconIo,         "Phase 3" },
  { RwCmdIndexData,  "Index/Data",                NULL,       &gIconIndexData,  "Phase 3" },
  { RwCmdCmos,       "CMOS",                      NULL,       &gIconCmos,       "Phase 3" },
  { RwCmdMsr,        "CPU MSR",                   NULL,       &gIconMsr,        "Phase 3" },
  { RwCmdEc,         "Embedded Controller",       NULL,       &gIconEc,         "Phase 5" },
  { RwCmdSuperIo,    "Super I/O",                 NULL,       &gIconSuperIo,    "Phase 5" },
  { RwCmdSmbus,      "SMBus / SPD",               NULL,       &gIconSmbus,      "Phase 5" },
  { RwCmdStorage,    "ATA / NVMe Identify",       NULL,       &gIconStorage,    "Phase 5" },
  { RwCmdUsb,        "USB Devices",               NULL,       &gIconUsb,        "Phase 4" },
  { RwCmdAcpi,       "ACPI Tables",               NULL,       &gIconAcpi,       "Phase 4" },
  { RwCmdSmbios,     "SMBIOS",                    NULL,       &gIconSmbios,     "Phase 4" },
  { RwCmdPcieTree,   "PCIe Tree",                 NULL,       &gIconPcieTree,   "Phase 4" },
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
        RwViewOpenPlaceholder (Id);
      }

      break;
  }
}
