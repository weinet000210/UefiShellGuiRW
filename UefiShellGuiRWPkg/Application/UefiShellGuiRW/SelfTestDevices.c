/** @file
  Phase 5 self test: Embedded Controller, Super I/O, COM ports, SMBus / SPD
  (including turning the host controller on), ATA / NVMe Identify, Monitor
  EDID and per-CPU MSRs, driven with keys. Only the SMBus host enable and
  nothing else is written; COM1 (the test log) is never put in loopback.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Views/Views.h"

#define Check  RwTestCheck
#define Pump   RwTestPump
#define Key    RwTestKey

STATIC
BOOLEAN
InfoHas (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name,
  IN CONST CHAR8    *Value
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if (((Name == NULL) || (AsciiStrCmp (Info->Rows[Index].Name, Name) == 0)) &&
        ((AsciiStrStr (Info->Rows[Index].Value, Value) != NULL) || ((Name == NULL) && (AsciiStrStr (Info->Rows[Index].Name, Value) != NULL))))
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
TestEcSio (
  VOID
  )
{
  RW_EC_VIEW   *Ec;
  RW_SIO_VIEW  *Sio;

  RwRunCommand (RwCmdEc);
  Pump (30);
  Ec = RwEcViewGet ();
  Check ("ec: command opens the window", (BOOLEAN)(Ec->Window != NULL && RwMdiGetActive () == Ec->Window));
  Check ("ec: q35 has no EC, the window says so (no grid)", (BOOLEAN)(!Ec->Present && Ec->Note != NULL && Ec->Pane.Grid == NULL));

  RwRunCommand (RwCmdSuperIo);
  Pump (30);
  Sio = RwSioViewGet ();
  Check ("sio: command opens the window", (BOOLEAN)(Sio->Window != NULL && RwMdiGetActive () == Sio->Window));
  Check ("sio: q35 has no Super I/O, the window says so", (BOOLEAN)(!Sio->Found && Sio->Note != NULL));
}

STATIC
VOID
TestCom (
  VOID
  )
{
  RW_COM_VIEW  *V;

  RwRunCommand (RwCmdComPort);
  Pump (30);
  V = RwComViewGet ();
  Check ("com: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("com: COM1 present with the firmware console, COM2-4 absent", (BOOLEAN)(V->Present[0] && V->Firmware[0] && !V->Present[1] && !V->Present[2] && !V->Present[3]));
  Check ("com: COM1 line settings 115200 8N1", (BOOLEAN)(V->Current == 0 && InfoHas (RwPropTableGetInfo (V->Decode), "Baud rate", "115200") && InfoHas (RwPropTableGetInfo (V->Decode), "Format", "8N1")));
  Key (SCAN_DOWN, 0, 0);
  Pump (20);
  Check ("com: COM2 says no UART", (BOOLEAN)(V->Current == 1 && InfoHas (RwPropTableGetInfo (V->Decode), NULL, "No 16550")));
  Key (SCAN_NULL, L'l', 0);
  Pump (20);
  Check ("com: L on a missing port explains instead of testing", (BOOLEAN)(RwDialogIsOpen () && !V->HaveResult[1]));
  Key (SCAN_NULL, CHAR_CARRIAGE_RETURN, 0);
  Pump (20);
  Check ("com: message closes", (BOOLEAN)!RwDialogIsOpen ());
}

STATIC
VOID
TestSmbus (
  VOID
  )
{
  RW_SMBUS_VIEW  *V;

  RwRunCommand (RwCmdSmbus);
  Pump (50);
  V = RwSmbusViewGet ();
  Check ("smbus: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("smbus: ICH9 host found", (BOOLEAN)(V->Bus.Kind == RW_SMBUS_INTEL && V->Bus.DeviceId == 0x2930));
  if (V->BusStatus == EFI_NOT_READY) {
    Check ("smbus: heading offers to enable the host", (BOOLEAN)(AsciiStrStr (lv_label_get_text (V->Heading), "E to enable") != NULL));
    Key (SCAN_NULL, L'e', 0);
    Pump (20);
    Check ("smbus: E asks first", RwDialogIsOpen ());
    Key (SCAN_NULL, L'y', 0);
    Pump (50);
  }

  Check ("smbus: host on, eight SPD EEPROMs listed", (BOOLEAN)(V->BusStatus == EFI_SUCCESS && V->Count >= 8 && RwTableRowCount (V->Devices) == V->Count));
  Check (
    "smbus: first SPD decoded, grid on the read-only SPD copy",
    (BOOLEAN)(V->Current == 0x50 && InfoHas (RwPropTableGetInfo (V->Decode), "Memory type", "") && V->Source.Write == NULL && RwDsSize (&V->Source) >= 256)
    );
  Key (SCAN_NULL, L't', UEFI_GUI_KEY_MOD_CTRL);
  Pump (20);
  Check ("smbus: Ctrl+T shows the raw registers (writable, dangerous)", (BOOLEAN)(V->Source.Write != NULL && RwDsIsDangerous (&V->Source, 0, 1)));
  Key (SCAN_NULL, L't', UEFI_GUI_KEY_MOD_CTRL);
  Pump (20);
  Check ("smbus: Ctrl+T back to the SPD copy", (BOOLEAN)(V->Source.Write == NULL));
  Key (SCAN_DOWN, 0, 0);
  Pump (30);
  Check ("smbus: Down selects the next device", (BOOLEAN)(V->Current == 0x51));
}

STATIC
VOID
TestStorage (
  VOID
  )
{
  RW_STORAGE_VIEW  *V;
  UINTN            Index;
  INTN             Ata;

  RwRunCommand (RwCmdStorage);
  Pump (50);
  V = RwStorageViewGet ();
  Check ("storage: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("storage: NVMe controller, namespace and AHCI disk listed", (BOOLEAN)(V->List->Count >= 3 && RwTableRowCount (V->Drives) == V->List->Count));
  Check (
    "storage: NVMe controller first, decoded with its serial",
    (BOOLEAN)(V->Current == 0 && V->List->Devs[0].Kind == RwStorNvmeCtrl && InfoHas (RwPropTableGetInfo (V->Decode), "Serial (SN)", "RWNVME01"))
    );
  Ata = -1;
  for (Index = 0; Index < V->List->Count; Index++) {
    if (V->List->Devs[Index].Kind == RwStorAta) {
      Ata = (INTN)Index;
      break;
    }
  }

  if (Ata >= 0) {
    RwVListSelect (RwTableList (V->Drives), (UINTN)Ata);
    Pump (30);
  }

  Check ("storage: AHCI disk decoded (ATA IDENTIFY)", (BOOLEAN)(Ata >= 0 && InfoHas (RwPropTableGetInfo (V->Decode), "Model", "QEMU HARDDISK")));
}

STATIC
VOID
TestEdid (
  VOID
  )
{
  RW_EDID_VIEW  *V;

  RwRunCommand (RwCmdEdid);
  Pump (30);
  V = RwEdidViewGet ();
  Check ("edid: command opens the window", (BOOLEAN)(V->Window != NULL && RwMdiGetActive () == V->Window));
  if (V->Window == NULL) {
    return;
  }

  Check ("edid: QEMU monitor listed", (BOOLEAN)(V->List->Count >= 1 && AsciiStrStr (V->List->Displays[0].Name, "QEMU") != NULL));
  Check ("edid: preferred timing 1024x768 decoded", (BOOLEAN)(V->Current == 0 && InfoHas (RwPropTableGetInfo (V->Decode), "Preferred timing", "1024x768")));
}

STATIC
VOID
TestMsrCpu (
  VOID
  )
{
  RW_MSR_VIEW  *V;
  UINTN        Row;

  RwRunCommand (RwCmdMsr);
  Pump (30);
  V = RwMsrViewGet ();
  Check ("msr cpu: two processors, boot processor shown", (BOOLEAN)(V->Window != NULL && V->CpuCount == 2 && V->CpuIndex == RwCpuBsp ()));
  if ((V->Window == NULL) || (V->CpuCount < 2)) {
    return;
  }

  for (Row = 0; Row < V->Count; Row++) {
    if (V->Entries[Row].Index == 0x1B) {
      break;
    }
  }

  Check ("msr cpu: IA32_APIC_BASE marks the boot processor", (BOOLEAN)(Row < V->Count && (V->Entries[Row].Value & BIT8) != 0));
  Key (SCAN_RIGHT, 0, UEFI_GUI_KEY_MOD_CTRL);
  Pump (30);
  Check (
    "msr cpu: Ctrl+Right shows the AP's MSRs",
    (BOOLEAN)(V->CpuIndex != RwCpuBsp () && Row < V->Count && V->Entries[Row].Valid && (V->Entries[Row].Value & BIT8) == 0 &&
              AsciiStrStr (lv_label_get_text (V->Cpu), "of 2") != NULL)
    );
  Key (SCAN_LEFT, 0, UEFI_GUI_KEY_MOD_CTRL);
  Pump (30);
  Check ("msr cpu: Ctrl+Left back to the boot processor", (BOOLEAN)(V->CpuIndex == RwCpuBsp () && (V->Entries[Row].Value & BIT8) != 0));
}

VOID
RwSelfTestDevices (
  VOID
  )
{
  RW_MDI_WINDOW  *Window;

  TestEcSio ();
  TestCom ();
  TestSmbus ();
  TestStorage ();
  TestEdid ();
  TestMsrCpu ();

  //
  // Screenshot layout: close the small windows, cascade the rest, SMBus / SPD on top.
  //
  Window = RwMdiFind (RwCmdEc);
  if (Window != NULL) {
    RwMdiClose (Window);
  }

  Window = RwMdiFind (RwCmdSuperIo);
  if (Window != NULL) {
    RwMdiClose (Window);
  }

  RwRunCommand (RwCmdCascade);
  Pump (50);
  Window = RwMdiFind (RwCmdSmbus);
  if (Window != NULL) {
    RwMdiActivate (Window);
  }

  Pump (50);
}
