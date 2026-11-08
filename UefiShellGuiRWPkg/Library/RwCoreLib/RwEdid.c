/** @file
  RwCoreLib EDID: collection and the EDID 1.3 / 1.4 decoder.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/GraphicsOutput.h>

STATIC CONST UINT8  mHeader[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

BOOLEAN
RwEdidHasHeader (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  return (BOOLEAN)((Size >= 128) && (CompareMem (Data, mHeader, sizeof (mHeader)) == 0));
}

VOID
RwEdidPnpId (
  IN  CONST UINT8  *Data,
  OUT CHAR8        Id[4]
  )
{
  UINT16  V;

  V     = (UINT16)((Data[8] << 8) | Data[9]);
  Id[0] = (CHAR8)('A' - 1 + ((V >> 10) & 0x1F));
  Id[1] = (CHAR8)('A' - 1 + ((V >> 5) & 0x1F));
  Id[2] = (CHAR8)('A' - 1 + (V & 0x1F));
  Id[3] = '\0';
}

STATIC
VOID
DescText (
  IN  CONST UINT8  *Desc,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN  Index;
  UINTN  Out;

  Out = 0;
  for (Index = 5; (Index < 18) && (Out + 1 < BufferSize); Index++) {
    if (Desc[Index] == 0x0A) {
      break;
    }

    Buffer[Out++] = ((Desc[Index] >= 0x20) && (Desc[Index] < 0x7F)) ? (CHAR8)Desc[Index] : ' ';
  }

  while ((Out > 0) && (Buffer[Out - 1] == ' ')) {
    Out--;
  }

  Buffer[Out] = '\0';
}

STATIC
VOID
SetName (
  IN OUT RW_EDID  *Edid
  )
{
  UINTN  Index;
  CHAR8  Pnp[4];
  CONST UINT8  *Desc;

  for (Index = 0; Index < 4; Index++) {
    Desc = &Edid->Data[54 + Index * 18];
    if ((Desc[0] == 0) && (Desc[1] == 0) && (Desc[3] == 0xFC)) {
      DescText (Desc, Edid->Name, sizeof (Edid->Name));
      if (Edid->Name[0] != '\0') {
        return;
      }
    }
  }

  RwEdidPnpId (Edid->Data, Pnp);
  AsciiSPrint (Edid->Name, sizeof (Edid->Name), "%a %04X", Pnp, Edid->Data[10] | (Edid->Data[11] << 8));
}

STATIC
VOID
Add (
  IN OUT RW_EDID_LIST  *List,
  IN     CONST UINT8   *Data,
  IN     UINTN         Size,
  IN     CONST CHAR8   *Source
  )
{
  RW_EDID  *E;
  UINTN    Index;

  if ((List->Count >= RW_EDID_MAX_DISPLAYS) || !RwEdidHasHeader (Data, Size)) {
    return;
  }

  //
  // Size covers the base block and the extensions it announces.
  //
  Size = MIN (Size, (UINTN)(Data[126] + 1) * 128);
  Size = MIN (Size, RW_EDID_MAX_SIZE);
  for (Index = 0; Index < List->Count; Index++) {
    if ((List->Displays[Index].Size == Size) && (CompareMem (List->Displays[Index].Data, Data, Size) == 0)) {
      return;
    }
  }

  E       = &List->Displays[List->Count++];
  E->Size = Size;
  CopyMem (E->Data, Data, Size);
  AsciiStrCpyS (E->Source, sizeof (E->Source), Source);
  SetName (E);
}

STATIC
VOID
ScanQemuStdVga (
  IN OUT RW_EDID_LIST  *List
  )
{
  RW_PCI_LIST    Pci;
  UINTN          Index;
  RW_PCI_DEVICE  *D;
  UINT32         Bar;
  UINT8          Blob[RW_EDID_MAX_SIZE];
  volatile UINT8 *Mmio;
  UINTN          Byte;
  CHAR8          Source[64];

  if (EFI_ERROR (RwPciScan (&Pci)) && (Pci.DeviceCount == 0)) {
    return;
  }

  for (Index = 0; Index < Pci.DeviceCount; Index++) {
    D = &Pci.Devices[Index];
    if ((D->VendorId != 0x1234) || (D->DeviceId != 0x1111)) {
      continue;
    }

    //
    // QEMU stdvga: BAR 2 is the MMIO bar, its first 1 KB is the EDID blob.
    // Only read it while memory decoding is on.
    //
    Bar = RwPciRd (D, 0x18, 4);
    if (((RwPciRd (D, 0x04, 2) & BIT1) == 0) || ((Bar & BIT0) != 0) || ((Bar & ~0xFU) == 0)) {
      continue;
    }

    Mmio = (volatile UINT8 *)(UINTN)(Bar & ~0xFU);
    for (Byte = 0; Byte < sizeof (Blob); Byte++) {
      Blob[Byte] = Mmio[Byte];
    }

    AsciiSPrint (Source, sizeof (Source), "QEMU stdvga EDID window, %02X:%02X.%X", D->Bus, D->Dev, D->Func);
    Add (List, Blob, sizeof (Blob), Source);
  }

  RwPciFree (&Pci);
}

EFI_STATUS
RwEdidScan (
  OUT RW_EDID_LIST  *List
  )
{
  EFI_HANDLE                     *Handles;
  UINTN                          Count;
  UINTN                          Index;
  EFI_EDID_ACTIVE_PROTOCOL       *Active;
  EFI_EDID_DISCOVERED_PROTOCOL   *Discovered;
  CHAR8                          Source[64];

  ZeroMem (List, sizeof (*List));
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    for (Index = 0; Index < Count; Index++) {
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiEdidActiveProtocolGuid, (VOID **)&Active)) &&
          (Active->Edid != NULL) && (Active->SizeOfEdid >= 128))
      {
        AsciiSPrint (Source, sizeof (Source), "EDID Active, graphics output %d", Index);
        Add (List, Active->Edid, Active->SizeOfEdid, Source);
      } else if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiEdidDiscoveredProtocolGuid, (VOID **)&Discovered)) &&
                 (Discovered->Edid != NULL) && (Discovered->SizeOfEdid >= 128))
      {
        AsciiSPrint (Source, sizeof (Source), "EDID Discovered, graphics output %d", Index);
        Add (List, Discovered->Edid, Discovered->SizeOfEdid, Source);
      }
    }

    FreePool (Handles);
  }

  if (List->Count == 0) {
    ScanQemuStdVga (List);
  }

  return (List->Count > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

//
// ---- Decoder ---------------------------------------------------------------
//

STATIC
VOID
F (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT32       Offset,
  IN     UINT32       Size,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Value
  )
{
  RwInfoSetReg (RwInfoAdd (Info, RwKindField, Level, Emphasis, Name, Value), Offset, Size);
}

STATIC
UINT8
Sum (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  UINT8  S;
  UINTN  Index;

  S = 0;
  for (Index = 0; Index < Size; Index++) {
    S = (UINT8)(S + Data[Index]);
  }

  return S;
}

STATIC
VOID
DetailedTiming (
  IN OUT RW_INFO      *Info,
  IN     CONST UINT8  *T,
  IN     UINT32       Offset,
  IN     UINTN        Number
  )
{
  UINT32  Clock;
  UINT32  HActive;
  UINT32  HBlank;
  UINT32  VActive;
  UINT32  VBlank;
  UINT32  HTotal;
  UINT32  VTotal;
  UINT32  Hz100;
  CHAR8   Text[128];

  Clock   = (UINT32)(T[0] | (T[1] << 8)) * 10;          // kHz
  HActive = T[2] | ((T[4] & 0xF0) << 4);
  HBlank  = T[3] | ((T[4] & 0x0F) << 8);
  VActive = T[5] | ((T[7] & 0xF0) << 4);
  VBlank  = T[6] | ((T[7] & 0x0F) << 8);
  HTotal  = HActive + HBlank;
  VTotal  = VActive + VBlank;
  Hz100   = ((HTotal != 0) && (VTotal != 0)) ? (UINT32)DivU64x32 (MultU64x32 (Clock, 100000), HTotal * VTotal) : 0;
  AsciiSPrint (
    Text,
    sizeof (Text),
    "%dx%d%a @ %d.%02d Hz, %d.%02d MHz, total %dx%d, %dx%d mm",
    HActive,
    VActive,
    ((T[17] & BIT7) != 0) ? "i" : "",
    Hz100 / 100,
    Hz100 % 100,
    Clock / 1000,
    (Clock % 1000) / 10,
    HTotal,
    VTotal,
    T[12] | ((T[14] & 0xF0) << 4),
    T[13] | ((T[14] & 0x0F) << 8)
    );
  RwInfoSetReg (RwInfoField (Info, 1, (Number == 0) ? RwEmStrong : RwEmNormal, (Number == 0) ? "Preferred timing" : "Detailed timing", "%a", Text), Offset, 18);
  AsciiSPrint (
    Text,
    sizeof (Text),
    "H front %d sync %d, V front %d sync %d, %a",
    T[8] | ((T[11] & 0xC0) << 2),
    T[9] | ((T[11] & 0x30) << 4),
    (T[10] >> 4) | ((T[11] & 0x0C) << 2),
    (T[10] & 0x0F) | ((T[11] & 0x03) << 4),
    ((T[17] & 0x18) == 0x18) ? (((T[17] & BIT2) != 0) ? "+V" : "-V") : "composite"
    );
  F (Info, 2, Offset + 8, 4, RwEmDim, "Porches / sync", Text);
}

VOID
RwEdidDecode (
  IN     CONST UINT8  *E,
  IN     UINTN        Size,
  IN OUT RW_INFO      *Info
  )
{
  CHAR8                Text[128];
  CHAR8                Pnp[4];
  UINTN                Index;
  UINTN                Bit;
  CONST UINT8          *Desc;
  UINT32               Offset;
  UINTN                Ext;
  UINT32               HRes;
  STATIC CONST CHAR8   *Aspect[] = { "16:10", "4:3", "5:4", "16:9" };
  STATIC CONST CHAR8   *Established[] = {
    "800x600@60", "800x600@56", "640x480@75", "640x480@72", "640x480@67", "640x480@60", "720x400@88", "720x400@70",
    "1280x1024@75", "1024x768@75", "1024x768@70", "1024x768@60", "1024x768@87i", "832x624@75", "800x600@75", "800x600@72"
  };
  RW_STR               Str;

  if (!RwEdidHasHeader (E, Size)) {
    RwInfoText (Info, 0, RwEmAlert, "No EDID header.");
    RwInfoAddHex (Info, 0, E, MIN (Size, 128), 0);
    return;
  }

  RwInfoSection (Info, 0, "Vendor / product");
  RwEdidPnpId (E, Pnp);
  F (Info, 1, 8, 2, RwEmStrong, "Manufacturer ID", Pnp);
  AsciiSPrint (Text, sizeof (Text), "%04Xh", E[10] | (E[11] << 8));
  F (Info, 1, 10, 2, RwEmNormal, "Product code", Text);
  AsciiSPrint (Text, sizeof (Text), "%08Xh", *(UINT32 *)&E[12]);
  F (Info, 1, 12, 4, RwEmNormal, "Serial number", Text);
  if (E[16] == 0xFF) {
    AsciiSPrint (Text, sizeof (Text), "model year %d", 1990 + E[17]);
  } else {
    AsciiSPrint (Text, sizeof (Text), "week %d of %d", E[16], 1990 + E[17]);
  }

  F (Info, 1, 16, 2, RwEmNormal, "Manufactured", Text);
  AsciiSPrint (Text, sizeof (Text), "%d.%d", E[18], E[19]);
  F (Info, 1, 18, 2, RwEmNormal, "EDID version", Text);

  RwInfoSection (Info, 0, "Display parameters");
  if ((E[20] & BIT7) != 0) {
    STATIC CONST CHAR8  *Depth[] = { "undefined", "6", "8", "10", "12", "14", "16", "reserved" };
    STATIC CONST CHAR8  *Iface[] = { "undefined", "DVI", "HDMI-a", "HDMI-b", "MDDI", "DisplayPort" };
    AsciiSPrint (
      Text,
      sizeof (Text),
      "Digital, %a bits per colour, %a",
      Depth[(E[20] >> 4) & 7],
      ((E[20] & 0x0F) < ARRAY_SIZE (Iface)) ? Iface[E[20] & 0x0F] : "other"
      );
  } else {
    AsciiStrCpyS (Text, sizeof (Text), "Analog");
  }

  F (Info, 1, 20, 1, RwEmNormal, "Input", Text);
  if ((E[21] != 0) && (E[22] != 0)) {
    AsciiSPrint (Text, sizeof (Text), "%d x %d cm", E[21], E[22]);
  } else {
    AsciiStrCpyS (Text, sizeof (Text), "Not given (projector or variable)");
  }

  F (Info, 1, 21, 2, RwEmNormal, "Screen size", Text);
  if (E[23] != 0xFF) {
    AsciiSPrint (Text, sizeof (Text), "%d.%02d", (E[23] + 100) / 100, (E[23] + 100) % 100);
    F (Info, 1, 23, 1, RwEmNormal, "Gamma", Text);
  }

  AsciiSPrint (
    Text,
    sizeof (Text),
    "%a%a%a%a%a",
    ((E[24] & BIT7) != 0) ? "standby " : "",
    ((E[24] & BIT6) != 0) ? "suspend " : "",
    ((E[24] & BIT5) != 0) ? "active-off " : "",
    ((E[24] & BIT1) != 0) ? "preferred-timing-native " : "",
    ((E[24] & BIT0) != 0) ? "continuous-frequency" : ""
    );
  F (Info, 1, 24, 1, RwEmNormal, "Features", Text);

  RwInfoSection (Info, 0, "Established timings");
  RwStrInit (&Str);
  for (Bit = 0; Bit < 16; Bit++) {
    if ((((E[35] << 8) | E[36]) & (0x8000 >> (15 - Bit))) != 0) {
      if (Str.Len > 0) {
        RwStrAppend (&Str, ", ");
      }

      RwStrAppend (&Str, Established[15 - Bit]);
    }
  }

  F (Info, 1, 35, 3, RwEmNormal, "Supported", (Str.Len > 0) ? Str.Buf : "none");
  RwStrFree (&Str);

  RwInfoSection (Info, 0, "Standard timings");
  for (Index = 0; Index < 8; Index++) {
    Offset = 38 + (UINT32)Index * 2;
    if (((E[Offset] == 0x01) && (E[Offset + 1] == 0x01)) || (E[Offset] == 0x00)) {
      continue;
    }

    HRes = (E[Offset] + 31) * 8;
    AsciiSPrint (Text, sizeof (Text), "%d, aspect %a, %d Hz", HRes, Aspect[E[Offset + 1] >> 6], (E[Offset + 1] & 0x3F) + 60);
    F (Info, 1, Offset, 2, RwEmNormal, "Horizontal", Text);
  }

  RwInfoSection (Info, 0, "Descriptors");
  for (Index = 0; Index < 4; Index++) {
    Offset = 54 + (UINT32)Index * 18;
    Desc   = &E[Offset];
    if ((Desc[0] != 0) || (Desc[1] != 0)) {
      DetailedTiming (Info, Desc, Offset, Index);
      continue;
    }

    switch (Desc[3]) {
      case 0xFC:
        DescText (Desc, Text, sizeof (Text));
        F (Info, 1, Offset, 18, RwEmStrong, "Monitor name", Text);
        break;
      case 0xFF:
        DescText (Desc, Text, sizeof (Text));
        F (Info, 1, Offset, 18, RwEmNormal, "Monitor serial", Text);
        break;
      case 0xFE:
        DescText (Desc, Text, sizeof (Text));
        F (Info, 1, Offset, 18, RwEmNormal, "Text", Text);
        break;
      case 0xFD:
        AsciiSPrint (Text, sizeof (Text), "V %d-%d Hz, H %d-%d kHz, max %d MHz", Desc[5], Desc[6], Desc[7], Desc[8], Desc[9] * 10);
        F (Info, 1, Offset, 18, RwEmNormal, "Range limits", Text);
        break;
      case 0x10:
        F (Info, 1, Offset, 18, RwEmDim, "Dummy", "");
        break;
      default:
        AsciiSPrint (Text, sizeof (Text), "tag %02Xh", Desc[3]);
        F (Info, 1, Offset, 18, RwEmDim, "Descriptor", Text);
        break;
    }
  }

  RwInfoSection (Info, 0, "Extensions (%d)", E[126]);
  AsciiSPrint (Text, sizeof (Text), "%02Xh %a", E[127], (Sum (E, 128) == 0) ? "OK" : "BAD");
  F (Info, 1, 127, 1, (Sum (E, 128) == 0) ? RwEmNormal : RwEmAlert, "Base block checksum", Text);
  for (Ext = 1; (Ext <= E[126]) && ((Ext + 1) * 128 <= Size); Ext++) {
    Desc = &E[Ext * 128];
    AsciiSPrint (
      Text,
      sizeof (Text),
      "%a (tag %02Xh, rev %d), checksum %a",
      (Desc[0] == 0x02) ? "CTA-861" : ((Desc[0] == 0x70) ? "DisplayID" : ((Desc[0] == 0xF0) ? "Block map" : "Extension")),
      Desc[0],
      Desc[1],
      (Sum (Desc, 128) == 0) ? "OK" : "BAD"
      );
    RwInfoSetReg (RwInfoField (Info, 1, (Sum (Desc, 128) == 0) ? RwEmNormal : RwEmAlert, "Block", "%d: %a", Ext, Text), (UINT32)(Ext * 128), 128);
  }

  RwInfoSection (Info, 0, "Raw (%d bytes)", Size);
  RwInfoAddHex (Info, 1, E, Size, 0);
}

typedef struct {
  CONST RW_EDID    *Edid;
} EDID_DS;

STATIC
EFI_STATUS
EFIAPI
EdidDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  CONST RW_EDID  *Edid;

  Edid = ((EDID_DS *)Source->Ctx)->Edid;
  if (Address >= Edid->Size) {
    *Value = 0;
    return EFI_INVALID_PARAMETER;
  }

  *Value = Edid->Data[Address];
  return EFI_SUCCESS;
}

VOID
RwEdidSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_EDID   *Edid
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiSPrint (Source->Name, sizeof (Source->Name), "EDID %a (read-only copy)", Edid->Name);
  Source->Base       = 0;
  Source->Limit      = Edid->Size - 1;
  Source->AddrDigits = 3;
  Source->WidthMask  = RW_DS_WIDTH_8;
  Source->Read       = EdidDsRead;
  ((EDID_DS *)Source->Ctx)->Edid = Edid;
}
