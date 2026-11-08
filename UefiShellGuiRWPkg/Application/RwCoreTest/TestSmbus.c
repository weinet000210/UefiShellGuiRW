/** @file
  RwCoreTest: SMBus host controller (q35 ICH9 8086:2930 with eight SPD
  EEPROMs at 50h-57h) and the SPD decoder on built-in DDR4 / DDR5 images.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

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
    if ((AsciiStrCmp (Info->Rows[Index].Name, Name) == 0) && (AsciiStrStr (Info->Rows[Index].Value, Value) != NULL)) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
SetCrc (
  IN OUT UINT8  *Data,
  IN     UINTN  End
  )
{
  UINT16  Crc;

  Crc           = RwSpdCrc16 (Data, End);
  Data[End]     = (UINT8)Crc;
  Data[End + 1] = (UINT8)(Crc >> 8);
}

STATIC
VOID
TestSpdImages (
  VOID
  )
{
  STATIC RW_SPD  Spd;
  RW_INFO        Info;
  CHAR8          Line[128];

  //
  // DDR4 SO-DIMM, 8 Gb x8 single rank, 64 bit: 8 GB, tCK 625 ps = 3200 MT/s.
  //
  ZeroMem (&Spd, sizeof (Spd));
  Spd.Addr     = 0x50;
  Spd.Size     = 512;
  Spd.Type     = RW_SPD_DDR4;
  Spd.Data[0]  = 0x23;
  Spd.Data[1]  = 0x11;
  Spd.Data[2]  = RW_SPD_DDR4;
  Spd.Data[3]  = 0x03;
  Spd.Data[4]  = 0x45;
  Spd.Data[11] = 0x03;
  Spd.Data[12] = 0x01;
  Spd.Data[13] = 0x03;
  Spd.Data[18] = 0x05;
  Spd.Data[320] = 0x80;
  Spd.Data[321] = 0xCE;
  CopyMem (&Spd.Data[329], "M471A1K43DB1-CWE    ", 20);
  SetCrc (Spd.Data, 126);
  RwSpdSummary (&Spd, Line, sizeof (Line));
  TestLog ("    DDR4 image: %a", Line);
  CHECK (AsciiStrCmp (Line, "DDR4 SO-DIMM 8 GB 3200 MT/s  Samsung M471A1K43DB1-CWE") == 0, "SPD summary of a DDR4 image");
  RwInfoInit (&Info);
  RwSpdDecode (&Spd, &Info);
  CHECK (InfoHas (&Info, "Module size", "8192 MB") && InfoHas (&Info, "Base CRC", "OK") && InfoHas (&Info, "Manufacturer", "Samsung"), "SPD decode of a DDR4 image");
  RwInfoFree (&Info);
  Spd.Data[5] ^= 0xFF;
  RwInfoInit (&Info);
  RwSpdDecode (&Spd, &Info);
  CHECK (InfoHas (&Info, "Base CRC", "BAD"), "SPD CRC catches a changed byte");
  RwInfoFree (&Info);

  //
  // DDR5 UDIMM, 16 Gb x8, 2 ranks, 2 x 32-bit sub-channels: 32 GB, 416 ps = 4800 MT/s.
  //
  ZeroMem (&Spd, sizeof (Spd));
  Spd.Addr      = 0x51;
  Spd.Size      = 1024;
  Spd.Type      = RW_SPD_DDR5;
  Spd.Data[0]   = 0x30;
  Spd.Data[1]   = 0x10;
  Spd.Data[2]   = RW_SPD_DDR5;
  Spd.Data[3]   = 0x02;
  Spd.Data[4]   = 0x04;
  Spd.Data[6]   = 0x20;
  Spd.Data[20]  = (UINT8)416;
  Spd.Data[21]  = (UINT8)(416 >> 8);
  Spd.Data[234] = 0x08;
  Spd.Data[235] = 0x22;
  Spd.Data[512] = 0x80;
  Spd.Data[513] = 0xAD;
  CopyMem (&Spd.Data[521], "HMCG88AEBUA081N               ", 30);
  SetCrc (Spd.Data, 510);
  RwSpdSummary (&Spd, Line, sizeof (Line));
  TestLog ("    DDR5 image: %a", Line);
  CHECK (AsciiStrCmp (Line, "DDR5 UDIMM 32 GB 4800 MT/s  SK hynix HMCG88AEBUA081N") == 0, "SPD summary of a DDR5 image");
  CHECK (AsciiStrCmp (RwSpdManufacturer (0x01, 0x98), "Kingston") == 0, "JEDEC bank 2 manufacturer");
}

VOID
TestSmbus (
  VOID
  )
{
  RW_SMBUS        Bus;
  EFI_STATUS      Status;
  BOOLEAN         Present[128];
  UINTN           Count;
  UINTN           Addr;
  UINTN           SpdCount;
  UINT8           Value;
  STATIC RW_SPD   Spd;
  RW_DATA_SOURCE  Source;
  UINT64          V;
  CHAR8           Line[128];

  TestSpdImages ();

  Status = RwSmbusFind (&Bus);
  TestLog ("    %a: %r (I/O decode %d, host enable %d)", Bus.Name, Status, Bus.IoDecode, Bus.HostEnabled);
  if (Status == EFI_NOT_READY) {
    //
    // OVMF leaves the ICH9 SMBus host disabled.
    //
    Status = RwSmbusEnable (&Bus);
    TestLog ("    enable: %r", Status);
  }

  CHECK (!EFI_ERROR (Status) && (Bus.Kind == RW_SMBUS_INTEL) && (Bus.DeviceId == 0x2930) && (Bus.Base != 0), "ICH9 SMBus host found");
  if (EFI_ERROR (Status)) {
    return;
  }

  Count    = RwSmbusScan (&Bus, Present);
  SpdCount = 0;
  for (Addr = 0x50; Addr <= 0x57; Addr++) {
    SpdCount += Present[Addr] ? 1 : 0;
  }

  TestLog ("    %d devices, %d at 50h-57h", Count, SpdCount);
  CHECK (SpdCount == 8, "scan finds the eight SPD EEPROMs");
  CHECK (RwSmbusReadByte (&Bus, 0x12, 0, &Value) == EFI_NO_RESPONSE, "an empty address does not answer");
  CHECK (AsciiStrCmp (RwSmbusAddrHint (0x52), "SPD EEPROM") == 0, "address hint");

  Status = RwSpdRead (&Bus, 0x50, &Spd);
  RwSpdSummary (&Spd, Line, sizeof (Line));
  TestLog ("    SPD 50h: %r, %d bytes, %a", Status, Spd.Size, Line);
  CHECK (!EFI_ERROR (Status) && (Spd.Size >= 256), "SPD 50h reads");

  RwSmbusSourceInit (&Source, &Bus, 0x50);
  CHECK (!EFI_ERROR (RwDsRead (&Source, 2, 1, &V)) && (V == Spd.Data[2]) && RwDsIsDangerous (&Source, 0, 1), "SMBus data source reads, writes count as dangerous");
  RwSpdSourceInit (&Source, &Spd);
  CHECK ((Source.Write == NULL) && (RwDsSize (&Source) == Spd.Size), "SPD copy source is read-only");
}
