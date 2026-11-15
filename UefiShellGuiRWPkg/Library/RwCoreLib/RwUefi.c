/** @file
  RwCoreLib UEFI module: GUID names, handle database, system table,
  graphics output and TPM 2.0 event log. Read only.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/DevicePathLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Tcg2Protocol.h>

#define DB_MAX_HANDLES    4096
#define TPM_MAX_EVENTS    1024
#define TPM_CC_PCR_READ   0x0000017E
#define TPM_ST_NO_SESSION 0x8001

//
// ---- GUID names ----------------------------------------------------------------
//

CONST CHAR8 *
RwGuidName (
  IN CONST EFI_GUID  *Guid
  )
{
  UINTN  Index;

  for (Index = 0; Index < gRwGuidNameCount; Index++) {
    if (CompareGuid (&gRwGuidNames[Index].Guid, Guid)) {
      return gRwGuidNames[Index].Name;
    }
  }

  return NULL;
}

VOID
RwGuidText (
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR8           *Buffer,
  IN  UINTN           BufferSize
  )
{
  CONST CHAR8  *Name;

  Name = RwGuidName (Guid);
  if (Name != NULL) {
    AsciiStrCpyS (Buffer, BufferSize, Name);
  } else {
    AsciiSPrint (Buffer, BufferSize, "%g", Guid);
  }
}

//
// ---- Handle database ------------------------------------------------------------
//

typedef struct {
  EFI_HANDLE    *Handles;
  UINTN         Count;
} HANDLE_SET;

/** 1-based number of Handle in the set (0 when not there). **/
STATIC
UINTN
HandleNumber (
  IN CONST HANDLE_SET  *Set,
  IN EFI_HANDLE        Handle
  )
{
  UINTN  Index;

  for (Index = 0; Index < Set->Count; Index++) {
    if (Set->Handles[Index] == Handle) {
      return Index + 1;
    }
  }

  return 0;
}

STATIC
VOID
DevicePathText (
  IN  EFI_DEVICE_PATH_PROTOCOL  *Path,
  OUT CHAR8                     *Buffer,
  IN  UINTN                     BufferSize
  )
{
  CHAR16  *Text;

  Buffer[0] = '\0';
  if (Path == NULL) {
    return;
  }

  Text = ConvertDevicePathToText (Path, FALSE, TRUE);
  if (Text != NULL) {
    RwUnicodeToAscii (Text, Buffer, BufferSize);
    FreePool (Text);
  }
}

STATIC
BOOLEAN
DriverName (
  IN  EFI_HANDLE  Handle,
  OUT CHAR8       *Buffer,
  IN  UINTN       BufferSize
  )
{
  EFI_COMPONENT_NAME2_PROTOCOL  *Cn2;
  CHAR16                        *Name;

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiComponentName2ProtocolGuid, (VOID **)&Cn2))) {
    return FALSE;
  }

  if (EFI_ERROR (Cn2->GetDriverName (Cn2, "en", &Name)) &&
      EFI_ERROR (Cn2->GetDriverName (Cn2, Cn2->SupportedLanguages, &Name)))
  {
    return FALSE;
  }

  RwUnicodeToAscii (Name, Buffer, BufferSize);
  return TRUE;
}

/** File name of a loaded image (last file path node), else "". **/
STATIC
VOID
ImageName (
  IN  EFI_LOADED_IMAGE_PROTOCOL  *Image,
  OUT CHAR8                      *Buffer,
  IN  UINTN                      BufferSize
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *Last;

  Buffer[0] = '\0';
  Last      = NULL;
  for (Node = Image->FilePath; (Node != NULL) && !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    Last = Node;
  }

  if (Last != NULL) {
    Node = AppendDevicePathNode (NULL, Last);
    DevicePathText (Node, Buffer, BufferSize);
    if (Node != NULL) {
      FreePool (Node);
    }
  }
}

/** One line telling what a handle is. **/
STATIC
VOID
Describe (
  IN  EFI_HANDLE  Handle,
  OUT CHAR8       *Buffer,
  IN  UINTN       BufferSize
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image;
  EFI_DEVICE_PATH_PROTOCOL   *Path;

  Buffer[0] = '\0';
  if (DriverName (Handle, Buffer, BufferSize)) {
    return;
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image))) {
    ImageName (Image, Buffer, BufferSize);
    if (Buffer[0] != '\0') {
      return;
    }
  }

  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path))) {
    DevicePathText (Path, Buffer, BufferSize);
  }
}

STATIC
CONST CHAR8 *
OpenAttrText (
  IN UINT32  Attributes
  )
{
  if ((Attributes & EFI_OPEN_PROTOCOL_EXCLUSIVE) != 0) {
    return ((Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) != 0) ? "BY_DRIVER EXCLUSIVE" : "EXCLUSIVE";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) != 0) {
    return "BY_DRIVER";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) != 0) {
    return "BY_CHILD_CONTROLLER";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_GET_PROTOCOL) != 0) {
    return "GET_PROTOCOL";
  }

  if ((Attributes & EFI_OPEN_PROTOCOL_TEST_PROTOCOL) != 0) {
    return "TEST_PROTOCOL";
  }

  return "BY_HANDLE_PROTOCOL";
}

/** Details of one protocol instance on Handle, "" when none. **/
STATIC
VOID
ProtocolDetail (
  IN  EFI_HANDLE      Handle,
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR8           *Buffer,
  IN  UINTN           BufferSize
  )
{
  VOID                          *Interface;
  EFI_LOADED_IMAGE_PROTOCOL     *Image;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  CHAR8                         Name[96];

  Buffer[0] = '\0';
  if (EFI_ERROR (gBS->HandleProtocol (Handle, (EFI_GUID *)Guid, &Interface)) || (Interface == NULL)) {
    return;
  }

  if (CompareGuid (Guid, &gEfiDevicePathProtocolGuid) || CompareGuid (Guid, &gEfiLoadedImageDevicePathProtocolGuid)) {
    DevicePathText ((EFI_DEVICE_PATH_PROTOCOL *)Interface, Buffer, BufferSize);
  } else if (CompareGuid (Guid, &gEfiLoadedImageProtocolGuid)) {
    Image = (EFI_LOADED_IMAGE_PROTOCOL *)Interface;
    ImageName (Image, Name, sizeof (Name));
    AsciiSPrint (Buffer, BufferSize, "%a  base %lX, %ld bytes", Name, (UINT64)(UINTN)Image->ImageBase, Image->ImageSize);
  } else if (CompareGuid (Guid, &gEfiComponentName2ProtocolGuid)) {
    DriverName (Handle, Buffer, BufferSize);
  } else if (CompareGuid (Guid, &gEfiGraphicsOutputProtocolGuid)) {
    Gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)Interface;
    if ((Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
      AsciiSPrint (Buffer, BufferSize, "mode %d, %dx%d", Gop->Mode->Mode, Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution);
    }
  } else {
    AsciiSPrint (Buffer, BufferSize, "interface %lX", (UINT64)(UINTN)Interface);
  }
}

STATIC
VOID
AddOpeners (
  IN OUT RW_INFO           *Info,
  IN     CONST HANDLE_SET  *Set,
  IN     EFI_HANDLE        Handle,
  IN     EFI_GUID          *Guid
  )
{
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *Open;
  UINTN                                Count;
  UINTN                                Index;
  CHAR8                                Name[32];
  CHAR8                                Who[96];

  if (EFI_ERROR (gBS->OpenProtocolInformation (Handle, Guid, &Open, &Count))) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    AsciiSPrint (Name, sizeof (Name), "Opened by %X", HandleNumber (Set, Open[Index].AgentHandle));
    Who[0] = '\0';
    DriverName (Open[Index].AgentHandle, Who, sizeof (Who));
    if (Open[Index].ControllerHandle != NULL) {
      RwInfoField (
        Info,
        2,
        RwEmDim,
        Name,
        "%a, controller %X%a%a",
        OpenAttrText (Open[Index].Attributes),
        HandleNumber (Set, Open[Index].ControllerHandle),
        (Who[0] != '\0') ? "  " : "",
        Who
        );
    } else {
      RwInfoField (Info, 2, RwEmDim, Name, "%a%a%a", OpenAttrText (Open[Index].Attributes), (Who[0] != '\0') ? "  " : "", Who);
    }
  }

  FreePool (Open);
}

typedef struct {
  EFI_GUID    Guid;
  UINTN       Count;
} PROTO_COUNT;

STATIC
EFI_STATUS
DbByProtocol (
  IN OUT RW_INFO           *Info,
  IN     CONST HANDLE_SET  *Set,
  OUT    UINTN             *Protocols
  )
{
  PROTO_COUNT  *Protos;
  PROTO_COUNT  *Grown;
  UINTN        ProtoCount;
  UINTN        ProtoCap;
  UINTN        H;
  UINTN        P;
  UINTN        Index;
  EFI_GUID     **Guids;
  UINTN        GuidCount;
  CHAR8        Name[96];
  CHAR8        Text[160];
  RW_INFO_ROW  *Row;

  Protos     = NULL;
  ProtoCount = 0;
  ProtoCap   = 0;
  for (H = 0; H < Set->Count; H++) {
    if (EFI_ERROR (gBS->ProtocolsPerHandle (Set->Handles[H], &Guids, &GuidCount))) {
      continue;
    }

    for (P = 0; P < GuidCount; P++) {
      for (Index = 0; Index < ProtoCount; Index++) {
        if (CompareGuid (&Protos[Index].Guid, Guids[P])) {
          break;
        }
      }

      if (Index == ProtoCount) {
        if (ProtoCount == ProtoCap) {
          Grown = ReallocatePool (ProtoCap * sizeof (PROTO_COUNT), (ProtoCap + 64) * sizeof (PROTO_COUNT), Protos);
          if (Grown == NULL) {
            break;
          }

          Protos    = Grown;
          ProtoCap += 64;
        }

        CopyGuid (&Protos[ProtoCount].Guid, Guids[P]);
        Protos[ProtoCount].Count = 0;
        ProtoCount++;
      }

      Protos[Index].Count++;
    }

    FreePool (Guids);
  }

  //
  // Named protocols first, alphabetically; unnamed GUIDs after them.
  //
  for (P = 0; P + 1 < ProtoCount; P++) {
    for (Index = P + 1; Index < ProtoCount; Index++) {
      CONST CHAR8  *A;
      CONST CHAR8  *B;
      PROTO_COUNT  Tmp;

      A = RwGuidName (&Protos[P].Guid);
      B = RwGuidName (&Protos[Index].Guid);
      if (((A == NULL) && (B != NULL)) || ((A != NULL) && (B != NULL) && (AsciiStriCmp (A, B) > 0))) {
        Tmp            = Protos[P];
        Protos[P]      = Protos[Index];
        Protos[Index]  = Tmp;
      }
    }
  }

  for (P = 0; P < ProtoCount; P++) {
    RwGuidText (&Protos[P].Guid, Name, sizeof (Name));
    RwInfoField (Info, 0, RwEmStrong, Name, "%d handle%a", Protos[P].Count, (Protos[P].Count == 1) ? "" : "s");
    for (H = 0; H < Set->Count; H++) {
      VOID  *Interface;

      if (EFI_ERROR (gBS->HandleProtocol (Set->Handles[H], &Protos[P].Guid, &Interface))) {
        continue;
      }

      AsciiSPrint (Name, sizeof (Name), "Handle %X", H + 1);
      Describe (Set->Handles[H], Text, sizeof (Text));
      Row = RwInfoField (Info, 1, RwEmNormal, Name, "%a", Text);
      RwInfoSetTag (Row, (INT32)(H + 1));
    }
  }

  if (Protocols != NULL) {
    *Protocols = ProtoCount;
  }

  if (Protos != NULL) {
    FreePool (Protos);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RwHandleDbDecode (
  IN     BOOLEAN  ByProto,
  IN OUT RW_INFO  *Info,
  OUT    UINTN    *Handles,
  OUT    UINTN    *Protocols
  )
{
  HANDLE_SET   Set;
  UINTN        H;
  UINTN        P;
  EFI_GUID     **Guids;
  UINTN        GuidCount;
  CHAR8        Name[96];
  CHAR8        Text[256];
  RW_INFO_ROW  *Row;
  EFI_STATUS   Status;
  UINTN        Distinct;

  if (Handles != NULL) {
    *Handles = 0;
  }

  if (Protocols != NULL) {
    *Protocols = 0;
  }

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Set.Count, &Set.Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Set.Count = MIN (Set.Count, DB_MAX_HANDLES);
  if (Handles != NULL) {
    *Handles = Set.Count;
  }

  if (ByProto) {
    Status = DbByProtocol (Info, &Set, Protocols);
    FreePool (Set.Handles);
    return Status;
  }

  for (H = 0; H < Set.Count; H++) {
    AsciiSPrint (Name, sizeof (Name), "Handle %X", H + 1);
    Describe (Set.Handles[H], Text, sizeof (Text));
    Row = RwInfoField (Info, 0, RwEmStrong, Name, "%a", Text);
    RwInfoSetTag (Row, (INT32)(H + 1));
    if (EFI_ERROR (gBS->ProtocolsPerHandle (Set.Handles[H], &Guids, &GuidCount))) {
      continue;
    }

    for (P = 0; P < GuidCount; P++) {
      RwGuidText (Guids[P], Name, sizeof (Name));
      ProtocolDetail (Set.Handles[H], Guids[P], Text, sizeof (Text));
      RwInfoField (Info, 1, (RwGuidName (Guids[P]) != NULL) ? RwEmNormal : RwEmDim, Name, "%a", Text);
      AddOpeners (Info, &Set, Set.Handles[H], Guids[P]);
    }

    FreePool (Guids);
  }

  if (Protocols != NULL) {
    //
    // Count distinct protocols cheaply through the by-protocol pass.
    //
    RW_INFO  Scratch;

    RwInfoInit (&Scratch);
    Distinct = 0;
    DbByProtocol (&Scratch, &Set, &Distinct);
    RwInfoFree (&Scratch);
    *Protocols = Distinct;
  }

  FreePool (Set.Handles);
  return EFI_SUCCESS;
}

//
// ---- System table and graphics output ------------------------------------------
//

STATIC
CONST CHAR8 *
PixelFormatName (
  IN EFI_GRAPHICS_PIXEL_FORMAT  Format
  )
{
  switch (Format) {
    case PixelRedGreenBlueReserved8BitPerColor:
      return "RGBx 8:8:8:8";
    case PixelBlueGreenRedReserved8BitPerColor:
      return "BGRx 8:8:8:8";
    case PixelBitMask:
      return "Bit mask";
    case PixelBltOnly:
      return "Blt only (no frame buffer)";
    default:
      return "Unknown";
  }
}

STATIC
VOID
GopDecode (
  IN OUT RW_INFO  *Info
  )
{
  EFI_HANDLE                            *Handles;
  UINTN                                 Count;
  UINTN                                 G;
  UINT32                                Mode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  UINTN                                 Size;
  CHAR8                                 Name[32];
  CHAR8                                 Path[160];
  EFI_DEVICE_PATH_PROTOCOL              *Dp;

  RwInfoSection (Info, 0, "Graphics output");
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    RwInfoText (Info, 1, RwEmDim, "No graphics output protocol");
    return;
  }

  for (G = 0; G < Count; G++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[G], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
      continue;
    }

    Path[0] = '\0';
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[G], &gEfiDevicePathProtocolGuid, (VOID **)&Dp))) {
      DevicePathText (Dp, Path, sizeof (Path));
    }

    AsciiSPrint (Name, sizeof (Name), "GOP %d", G);
    RwInfoField (Info, 1, RwEmStrong, Name, "%a", (Path[0] != '\0') ? Path : "(no device path)");
    RwInfoField (
      Info,
      2,
      RwEmStrong,
      "Current mode",
      "%d of %d: %dx%d, %a, %d pixels per scan line",
      Gop->Mode->Mode,
      Gop->Mode->MaxMode,
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      PixelFormatName (Gop->Mode->Info->PixelFormat),
      Gop->Mode->Info->PixelsPerScanLine
      );
    if (Gop->Mode->Info->PixelFormat == PixelBitMask) {
      RwInfoField (
        Info,
        2,
        RwEmNormal,
        "Pixel masks",
        "R %08X G %08X B %08X",
        Gop->Mode->Info->PixelInformation.RedMask,
        Gop->Mode->Info->PixelInformation.GreenMask,
        Gop->Mode->Info->PixelInformation.BlueMask
        );
    }

    RwInfoSetTag (
      RwInfoField (Info, 2, RwEmNormal, "Frame buffer (Enter: memory)", "%016lX, %ld bytes", Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize),
      (INT32)(RW_SYSTEM_TAG_FB + G)
      );
    for (Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
      if (EFI_ERROR (Gop->QueryMode (Gop, Mode, &Size, &ModeInfo))) {
        continue;
      }

      AsciiSPrint (Name, sizeof (Name), "Mode %d", Mode);
      RwInfoField (
        Info,
        3,
        (Mode == Gop->Mode->Mode) ? RwEmStrong : RwEmNormal,
        Name,
        "%dx%d, %a%a",
        ModeInfo->HorizontalResolution,
        ModeInfo->VerticalResolution,
        PixelFormatName (ModeInfo->PixelFormat),
        (Mode == Gop->Mode->Mode) ? "  (current)" : ""
        );
      FreePool (ModeInfo);
    }
  }

  FreePool (Handles);
}

VOID
RwSystemDecode (
  IN OUT RW_INFO  *Info
  )
{
  UINTN  Index;
  CHAR8  Text[128];

  RwInfoSection (Info, 0, "System table");
  RwUnicodeToAscii (gST->FirmwareVendor, Text, sizeof (Text));
  RwInfoField (Info, 1, RwEmStrong, "Firmware vendor", "%a", Text);
  RwInfoField (Info, 1, RwEmStrong, "Firmware revision", "%08X", gST->FirmwareRevision);
  if ((gST->Hdr.Revision & 0xFFFF) % 10 != 0) {
    RwInfoField (Info, 1, RwEmNormal, "UEFI revision", "%d.%d.%d", gST->Hdr.Revision >> 16, (gST->Hdr.Revision & 0xFFFF) / 10, (gST->Hdr.Revision & 0xFFFF) % 10);
  } else {
    RwInfoField (Info, 1, RwEmNormal, "UEFI revision", "%d.%d", gST->Hdr.Revision >> 16, (gST->Hdr.Revision & 0xFFFF) / 10);
  }

  RwInfoField (Info, 1, RwEmNormal, "System table", "%016lX, %d bytes", (UINT64)(UINTN)gST, gST->Hdr.HeaderSize);
  RwInfoField (Info, 1, RwEmNormal, "Boot services", "%016lX, revision %08X", (UINT64)(UINTN)gST->BootServices, gST->BootServices->Hdr.Revision);
  RwInfoField (Info, 1, RwEmNormal, "Runtime services", "%016lX, revision %08X", (UINT64)(UINTN)gST->RuntimeServices, gST->RuntimeServices->Hdr.Revision);

  RwInfoSection (Info, 0, "Configuration tables (%d, Enter: memory)", gST->NumberOfTableEntries);
  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    RwGuidText (&gST->ConfigurationTable[Index].VendorGuid, Text, sizeof (Text));
    RwInfoSetTag (
      RwInfoField (
        Info,
        1,
        (RwGuidName (&gST->ConfigurationTable[Index].VendorGuid) != NULL) ? RwEmNormal : RwEmDim,
        Text,
        "%016lX",
        (UINT64)(UINTN)gST->ConfigurationTable[Index].VendorTable
        ),
      (INT32)(RW_SYSTEM_TAG_CONFIG + Index)
      );
  }

  GopDecode (Info);
  RwTpmDecode (Info);
}

BOOLEAN
RwSystemTagAddress (
  IN  INT32   Tag,
  OUT UINT64  *Address
  )
{
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  BOOLEAN                       Found;

  if ((Tag >= RW_SYSTEM_TAG_CONFIG) && (Tag < RW_SYSTEM_TAG_CONFIG + (INT32)gST->NumberOfTableEntries)) {
    *Address = (UINT64)(UINTN)gST->ConfigurationTable[Tag - RW_SYSTEM_TAG_CONFIG].VendorTable;
    return TRUE;
  }

  if ((Tag >= RW_SYSTEM_TAG_FB) && (Tag < RW_SYSTEM_TAG_FB + 0x100)) {
    if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
      return FALSE;
    }

    Found = (BOOLEAN)(((UINTN)(Tag - RW_SYSTEM_TAG_FB) < Count) &&
                      !EFI_ERROR (gBS->HandleProtocol (Handles[Tag - RW_SYSTEM_TAG_FB], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) &&
                      (Gop->Mode != NULL));
    if (Found) {
      *Address = Gop->Mode->FrameBufferBase;
    }

    FreePool (Handles);
    return Found;
  }

  return FALSE;
}

//
// ---- TPM 2.0 ---------------------------------------------------------------------
//

typedef struct {
  UINT16         AlgId;
  UINT32         Bit;      // EFI_TCG2_BOOT_HASH_ALG_*
  UINT16         Size;
  CONST CHAR8    *Name;
} TPM_ALG;

STATIC CONST TPM_ALG  mTpmAlgs[] = {
  { 0x0004, EFI_TCG2_BOOT_HASH_ALG_SHA1,    20, "SHA1"    },
  { 0x000B, EFI_TCG2_BOOT_HASH_ALG_SHA256,  32, "SHA256"  },
  { 0x000C, EFI_TCG2_BOOT_HASH_ALG_SHA384,  48, "SHA384"  },
  { 0x000D, EFI_TCG2_BOOT_HASH_ALG_SHA512,  64, "SHA512"  },
  { 0x0012, EFI_TCG2_BOOT_HASH_ALG_SM3_256, 32, "SM3_256" },
};

STATIC
CONST TPM_ALG *
AlgById (
  IN UINT16  AlgId
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mTpmAlgs); Index++) {
    if (mTpmAlgs[Index].AlgId == AlgId) {
      return &mTpmAlgs[Index];
    }
  }

  return NULL;
}

typedef struct {
  UINT32         Type;
  CONST CHAR8    *Name;
} EVENT_NAME;

STATIC CONST EVENT_NAME  mEventNames[] = {
  { 0x00000000, "EV_PREBOOT_CERT"                  },
  { 0x00000001, "EV_POST_CODE"                     },
  { 0x00000003, "EV_NO_ACTION"                     },
  { 0x00000004, "EV_SEPARATOR"                     },
  { 0x00000005, "EV_ACTION"                        },
  { 0x00000006, "EV_EVENT_TAG"                     },
  { 0x00000007, "EV_S_CRTM_CONTENTS"               },
  { 0x00000008, "EV_S_CRTM_VERSION"                },
  { 0x00000009, "EV_CPU_MICROCODE"                 },
  { 0x0000000A, "EV_PLATFORM_CONFIG_FLAGS"         },
  { 0x0000000B, "EV_TABLE_OF_DEVICES"              },
  { 0x0000000C, "EV_COMPACT_HASH"                  },
  { 0x0000000D, "EV_IPL"                           },
  { 0x0000000E, "EV_IPL_PARTITION_DATA"            },
  { 0x0000000F, "EV_NONHOST_CODE"                  },
  { 0x00000010, "EV_NONHOST_CONFIG"                },
  { 0x00000011, "EV_NONHOST_INFO"                  },
  { 0x00000012, "EV_OMIT_BOOT_DEVICE_EVENTS"       },
  { 0x80000001, "EV_EFI_VARIABLE_DRIVER_CONFIG"    },
  { 0x80000002, "EV_EFI_VARIABLE_BOOT"             },
  { 0x80000003, "EV_EFI_BOOT_SERVICES_APPLICATION" },
  { 0x80000004, "EV_EFI_BOOT_SERVICES_DRIVER"      },
  { 0x80000005, "EV_EFI_RUNTIME_SERVICES_DRIVER"   },
  { 0x80000006, "EV_EFI_GPT_EVENT"                 },
  { 0x80000007, "EV_EFI_ACTION"                    },
  { 0x80000008, "EV_EFI_PLATFORM_FIRMWARE_BLOB"    },
  { 0x80000009, "EV_EFI_HANDOFF_TABLES"            },
  { 0x8000000A, "EV_EFI_PLATFORM_FIRMWARE_BLOB2"   },
  { 0x8000000B, "EV_EFI_HANDOFF_TABLES2"           },
  { 0x8000000C, "EV_EFI_VARIABLE_BOOT2"            },
  { 0x80000010, "EV_EFI_HCRTM_EVENT"               },
  { 0x800000E0, "EV_EFI_VARIABLE_AUTHORITY"        },
  { 0x800000E1, "EV_EFI_SPDM_FIRMWARE_BLOB"        },
  { 0x800000E2, "EV_EFI_SPDM_FIRMWARE_CONFIG"      },
};

STATIC
CONST CHAR8 *
EventName (
  IN UINT32  Type
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mEventNames); Index++) {
    if (mEventNames[Index].Type == Type) {
      return mEventNames[Index].Name;
    }
  }

  return "Unknown event";
}

STATIC
VOID
HexText (
  IN  CONST UINT8  *Data,
  IN  UINTN        Size,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN  Index;

  Buffer[0] = '\0';
  for (Index = 0; (Index < Size) && ((Index + 1) * 2 < BufferSize); Index++) {
    AsciiSPrint (Buffer + Index * 2, BufferSize - Index * 2, "%02x", Data[Index]);
  }
}

STATIC
UINT16
Be16 (
  IN CONST UINT8  *P
  )
{
  return (UINT16)((P[0] << 8) | P[1]);
}

STATIC
UINT32
Be32 (
  IN CONST UINT8  *P
  )
{
  return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) | P[3];
}

STATIC
VOID
PutBe16 (
  OUT UINT8   *P,
  IN  UINT16  V
  )
{
  P[0] = (UINT8)(V >> 8);
  P[1] = (UINT8)V;
}

STATIC
VOID
PutBe32 (
  OUT UINT8   *P,
  IN  UINT32  V
  )
{
  P[0] = (UINT8)(V >> 24);
  P[1] = (UINT8)(V >> 16);
  P[2] = (UINT8)(V >> 8);
  P[3] = (UINT8)V;
}

/**
  TPM2_PCR_Read of the 8 PCRs of Group (0: PCR 0-7, 1: 8-15, 2: 16-23) in
  one bank. Digests go to Out[pcr] (Alg->Size bytes each).
**/
STATIC
EFI_STATUS
PcrRead (
  IN  EFI_TCG2_PROTOCOL  *Tcg2,
  IN  CONST TPM_ALG      *Alg,
  IN  UINTN              Group,
  OUT UINT8              Out[24][64],
  OUT BOOLEAN            Valid[24]
  )
{
  UINT8        Cmd[20];
  UINT8        Rsp[1024];
  EFI_STATUS   Status;
  UINT32       Size;
  UINT32       SelCount;
  UINT8        SelSize;
  UINT8        Select[4];
  CONST UINT8  *P;
  CONST UINT8  *End;
  UINT32       Digests;
  UINTN        Pcr;
  UINT16       DigestSize;

  ZeroMem (Cmd, sizeof (Cmd));
  PutBe16 (&Cmd[0], TPM_ST_NO_SESSION);
  PutBe32 (&Cmd[2], sizeof (Cmd));
  PutBe32 (&Cmd[6], TPM_CC_PCR_READ);
  PutBe32 (&Cmd[10], 1);               // one selection
  PutBe16 (&Cmd[14], Alg->AlgId);
  Cmd[16]             = 3;             // sizeofSelect
  Cmd[17 + Group]     = 0xFF;
  ZeroMem (Rsp, sizeof (Rsp));
  Status = Tcg2->SubmitCommand (Tcg2, sizeof (Cmd), Cmd, sizeof (Rsp), Rsp);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = Be32 (&Rsp[2]);
  if ((Size < 18) || (Size > sizeof (Rsp)) || (Be32 (&Rsp[6]) != 0)) {
    return EFI_DEVICE_ERROR;
  }

  End      = Rsp + Size;
  P        = &Rsp[14];                 // after header and pcrUpdateCounter
  SelCount = Be32 (P);
  P       += 4;
  ZeroMem (Select, sizeof (Select));
  for ( ; SelCount > 0 && P + 3 <= End; SelCount--) {
    SelSize = P[2];
    if (P + 3 + SelSize > End) {
      return EFI_DEVICE_ERROR;
    }

    if (Be16 (P) == Alg->AlgId) {
      CopyMem (Select, P + 3, MIN (SelSize, sizeof (Select)));
    }

    P += 3 + SelSize;
  }

  if (P + 4 > End) {
    return EFI_DEVICE_ERROR;
  }

  Digests = Be32 (P);
  P      += 4;
  for (Pcr = 0; (Pcr < 24) && (Digests > 0); Pcr++) {
    if ((Select[Pcr / 8] & (1 << (Pcr % 8))) == 0) {
      continue;
    }

    if (P + 2 > End) {
      break;
    }

    DigestSize = Be16 (P);
    if ((P + 2 + DigestSize > End) || (DigestSize > 64)) {
      break;
    }

    CopyMem (Out[Pcr], P + 2, DigestSize);
    Valid[Pcr] = TRUE;
    P         += 2 + DigestSize;
    Digests--;
  }

  return EFI_SUCCESS;
}

/** Event data in one line for the log view. **/
STATIC
VOID
EventDataText (
  IN  UINT32       Type,
  IN  CONST UINT8  *Data,
  IN  UINT32       Size,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN   Index;
  UINT64  NameLength;
  UINT64  DataLength;
  CHAR8   Name[64];
  CHAR8   Var[64];
  UINT64  PathLength;
  BOOLEAN Printable;
  CHAR16  Wide[64];

  Buffer[0] = '\0';
  switch (Type) {
    case 0x80000001:
    case 0x80000002:
    case 0x8000000C:
    case 0x800000E0:
      //
      // UEFI_VARIABLE_DATA: GUID, name length (chars), data length, name, data.
      //
      if (Size < 32) {
        break;
      }

      NameLength = ReadUnaligned64 ((CONST UINT64 *)&Data[16]);
      DataLength = ReadUnaligned64 ((CONST UINT64 *)&Data[24]);
      if ((NameLength == 0) || (NameLength > 63) || (32 + NameLength * 2 > Size)) {
        break;
      }

      CopyMem (Wide, &Data[32], (UINTN)NameLength * 2);
      Wide[NameLength] = L'\0';
      RwUnicodeToAscii (Wide, Var, sizeof (Var));
      RwGuidText ((CONST EFI_GUID *)Data, Name, sizeof (Name));
      AsciiSPrint (Buffer, BufferSize, "%a:%a, %ld bytes", Name, Var, DataLength);
      return;

    case 0x80000003:
    case 0x80000004:
    case 0x80000005:
      //
      // UEFI_IMAGE_LOAD_EVENT: location, length, link address, path length, path.
      //
      if (Size < 32) {
        break;
      }

      PathLength = ReadUnaligned64 ((CONST UINT64 *)&Data[24]);
      if ((PathLength == 0) || (32 + PathLength > Size)) {
        AsciiSPrint (Buffer, BufferSize, "image at %lX, %ld bytes", ReadUnaligned64 ((CONST UINT64 *)Data), ReadUnaligned64 ((CONST UINT64 *)&Data[8]));
        return;
      }

      {
        VOID  *Copy;

        Copy = AllocateCopyPool ((UINTN)PathLength, &Data[32]);
        if (Copy != NULL) {
          if (IsDevicePathValid ((EFI_DEVICE_PATH_PROTOCOL *)Copy, (UINTN)PathLength)) {
            DevicePathText ((EFI_DEVICE_PATH_PROTOCOL *)Copy, Buffer, BufferSize);
          }

          FreePool (Copy);
        }
      }

      if (Buffer[0] == '\0') {
        AsciiSPrint (Buffer, BufferSize, "image at %lX, %ld bytes", ReadUnaligned64 ((CONST UINT64 *)Data), ReadUnaligned64 ((CONST UINT64 *)&Data[8]));
      }

      return;

    case 0x00000004:
      if (Size == 4) {
        AsciiSPrint (Buffer, BufferSize, "%08X", ReadUnaligned32 ((CONST UINT32 *)Data));
        return;
      }

      break;

    case 0x00000008:
      //
      // CRTM version: UCS-2.
      //
      if ((Size >= 2) && (Size <= sizeof (Wide)) && (Data[1] == 0)) {
        ZeroMem (Wide, sizeof (Wide));
        CopyMem (Wide, Data, MIN (Size, sizeof (Wide) - 2));
        RwUnicodeToAscii (Wide, Buffer, BufferSize);
        return;
      }

      break;

    default:
      break;
  }

  //
  // Printable ASCII (EV_EFI_ACTION, EV_POST_CODE strings), else hex.
  //
  Printable = (BOOLEAN)(Size > 0);
  for (Index = 0; Index < Size; Index++) {
    if (((Data[Index] < 0x20) || (Data[Index] > 0x7E)) && !((Data[Index] == 0) && (Index == Size - 1))) {
      Printable = FALSE;
      break;
    }
  }

  if (Printable) {
    for (Index = 0; (Index < Size) && (Index + 1 < BufferSize) && (Data[Index] != 0); Index++) {
      Buffer[Index] = (CHAR8)Data[Index];
    }

    Buffer[Index] = '\0';
    return;
  }

  HexText (Data, MIN (Size, 16), Buffer, BufferSize);
  AsciiSPrint (Buffer + AsciiStrLen (Buffer), BufferSize - AsciiStrLen (Buffer), "%a (%d bytes)", (Size > 16) ? "..." : "", Size);
}

STATIC
VOID
EventLogDecode (
  IN OUT RW_INFO            *Info,
  IN     EFI_TCG2_PROTOCOL  *Tcg2
  )
{
  EFI_PHYSICAL_ADDRESS  Location;
  EFI_PHYSICAL_ADDRESS  Last;
  BOOLEAN               Truncated;
  CONST UINT8           *P;
  CONST UINT8           *LastP;
  UINT32                EventSize;
  UINT32                AlgCount;
  UINT16                SpecAlgs[8];
  UINT16                SpecSizes[8];
  UINT32                SpecCount;
  UINTN                 Index;
  UINTN                 Events;
  UINT32                DigestCount;
  UINT16                AlgId;
  UINT16                DigestSize;
  CHAR8                 Name[32];
  CHAR8                 Text[160];
  CONST TPM_ALG         *Alg;
  BOOLEAN               Bad;

  RwInfoSection (Info, 1, "Event log (TCG2)");
  if (EFI_ERROR (Tcg2->GetEventLog (Tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2, &Location, &Last, &Truncated)) || (Location == 0)) {
    RwInfoText (Info, 2, RwEmDim, "No TCG2 format event log");
    return;
  }

  if (Last == 0) {
    RwInfoText (Info, 2, RwEmDim, "Event log is empty");
    return;
  }

  //
  // First event: TCG_PCClientPCREvent (SHA1 format) holding the Spec ID
  // event, which lists the digest sizes of the crypto agile events.
  //
  P         = (CONST UINT8 *)(UINTN)Location;
  LastP     = (CONST UINT8 *)(UINTN)Last;
  EventSize = ReadUnaligned32 ((CONST UINT32 *)&P[28]);
  SpecCount = 0;
  if ((EventSize >= 29) && (CompareMem (&P[32], "Spec ID Event03", 15) == 0)) {
    AlgCount = ReadUnaligned32 ((CONST UINT32 *)&P[32 + 24]);
    for (Index = 0; (Index < AlgCount) && (Index < ARRAY_SIZE (SpecAlgs)); Index++) {
      SpecAlgs[Index]  = ReadUnaligned16 ((CONST UINT16 *)&P[32 + 28 + Index * 4]);
      SpecSizes[Index] = ReadUnaligned16 ((CONST UINT16 *)&P[32 + 30 + Index * 4]);
      SpecCount++;
    }
  }

  RwInfoField (Info, 2, RwEmNormal, "Location", "%016lX%a", Location, Truncated ? "  (truncated)" : "");
  Text[0] = '\0';
  for (Index = 0; Index < SpecCount; Index++) {
    Alg = AlgById (SpecAlgs[Index]);
    AsciiSPrint (Text + AsciiStrLen (Text), sizeof (Text) - AsciiStrLen (Text), "%a%a", (Index > 0) ? " " : "", (Alg != NULL) ? Alg->Name : "?");
  }

  RwInfoField (Info, 2, RwEmNormal, "Spec ID event", "%a", (SpecCount > 0) ? Text : "missing");
  if (SpecCount == 0) {
    return;
  }

  P     += 32 + EventSize;
  Events = 1;
  Bad    = FALSE;
  while ((P <= LastP) && (Events < TPM_MAX_EVENTS)) {
    AsciiSPrint (Name, sizeof (Name), "%3d PCR %2d", Events, ReadUnaligned32 ((CONST UINT32 *)P));
    DigestCount = ReadUnaligned32 ((CONST UINT32 *)&P[8]);
    if (DigestCount > 8) {
      Bad = TRUE;
      break;
    }

    //
    // Find where the event data starts: after every digest.
    //
    {
      CONST UINT8  *D;
      UINTN        K;

      D = &P[12];
      for (Index = 0; Index < DigestCount; Index++) {
        AlgId      = ReadUnaligned16 ((CONST UINT16 *)D);
        DigestSize = 0;
        for (K = 0; K < SpecCount; K++) {
          if (SpecAlgs[K] == AlgId) {
            DigestSize = SpecSizes[K];
          }
        }

        if (DigestSize == 0) {
          Bad = TRUE;
          break;
        }

        D += 2 + DigestSize;
      }

      if (Bad) {
        break;
      }

      EventSize = ReadUnaligned32 ((CONST UINT32 *)D);
      if (EventSize > SIZE_1MB) {
        Bad = TRUE;
        break;
      }

      EventDataText (ReadUnaligned32 ((CONST UINT32 *)&P[4]), D + 4, EventSize, Text, sizeof (Text));
      RwInfoField (Info, 2, RwEmStrong, Name, "%a  %a", EventName (ReadUnaligned32 ((CONST UINT32 *)&P[4])), Text);

      D = &P[12];
      for (Index = 0; Index < DigestCount; Index++) {
        AlgId      = ReadUnaligned16 ((CONST UINT16 *)D);
        DigestSize = 0;
        for (K = 0; K < SpecCount; K++) {
          if (SpecAlgs[K] == AlgId) {
            DigestSize = SpecSizes[K];
          }
        }

        Alg = AlgById (AlgId);
        HexText (D + 2, DigestSize, Text, sizeof (Text));
        RwInfoField (Info, 3, RwEmDim, (Alg != NULL) ? Alg->Name : "Digest", "%a", Text);
        D += 2 + DigestSize;
      }

      P = D + 4 + EventSize;
    }

    Events++;
  }

  if (Bad) {
    RwInfoText (Info, 2, RwEmAlert, "Malformed event %d: decoding stopped", Events);
  }

  RwInfoField (Info, 2, RwEmNormal, "Events", "%d", Events);
}

EFI_STATUS
RwTpmDecode (
  IN OUT RW_INFO  *Info
  )
{
  EFI_TCG2_PROTOCOL                 *Tcg2;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY  Cap;
  CHAR8                             Vendor[5];
  CHAR8                             Text[160];
  CHAR8                             Name[16];
  UINTN                             A;
  UINTN                             Group;
  UINTN                             Pcr;
  UINT8                             (*Digest)[64];
  BOOLEAN                           Valid[24];
  RW_STR                            Banks;

  RwInfoSection (Info, 0, "TPM");
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiTcg2ProtocolGuid, NULL, (VOID **)&Tcg2))) {
    RwInfoText (Info, 1, RwEmDim, "No TPM 2.0 (EFI_TCG2_PROTOCOL not found)");
    return EFI_NOT_FOUND;
  }

  ZeroMem (&Cap, sizeof (Cap));
  Cap.Size = (UINT8)sizeof (Cap);
  if (EFI_ERROR (Tcg2->GetCapability (Tcg2, &Cap)) || !Cap.TPMPresentFlag) {
    RwInfoText (Info, 1, RwEmDim, "No TPM 2.0 present");
    return EFI_NOT_FOUND;
  }

  //
  // The vendor ID is four ASCII characters, first character in the low byte.
  //
  Vendor[0] = (CHAR8)Cap.ManufacturerID;
  Vendor[1] = (CHAR8)(Cap.ManufacturerID >> 8);
  Vendor[2] = (CHAR8)(Cap.ManufacturerID >> 16);
  Vendor[3] = (CHAR8)(Cap.ManufacturerID >> 24);
  Vendor[4] = '\0';
  for (A = 0; A < 4; A++) {
    if ((Vendor[A] < 0x20) || (Vendor[A] > 0x7E)) {
      Vendor[A] = ' ';
    }
  }

  RwInfoField (Info, 1, RwEmStrong, "Manufacturer", "%a (%08X)", Vendor, Cap.ManufacturerID);
  RwInfoField (Info, 1, RwEmNormal, "TCG2 protocol", "%d.%d", Cap.ProtocolVersion.Major, Cap.ProtocolVersion.Minor);
  RwInfoField (Info, 1, RwEmNormal, "Max command / response", "%d / %d bytes", Cap.MaxCommandSize, Cap.MaxResponseSize);
  RwStrInit (&Banks);
  for (A = 0; A < ARRAY_SIZE (mTpmAlgs); A++) {
    if ((Cap.ActivePcrBanks & mTpmAlgs[A].Bit) != 0) {
      if (Banks.Len != 0) {
        RwStrAppendChar (&Banks, ' ');
      }

      RwStrAppend (&Banks, mTpmAlgs[A].Name);
    }
  }

  RwInfoField (Info, 1, RwEmNormal, "Active PCR banks", "%a", (Banks.Buf != NULL) ? Banks.Buf : "none");
  RwStrFree (&Banks);

  Digest = AllocateZeroPool (24 * 64);
  if (Digest != NULL) {
    for (A = 0; A < ARRAY_SIZE (mTpmAlgs); A++) {
      if ((Cap.ActivePcrBanks & mTpmAlgs[A].Bit) == 0) {
        continue;
      }

      RwInfoSection (Info, 1, "PCR values (%a)", mTpmAlgs[A].Name);
      ZeroMem (Valid, sizeof (Valid));
      for (Group = 0; Group < 3; Group++) {
        if (EFI_ERROR (PcrRead (Tcg2, &mTpmAlgs[A], Group, Digest, Valid))) {
          break;
        }
      }

      for (Pcr = 0; Pcr < 24; Pcr++) {
        if (!Valid[Pcr]) {
          continue;
        }

        AsciiSPrint (Name, sizeof (Name), "PCR %2d", Pcr);
        HexText (Digest[Pcr], mTpmAlgs[A].Size, Text, sizeof (Text));
        RwInfoField (Info, 2, RwEmNormal, Name, "%a", Text);
      }
    }

    FreePool (Digest);
  }

  EventLogDecode (Info, Tcg2);
  return EFI_SUCCESS;
}
