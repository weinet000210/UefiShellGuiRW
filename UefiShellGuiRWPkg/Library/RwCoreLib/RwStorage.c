/** @file
  RwCoreLib storage: ATA / ATAPI IDENTIFY and NVMe Identify through the
  firmware's pass-thru protocols, the read-only health data (ATA SMART READ
  DATA / THRESHOLDS, NVMe log pages 01h-03h), and their decoders.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/DevicePathLib.h>
#include <Protocol/AtaPassThru.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/DevicePath.h>

#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_IDENTIFY_PACKET  0xA1
#define ATA_TIMEOUT_100NS        30000000ULL      // 3 s
#define NVME_TIMEOUT_100NS       30000000ULL
#define NVME_OPC_IDENTIFY        0x06
#define NVME_CNS_NAMESPACE       0
#define NVME_CNS_CONTROLLER      1
#define ID_ATA_SIZE              512
#define ID_NVME_SIZE             4096
#define ATA_CMD_SMART            0xB0
#define ATA_SMART_READ_DATA      0xD0
#define ATA_SMART_READ_THRESH    0xD1
#define NVME_OPC_GET_LOG         0x02
#define NVME_LOG_ERROR           0x01
#define NVME_LOG_HEALTH          0x02
#define NVME_LOG_FW_SLOT         0x03

CONST CHAR8 *
RwStorageKindName (
  IN UINT8  Kind
  )
{
  switch (Kind) {
    case RwStorAta:      return "ATA";
    case RwStorAtapi:    return "ATAPI";
    case RwStorNvmeCtrl: return "NVMe controller";
    case RwStorNvmeNs:   return "NVMe namespace";
    default:             return "?";
  }
}

//
// ---- Helpers ---------------------------------------------------------------
//

STATIC
VOID
Location (
  IN  EFI_HANDLE  Handle,
  OUT CHAR8       *Buffer,
  IN  UINTN       BufferSize
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  CHAR16                    *Text;
  UINTN                     Len;
  UINTN                     Skip;

  Buffer[0] = '\0';
  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Dp))) {
    return;
  }

  Text = ConvertDevicePathToText (Dp, TRUE, TRUE);
  if (Text == NULL) {
    return;
  }

  //
  // Keep the tail (the part that names the controller) when it is long.
  //
  Len  = StrLen (Text);
  Skip = (Len + 1 > BufferSize) ? Len + 1 - BufferSize : 0;
  RwUnicodeToAscii (Text + Skip, Buffer, BufferSize);
  FreePool (Text);
}

VOID
RwAtaString (
  IN  CONST UINT8  *Identify,
  IN  UINTN        FirstWord,
  IN  UINTN        Words,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN  Index;
  UINTN  Out;
  UINT8  C;
  UINTN  Start;

  Out = 0;
  for (Index = 0; (Index < Words * 2) && (Out + 1 < BufferSize); Index++) {
    C             = Identify[FirstWord * 2 + (Index ^ 1)];
    Buffer[Out++] = ((C >= 0x20) && (C < 0x7F)) ? (CHAR8)C : ' ';
  }

  while ((Out > 0) && (Buffer[Out - 1] == ' ')) {
    Out--;
  }

  Buffer[Out] = '\0';
  for (Start = 0; Buffer[Start] == ' '; Start++) {
  }

  if (Start > 0) {
    CopyMem (Buffer, Buffer + Start, Out - Start + 1);
  }
}

STATIC
VOID
NvmeString (
  IN  CONST UINT8  *Src,
  IN  UINTN        Len,
  OUT CHAR8        *Buffer,
  IN  UINTN        BufferSize
  )
{
  UINTN  Index;
  UINTN  Out;
  UINTN  Start;

  Out = 0;
  for (Index = 0; (Index < Len) && (Out + 1 < BufferSize); Index++) {
    Buffer[Out++] = ((Src[Index] >= 0x20) && (Src[Index] < 0x7F)) ? (CHAR8)Src[Index] : ' ';
  }

  while ((Out > 0) && (Buffer[Out - 1] == ' ')) {
    Out--;
  }

  Buffer[Out] = '\0';
  for (Start = 0; Buffer[Start] == ' '; Start++) {
  }

  if (Start > 0) {
    CopyMem (Buffer, Buffer + Start, Out - Start + 1);
  }
}

STATIC
UINT16
W (
  IN CONST UINT8  *Id,
  IN UINTN        Word
  )
{
  return (UINT16)(Id[Word * 2] | (Id[Word * 2 + 1] << 8));
}

STATIC
UINT64
AtaCapacity (
  IN CONST UINT8  *Id
  )
{
  UINT64  Sectors;
  UINT32  SectorSize;

  if ((W (Id, 83) & BIT10) != 0) {
    Sectors = (UINT64)W (Id, 100) | LShiftU64 (W (Id, 101), 16) | LShiftU64 (W (Id, 102), 32) | LShiftU64 (W (Id, 103), 48);
  } else {
    Sectors = (UINT64)W (Id, 60) | ((UINT64)W (Id, 61) << 16);
  }

  SectorSize = 512;
  if (((W (Id, 106) & 0xC000) == 0x4000) && ((W (Id, 106) & BIT12) != 0)) {
    SectorSize = (UINT32)(W (Id, 117) | (W (Id, 118) << 16)) * 2;
  }

  return MultU64x32 (Sectors, SectorSize);
}

//
// ---- ATA -------------------------------------------------------------------
//

STATIC
EFI_STATUS
AtaIdentify (
  IN  EFI_ATA_PASS_THRU_PROTOCOL  *Ata,
  IN  UINT16                      Port,
  IN  UINT16                      PmPort,
  IN  UINT8                       Command,
  OUT UINT8                       *Buffer,
  IN  EFI_ATA_STATUS_BLOCK        *Asb
  )
{
  EFI_ATA_COMMAND_BLOCK             Acb;
  EFI_ATA_PASS_THRU_COMMAND_PACKET  Packet;

  ZeroMem (&Acb, sizeof (Acb));
  ZeroMem (Asb, sizeof (*Asb));
  ZeroMem (&Packet, sizeof (Packet));
  Acb.AtaCommand          = Command;
  Acb.AtaDeviceHead       = (UINT8)(BIT7 | BIT6 | BIT5 | ((PmPort == 0xFFFF) ? 0 : ((PmPort & 1) << 4)));
  Packet.Acb              = &Acb;
  Packet.Asb              = Asb;
  Packet.Timeout          = ATA_TIMEOUT_100NS;
  Packet.InDataBuffer     = Buffer;
  Packet.InTransferLength = ID_ATA_SIZE;
  Packet.Protocol         = EFI_ATA_PASS_THRU_PROTOCOL_PIO_DATA_IN;
  Packet.Length           = EFI_ATA_PASS_THRU_LENGTH_BYTES;
  return Ata->PassThru (Ata, Port, PmPort, &Packet, NULL);
}

STATIC
EFI_STATUS
AtaSmartRead (
  IN  EFI_ATA_PASS_THRU_PROTOCOL  *Ata,
  IN  UINT16                      Port,
  IN  UINT16                      PmPort,
  IN  UINT8                       Feature,
  OUT UINT8                       *Buffer,
  IN  EFI_ATA_STATUS_BLOCK        *Asb
  )
{
  EFI_ATA_COMMAND_BLOCK             Acb;
  EFI_ATA_PASS_THRU_COMMAND_PACKET  Packet;

  ZeroMem (&Acb, sizeof (Acb));
  ZeroMem (Asb, sizeof (*Asb));
  ZeroMem (&Packet, sizeof (Packet));
  Acb.AtaCommand          = ATA_CMD_SMART;
  Acb.AtaFeatures         = Feature;
  Acb.AtaSectorCount      = 1;
  Acb.AtaCylinderLow      = 0x4F;     // SMART signature
  Acb.AtaCylinderHigh     = 0xC2;
  Acb.AtaDeviceHead       = (UINT8)(BIT7 | BIT6 | BIT5 | ((PmPort == 0xFFFF) ? 0 : ((PmPort & 1) << 4)));
  Packet.Acb              = &Acb;
  Packet.Asb              = Asb;
  Packet.Timeout          = ATA_TIMEOUT_100NS;
  Packet.InDataBuffer     = Buffer;
  Packet.InTransferLength = 512;
  Packet.Protocol         = EFI_ATA_PASS_THRU_PROTOCOL_PIO_DATA_IN;
  Packet.Length           = EFI_ATA_PASS_THRU_LENGTH_BYTES;
  return Ata->PassThru (Ata, Port, PmPort, &Packet, NULL);
}

STATIC
VOID
AddDev (
  IN OUT RW_STORAGE_LIST  *List,
  IN     UINT8            Kind,
  IN     INT32            Parent,
  IN     UINT16           Port,
  IN     UINT16           PmPort,
  IN     UINT32           Nsid,
  IN     CONST UINT8      *Data,
  IN     UINTN            Size,
  IN     CONST CHAR8      *Where
  )
{
  RW_STORAGE_DEV  *D;
  UINTN           Lbaf;
  UINT8           Lbads;

  if (List->Count >= RW_STORAGE_MAX) {
    return;
  }

  D       = &List->Devs[List->Count];
  D->Data = AllocateCopyPool (Size, Data);
  if (D->Data == NULL) {
    return;
  }

  D->Kind   = Kind;
  D->Parent = Parent;
  D->Port   = Port;
  D->PmPort = PmPort;
  D->Nsid   = Nsid;
  D->Size   = Size;
  D->HealthStatus = (Kind == RwStorNvmeNs) ? EFI_NOT_FOUND : EFI_UNSUPPORTED;
  AsciiStrCpyS (D->Location, sizeof (D->Location), Where);
  switch (Kind) {
    case RwStorAta:
    case RwStorAtapi:
      RwAtaString (Data, 27, 20, D->Model, sizeof (D->Model));
      D->Capacity = (Kind == RwStorAta) ? AtaCapacity (Data) : 0;
      break;
    case RwStorNvmeCtrl:
      NvmeString (&Data[24], 40, D->Model, sizeof (D->Model));
      D->Capacity = *(UINT64 *)&Data[280];     // TNVMCAP low 64 bits
      break;
    case RwStorNvmeNs:
      AsciiSPrint (D->Model, sizeof (D->Model), "Namespace %d", Nsid);
      Lbaf        = Data[26] & 0x0F;
      Lbads       = Data[128 + Lbaf * 4 + 2];
      D->Capacity = ((Lbads >= 9) && (Lbads < 32)) ? LShiftU64 (*(UINT64 *)&Data[0], Lbads) : 0;
      break;
  }

  List->Count++;
}

/**
  SMART data and thresholds of the last added ATA device, only when the
  drive reports SMART supported and enabled (word 82 / 85 bit 0).
**/
STATIC
VOID
AtaHealth (
  IN OUT RW_STORAGE_DEV              *D,
  IN     EFI_ATA_PASS_THRU_PROTOCOL  *Ata,
  IN     UINT8                       *Buffer,
  IN     EFI_ATA_STATUS_BLOCK        *Asb
  )
{
  UINT8       *Health;
  EFI_STATUS  Status;

  if (((W (D->Data, 82) & BIT0) == 0) || ((W (D->Data, 85) & BIT0) == 0)) {
    D->HealthStatus = EFI_UNSUPPORTED;
    return;
  }

  Health = AllocateZeroPool (1024);
  if (Health == NULL) {
    D->HealthStatus = EFI_OUT_OF_RESOURCES;
    return;
  }

  ZeroMem (Buffer, 512);
  Status = AtaSmartRead (Ata, D->Port, D->PmPort, ATA_SMART_READ_DATA, Buffer, Asb);
  if (!EFI_ERROR (Status)) {
    CopyMem (&Health[RW_ATA_SMART_DATA], Buffer, 512);
    ZeroMem (Buffer, 512);
    if (!EFI_ERROR (AtaSmartRead (Ata, D->Port, D->PmPort, ATA_SMART_READ_THRESH, Buffer, Asb))) {
      CopyMem (&Health[RW_ATA_SMART_THRESH], Buffer, 512);
    }
  }

  D->HealthStatus = Status;
  if (EFI_ERROR (Status)) {
    FreePool (Health);
    return;
  }

  D->Health     = Health;
  D->HealthSize = 1024;
}

STATIC
VOID
ScanAta (
  IN OUT RW_STORAGE_LIST  *List
  )
{
  EFI_HANDLE                  *Handles;
  UINTN                       Count;
  UINTN                       H;
  EFI_ATA_PASS_THRU_PROTOCOL  *Ata;
  UINT16                      Port;
  UINT16                      PmPort;
  UINT8                       *Buffer;
  EFI_ATA_STATUS_BLOCK        *Asb;
  UINTN                       Align;
  CHAR8                       Where[96];
  UINTN                       Guard;
  UINTN                       Devices;
  UINTN                       Before;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiAtaPassThruProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (H = 0; H < Count; H++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[H], &gEfiAtaPassThruProtocolGuid, (VOID **)&Ata))) {
      continue;
    }

    Align  = (Ata->Mode->IoAlign > 1) ? Ata->Mode->IoAlign : 1;
    Buffer = AllocateAlignedPages (1, MAX (Align, EFI_PAGE_SIZE));
    Asb    = AllocateAlignedPages (1, MAX (Align, EFI_PAGE_SIZE));
    if ((Buffer == NULL) || (Asb == NULL)) {
      if (Buffer != NULL) {
        FreeAlignedPages (Buffer, 1);
      }

      if (Asb != NULL) {
        FreeAlignedPages (Asb, 1);
      }

      continue;
    }

    Location (Handles[H], Where, sizeof (Where));
    Port = 0xFFFF;
    for (Guard = 0; Guard < 64 && !EFI_ERROR (Ata->GetNextPort (Ata, &Port)); Guard++) {
      //
      // GetNextDevice is walked until it fails: edk2's AtaAtapiPassThru keeps
      // the last port multiplier port it returned, and stopping early leaves
      // that state behind so the next scan would find no device on the port.
      //
      PmPort = 0xFFFF;
      for (Devices = 0; Devices < 16 && !EFI_ERROR (Ata->GetNextDevice (Ata, Port, &PmPort)); Devices++) {
        ZeroMem (Buffer, ID_ATA_SIZE);
        if (!EFI_ERROR (AtaIdentify (Ata, Port, PmPort, ATA_CMD_IDENTIFY, Buffer, Asb))) {
          Before = List->Count;
          AddDev (List, RwStorAta, -1, Port, PmPort, 0, Buffer, ID_ATA_SIZE, Where);
          if (List->Count > Before) {
            AtaHealth (&List->Devs[Before], Ata, Buffer, Asb);
          }
        } else {
          ZeroMem (Buffer, ID_ATA_SIZE);
          if (!EFI_ERROR (AtaIdentify (Ata, Port, PmPort, ATA_CMD_IDENTIFY_PACKET, Buffer, Asb))) {
            AddDev (List, RwStorAtapi, -1, Port, PmPort, 0, Buffer, ID_ATA_SIZE, Where);
          }
        }
      }
    }

    FreeAlignedPages (Buffer, 1);
    FreeAlignedPages (Asb, 1);
  }

  FreePool (Handles);
}

//
// ---- NVMe ------------------------------------------------------------------
//

STATIC
EFI_STATUS
NvmeIdentify (
  IN  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *Nvme,
  IN  UINT32                              Nsid,
  IN  UINT32                              Cns,
  OUT UINT8                               *Buffer
  )
{
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET  Packet;
  EFI_NVM_EXPRESS_COMMAND                   Cmd;
  EFI_NVM_EXPRESS_COMPLETION                Completion;

  ZeroMem (&Packet, sizeof (Packet));
  ZeroMem (&Cmd, sizeof (Cmd));
  ZeroMem (&Completion, sizeof (Completion));
  Cmd.Cdw0.Opcode       = NVME_OPC_IDENTIFY;
  Cmd.Nsid              = Nsid;
  Cmd.Cdw10             = Cns;
  Cmd.Flags             = CDW10_VALID;
  Packet.NvmeCmd        = &Cmd;
  Packet.NvmeCompletion = &Completion;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = ID_NVME_SIZE;
  Packet.CommandTimeout = NVME_TIMEOUT_100NS;
  Packet.QueueType      = NVME_ADMIN_QUEUE;
  return Nvme->PassThru (Nvme, (Cns == NVME_CNS_CONTROLLER) ? 0 : Nsid, &Packet, NULL);
}

/**
  Get Log Page into Buffer (Size bytes, a multiple of 4). Nsid FFFFFFFFh is
  the controller-wide log; the pass-thru namespace must match Cmd.Nsid.
**/
STATIC
EFI_STATUS
NvmeGetLog (
  IN  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *Nvme,
  IN  UINT8                               Lid,
  IN  UINT32                              Size,
  OUT UINT8                               *Buffer
  )
{
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET  Packet;
  EFI_NVM_EXPRESS_COMMAND                   Cmd;
  EFI_NVM_EXPRESS_COMPLETION                Completion;

  ZeroMem (&Packet, sizeof (Packet));
  ZeroMem (&Cmd, sizeof (Cmd));
  ZeroMem (&Completion, sizeof (Completion));
  ZeroMem (Buffer, Size);
  Cmd.Cdw0.Opcode       = NVME_OPC_GET_LOG;
  Cmd.Nsid              = 0xFFFFFFFF;
  Cmd.Cdw10             = Lid | (((Size / 4) - 1) << 16);     // NUMDL, 0's based
  Cmd.Flags             = CDW10_VALID;
  Packet.NvmeCmd        = &Cmd;
  Packet.NvmeCompletion = &Completion;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = Size;
  Packet.CommandTimeout = NVME_TIMEOUT_100NS;
  Packet.QueueType      = NVME_ADMIN_QUEUE;
  return Nvme->PassThru (Nvme, Cmd.Nsid, &Packet, NULL);
}

/**
  Health (02h), Firmware Slot (03h) and Error (01h) logs of the controller
  last added. Buffer is a page from the caller (aligned for the controller);
  the identify data gives the number of error log entries (ELPE).
**/
STATIC
VOID
NvmeHealth (
  IN OUT RW_STORAGE_DEV                      *D,
  IN     EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *Nvme,
  IN     UINT8                               *Buffer
  )
{
  UINT8       *Health;
  UINTN       Errors;
  EFI_STATUS  Status;

  Errors = MIN ((UINTN)D->Data[262] + 1, RW_NVME_ERRORS_MAX);
  Health = AllocateZeroPool (RW_NVME_LOG_ERRORS + Errors * RW_NVME_ERROR_ENTRY);
  if (Health == NULL) {
    D->HealthStatus = EFI_OUT_OF_RESOURCES;
    return;
  }

  Status = NvmeGetLog (Nvme, NVME_LOG_HEALTH, 512, Buffer);
  D->HealthStatus = Status;
  if (EFI_ERROR (Status)) {
    FreePool (Health);
    return;
  }

  CopyMem (&Health[RW_NVME_LOG_HEALTH], Buffer, 512);
  if (!EFI_ERROR (NvmeGetLog (Nvme, NVME_LOG_FW_SLOT, 512, Buffer))) {
    CopyMem (&Health[RW_NVME_LOG_FW_SLOT], Buffer, 512);
  }

  if (!EFI_ERROR (NvmeGetLog (Nvme, NVME_LOG_ERROR, (UINT32)(Errors * RW_NVME_ERROR_ENTRY), Buffer))) {
    CopyMem (&Health[RW_NVME_LOG_ERRORS], Buffer, Errors * RW_NVME_ERROR_ENTRY);
  } else {
    Errors = 0;
  }

  D->Health       = Health;
  D->HealthErrors = Errors;
  D->HealthSize   = RW_NVME_LOG_ERRORS + Errors * RW_NVME_ERROR_ENTRY;
}

STATIC
VOID
ScanNvme (
  IN OUT RW_STORAGE_LIST  *List
  )
{
  EFI_HANDLE                          *Handles;
  UINTN                               Count;
  UINTN                               H;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *Nvme;
  UINT8                               *Buffer;
  UINTN                               Align;
  UINT32                              Nsid;
  INT32                               Ctrl;
  CHAR8                               Where[96];
  UINTN                               Guard;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (H = 0; H < Count; H++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[H], &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme))) {
      continue;
    }

    Align  = (Nvme->Mode->IoAlign > 1) ? Nvme->Mode->IoAlign : 1;
    Buffer = AllocateAlignedPages (1, MAX (Align, EFI_PAGE_SIZE));
    if (Buffer == NULL) {
      continue;
    }

    Location (Handles[H], Where, sizeof (Where));
    ZeroMem (Buffer, ID_NVME_SIZE);
    if (!EFI_ERROR (NvmeIdentify (Nvme, 0, NVME_CNS_CONTROLLER, Buffer))) {
      Ctrl = (INT32)List->Count;
      AddDev (List, RwStorNvmeCtrl, -1, 0, 0, 0, Buffer, ID_NVME_SIZE, Where);
      if (List->Count > (UINTN)Ctrl) {
        NvmeHealth (&List->Devs[Ctrl], Nvme, Buffer);
      }
      Nsid = 0xFFFFFFFF;
      for (Guard = 0; Guard < 1024 && !EFI_ERROR (Nvme->GetNextNamespace (Nvme, &Nsid)); Guard++) {
        ZeroMem (Buffer, ID_NVME_SIZE);
        //
        // Inactive namespace IDs identify as all zero: skip them.
        //
        if (!EFI_ERROR (NvmeIdentify (Nvme, Nsid, NVME_CNS_NAMESPACE, Buffer)) && (*(UINT64 *)&Buffer[0] != 0)) {
          AddDev (List, RwStorNvmeNs, Ctrl, 0, 0, Nsid, Buffer, ID_NVME_SIZE, Where);
        }
      }
    }

    FreeAlignedPages (Buffer, 1);
  }

  FreePool (Handles);
}

EFI_STATUS
RwStorageScan (
  OUT RW_STORAGE_LIST  *List
  )
{
  VOID  *Probe;
  BOOLEAN  Any;

  ZeroMem (List, sizeof (*List));
  Any = (BOOLEAN)(!EFI_ERROR (gBS->LocateProtocol (&gEfiAtaPassThruProtocolGuid, NULL, &Probe)) ||
                  !EFI_ERROR (gBS->LocateProtocol (&gEfiNvmExpressPassThruProtocolGuid, NULL, &Probe)));
  if (!Any) {
    return EFI_NOT_FOUND;
  }

  ScanNvme (List);
  ScanAta (List);
  return EFI_SUCCESS;
}

VOID
RwStorageFree (
  IN OUT RW_STORAGE_LIST  *List
  )
{
  UINTN  Index;

  for (Index = 0; Index < List->Count; Index++) {
    if (List->Devs[Index].Data != NULL) {
      FreePool (List->Devs[Index].Data);
    }

    if (List->Devs[Index].Health != NULL) {
      FreePool (List->Devs[Index].Health);
    }
  }

  ZeroMem (List, sizeof (*List));
}

//
// ---- Decoders --------------------------------------------------------------
//

STATIC
VOID
F (
  IN OUT RW_INFO      *Info,
  IN     UINT32       Offset,
  IN     UINT32       Size,
  IN     UINT8        Emphasis,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Value
  )
{
  RwInfoSetReg (RwInfoAdd (Info, RwKindField, 1, Emphasis, Name, Value), Offset, Size);
}

STATIC
VOID
SizeText (
  IN  UINT64  Bytes,
  OUT CHAR8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  //
  // Decimal units, as drives are labelled.
  //
  if (Bytes >= 1000000000000ULL) {
    AsciiSPrint (Buffer, BufferSize, "%Ld.%02Ld TB (%Ld bytes)", DivU64x32 (Bytes, 1000000000) / 1000, DivU64x32 (Bytes, 1000000000) / 10 % 100, Bytes);
  } else if (Bytes >= 1000000000ULL) {
    AsciiSPrint (Buffer, BufferSize, "%Ld.%01Ld GB (%Ld bytes)", DivU64x32 (Bytes, 1000000000), DivU64x32 (Bytes, 100000000) % 10, Bytes);
  } else {
    AsciiSPrint (Buffer, BufferSize, "%Ld MB (%Ld bytes)", DivU64x32 (Bytes, 1000000), Bytes);
  }
}

STATIC
CONST CHAR8 *
YesNo (
  IN BOOLEAN  Value
  )
{
  return Value ? "Supported" : "Not supported";
}

VOID
RwAtaIdentifyDecode (
  IN     CONST UINT8  *Id,
  IN     BOOLEAN      Atapi,
  IN OUT RW_INFO      *Info
  )
{
  CHAR8   Text[96];
  UINT16  Major;
  UINT16  Sata;
  UINTN   Bit;
  UINT16  Rpm;

  RwInfoSection (Info, 0, Atapi ? "IDENTIFY PACKET DEVICE" : "IDENTIFY DEVICE");
  RwAtaString (Id, 27, 20, Text, sizeof (Text));
  F (Info, 54, 40, RwEmStrong, "Model", Text);
  RwAtaString (Id, 10, 10, Text, sizeof (Text));
  F (Info, 20, 20, RwEmNormal, "Serial number", Text);
  RwAtaString (Id, 23, 4, Text, sizeof (Text));
  F (Info, 46, 8, RwEmNormal, "Firmware", Text);
  if (Atapi) {
    AsciiSPrint (Text, sizeof (Text), "%02Xh%a", (W (Id, 0) >> 8) & 0x1F, (((W (Id, 0) >> 8) & 0x1F) == 5) ? " (CD/DVD)" : "");
    F (Info, 0, 2, RwEmNormal, "Device type", Text);
  } else {
    SizeText (AtaCapacity (Id), Text, sizeof (Text));
    F (Info, ((W (Id, 83) & BIT10) != 0) ? 200 : 120, ((W (Id, 83) & BIT10) != 0) ? 8 : 4, RwEmStrong, "Capacity", Text);
    AsciiSPrint (Text, sizeof (Text), "%a", ((W (Id, 83) & BIT10) != 0) ? "48-bit" : "28-bit");
    F (Info, 166, 2, RwEmNormal, "LBA addressing", Text);
    if ((W (Id, 106) & 0xC000) == 0x4000) {
      AsciiSPrint (
        Text,
        sizeof (Text),
        "%d logical sectors per physical, logical %d bytes",
        1 << (W (Id, 106) & 0x0F),
        ((W (Id, 106) & BIT12) != 0) ? (W (Id, 117) | (W (Id, 118) << 16)) * 2 : 512
        );
      F (Info, 212, 2, RwEmNormal, "Sector size", Text);
    }

    Rpm = W (Id, 217);
    if (Rpm == 1) {
      AsciiStrCpyS (Text, sizeof (Text), "Non-rotating (SSD)");
    } else if ((Rpm >= 0x401) && (Rpm <= 0xFFFE)) {
      AsciiSPrint (Text, sizeof (Text), "%d rpm", Rpm);
    } else {
      AsciiStrCpyS (Text, sizeof (Text), "Not reported");
    }

    F (Info, 434, 2, RwEmNormal, "Rotation rate", Text);
  }

  Major = W (Id, 80);
  Text[0] = '\0';
  if ((Major != 0) && (Major != 0xFFFF)) {
    for (Bit = 15; Bit >= 4; Bit--) {
      if ((Major & (1 << Bit)) != 0) {
        AsciiSPrint (Text, sizeof (Text), (Bit >= 9) ? "ACS-%d" : "ATA/ATAPI-%d", (Bit >= 9) ? (UINT32)(Bit - 7) : (UINT32)Bit);
        break;
      }
    }
  }

  F (Info, 160, 2, RwEmNormal, "Major version", (Text[0] != '\0') ? Text : "Not reported");
  Sata = W (Id, 76);
  if ((Sata != 0) && (Sata != 0xFFFF)) {
    AsciiSPrint (
      Text,
      sizeof (Text),
      "%a%a%a%a",
      ((Sata & BIT3) != 0) ? "6.0 " : "",
      ((Sata & BIT2) != 0) ? "3.0 " : "",
      ((Sata & BIT1) != 0) ? "1.5 " : "",
      ((Sata & BIT8) != 0) ? "Gb/s, NCQ" : "Gb/s"
      );
    F (Info, 152, 2, RwEmNormal, "SATA speeds", Text);
  }

  RwInfoSection (Info, 0, "Features");
  F (Info, 98, 2, RwEmNormal, "DMA", YesNo ((W (Id, 49) & BIT8) != 0));
  F (Info, 98, 2, RwEmNormal, "LBA", YesNo ((W (Id, 49) & BIT9) != 0));
  if (!Atapi) {
    F (Info, 164, 2, RwEmNormal, "SMART", YesNo ((W (Id, 82) & BIT0) != 0));
    F (Info, 164, 2, RwEmNormal, "Security", YesNo ((W (Id, 82) & BIT1) != 0));
    F (Info, 164, 2, RwEmNormal, "Write cache", YesNo ((W (Id, 82) & BIT5) != 0));
    F (Info, 172, 2, RwEmNormal, "Write cache enabled", ((W (Id, 85) & BIT5) != 0) ? "Yes" : "No");
    F (Info, 338, 2, RwEmNormal, "TRIM", YesNo ((W (Id, 169) & BIT0) != 0));
    if ((W (Id, 82) & BIT1) != 0) {
      AsciiSPrint (
        Text,
        sizeof (Text),
        "%a%a%a",
        ((W (Id, 128) & BIT1) != 0) ? "Enabled" : "Disabled",
        ((W (Id, 128) & BIT2) != 0) ? ", locked" : "",
        ((W (Id, 128) & BIT3) != 0) ? ", frozen" : ""
        );
      F (Info, 256, 2, ((W (Id, 128) & BIT2) != 0) ? RwEmAlert : RwEmNormal, "Security state", Text);
    }
  }
}

VOID
RwNvmeIdCtrlDecode (
  IN     CONST UINT8  *Id,
  IN OUT RW_INFO      *Info
  )
{
  CHAR8   Text[96];
  UINT32  Ver;
  UINT16  Temp;

  RwInfoSection (Info, 0, "Identify Controller");
  NvmeString (&Id[24], 40, Text, sizeof (Text));
  F (Info, 24, 40, RwEmStrong, "Model (MN)", Text);
  NvmeString (&Id[4], 20, Text, sizeof (Text));
  F (Info, 4, 20, RwEmNormal, "Serial (SN)", Text);
  NvmeString (&Id[64], 8, Text, sizeof (Text));
  F (Info, 64, 8, RwEmNormal, "Firmware (FR)", Text);
  AsciiSPrint (Text, sizeof (Text), "%04X / %04X", *(UINT16 *)&Id[0], *(UINT16 *)&Id[2]);
  F (Info, 0, 4, RwEmNormal, "PCI vendor / subsystem", Text);
  AsciiSPrint (Text, sizeof (Text), "%02X-%02X-%02X", Id[75], Id[74], Id[73]);
  F (Info, 73, 3, RwEmNormal, "IEEE OUI", Text);
  Ver = *(UINT32 *)&Id[80];
  if (Ver != 0) {
    AsciiSPrint (Text, sizeof (Text), "%d.%d.%d", Ver >> 16, (Ver >> 8) & 0xFF, Ver & 0xFF);
  } else {
    AsciiStrCpyS (Text, sizeof (Text), "1.0 or 1.1 (not reported)");
  }

  F (Info, 80, 4, RwEmNormal, "NVMe version", Text);
  AsciiSPrint (Text, sizeof (Text), "%d", *(UINT16 *)&Id[78]);
  F (Info, 78, 2, RwEmNormal, "Controller ID", Text);
  if (Id[77] == 0) {
    AsciiStrCpyS (Text, sizeof (Text), "No limit");
  } else {
    AsciiSPrint (Text, sizeof (Text), "2^%d minimum pages", Id[77]);
  }

  F (Info, 77, 1, RwEmNormal, "Max data transfer", Text);
  if ((*(UINT64 *)&Id[280] != 0) || (*(UINT64 *)&Id[288] != 0)) {
    SizeText (*(UINT64 *)&Id[280], Text, sizeof (Text));
    F (Info, 280, 16, RwEmStrong, "Total capacity", Text);
  }

  AsciiSPrint (Text, sizeof (Text), "%d", *(UINT32 *)&Id[516]);
  F (Info, 516, 4, RwEmNormal, "Namespaces (NN)", Text);
  AsciiSPrint (Text, sizeof (Text), "%d", Id[263] + 1);
  F (Info, 263, 1, RwEmNormal, "Power states", Text);
  Temp = *(UINT16 *)&Id[266];
  if (Temp != 0) {
    AsciiSPrint (Text, sizeof (Text), "%d C warning, %d C critical", (INT32)Temp - 273, (INT32)*(UINT16 *)&Id[268] - 273);
    F (Info, 266, 4, RwEmNormal, "Temperature limits", Text);
  }

  RwInfoSection (Info, 0, "Features");
  F (Info, 256, 2, RwEmNormal, "Security send / receive", YesNo ((Id[256] & BIT0) != 0));
  F (Info, 256, 2, RwEmNormal, "Format NVM", YesNo ((Id[256] & BIT1) != 0));
  F (Info, 256, 2, RwEmNormal, "Firmware download", YesNo ((Id[256] & BIT2) != 0));
  F (Info, 256, 2, RwEmNormal, "Namespace management", YesNo ((Id[256] & BIT3) != 0));
  F (Info, 520, 2, RwEmNormal, "Dataset management (TRIM)", YesNo ((Id[520] & BIT2) != 0));
  F (Info, 520, 2, RwEmNormal, "Write zeroes", YesNo ((Id[520] & BIT3) != 0));
  F (Info, 525, 1, RwEmNormal, "Volatile write cache", ((Id[525] & BIT0) != 0) ? "Present" : "None");
  AsciiSPrint (Text, sizeof (Text), "%d slot(s)%a", (Id[260] >> 1) & 0x07, ((Id[260] & BIT0) != 0) ? ", slot 1 read-only" : "");
  F (Info, 260, 1, RwEmNormal, "Firmware slots", Text);
  NvmeString (&Id[768], 256, Text, sizeof (Text));
  if (Text[0] != '\0') {
    F (Info, 768, 256, RwEmDim, "NVM subsystem NQN", Text);
  }
}

VOID
RwNvmeIdNsDecode (
  IN     CONST UINT8  *Id,
  IN OUT RW_INFO      *Info
  )
{
  CHAR8   Text[96];
  UINTN   Lbaf;
  UINTN   Index;
  UINT8   Lbads;
  STATIC CONST CHAR8  *Rp[] = { "best", "better", "good", "degraded" };

  Lbaf  = Id[26] & 0x0F;
  Lbads = Id[128 + Lbaf * 4 + 2];
  RwInfoSection (Info, 0, "Identify Namespace");
  AsciiSPrint (Text, sizeof (Text), "%Ld blocks", *(UINT64 *)&Id[0]);
  F (Info, 0, 8, RwEmNormal, "Size (NSZE)", Text);
  AsciiSPrint (Text, sizeof (Text), "%Ld blocks", *(UINT64 *)&Id[8]);
  F (Info, 8, 8, RwEmNormal, "Capacity (NCAP)", Text);
  AsciiSPrint (Text, sizeof (Text), "%Ld blocks", *(UINT64 *)&Id[16]);
  F (Info, 16, 8, RwEmNormal, "Utilization (NUSE)", Text);
  if ((Lbads >= 9) && (Lbads < 32)) {
    SizeText (LShiftU64 (*(UINT64 *)&Id[0], Lbads), Text, sizeof (Text));
    F (Info, 0, 8, RwEmStrong, "Size", Text);
  }

  AsciiSPrint (Text, sizeof (Text), "LBAF %d: %d-byte blocks", Lbaf, ((Lbads >= 9) && (Lbads < 32)) ? (1 << Lbads) : 0);
  F (Info, 26, 1, RwEmStrong, "Formatted LBA size", Text);
  AsciiSPrint (Text, sizeof (Text), "%02X%02X%02X%02X%02X%02X%02X%02X", Id[120], Id[121], Id[122], Id[123], Id[124], Id[125], Id[126], Id[127]);
  F (Info, 120, 8, RwEmNormal, "EUI-64", Text);
  F (Info, 24, 1, RwEmNormal, "Thin provisioning", YesNo ((Id[24] & BIT0) != 0));

  RwInfoSection (Info, 0, "LBA formats (%d)", Id[25] + 1);
  for (Index = 0; (Index <= Id[25]) && (Index < 16); Index++) {
    Lbads = Id[128 + Index * 4 + 2];
    AsciiSPrint (
      Text,
      sizeof (Text),
      "%d-byte data, %d-byte metadata, %a performance%a",
      ((Lbads >= 9) && (Lbads < 32)) ? (1 << Lbads) : 0,
      *(UINT16 *)&Id[128 + Index * 4],
      Rp[Id[128 + Index * 4 + 3] & 0x03],
      (Index == Lbaf) ? "  (in use)" : ""
      );
    RwInfoSetReg (RwInfoField (Info, 1, (Index == Lbaf) ? RwEmStrong : RwEmNormal, "LBAF", "%d: %a", Index, Text), (UINT32)(128 + Index * 4), 4);
  }
}

VOID
RwStorageDecode (
  IN     CONST RW_STORAGE_DEV  *Dev,
  IN OUT RW_INFO               *Info
  )
{
  CHAR8  Text[96];

  RwInfoSection (Info, 0, "%a", RwStorageKindName (Dev->Kind));
  RwInfoField (Info, 1, RwEmNormal, "Location", "%a", Dev->Location);
  if ((Dev->Kind == RwStorAta) || (Dev->Kind == RwStorAtapi)) {
    if (Dev->PmPort == 0xFFFF) {
      AsciiSPrint (Text, sizeof (Text), "%d", Dev->Port);
    } else {
      AsciiSPrint (Text, sizeof (Text), "%d, port multiplier port %d", Dev->Port, Dev->PmPort);
    }

    RwInfoField (Info, 1, RwEmNormal, "Port", "%a", Text);
    RwAtaIdentifyDecode (Dev->Data, (BOOLEAN)(Dev->Kind == RwStorAtapi), Info);
  } else if (Dev->Kind == RwStorNvmeCtrl) {
    RwNvmeIdCtrlDecode (Dev->Data, Info);
  } else {
    RwInfoField (Info, 1, RwEmNormal, "Namespace ID", "%d", Dev->Nsid);
    RwNvmeIdNsDecode (Dev->Data, Info);
  }

  RwInfoSection (Info, 0, "Raw identify data (%d bytes)", Dev->Size);
  RwInfoAddHex (Info, 1, Dev->Data, Dev->Size, 0);
}

//
// ---- Health decoders -------------------------------------------------------
//

typedef struct {
  UINT8          Id;
  CONST CHAR8    *Name;
} SMART_ATTR_NAME;

//
// Common attribute names; vendors reuse some IDs differently.
//
STATIC CONST SMART_ATTR_NAME  mSmartNames[] = {
  {   1, "Raw Read Error Rate" },
  {   2, "Throughput Performance" },
  {   3, "Spin-Up Time" },
  {   4, "Start/Stop Count" },
  {   5, "Reallocated Sectors Count" },
  {   7, "Seek Error Rate" },
  {   8, "Seek Time Performance" },
  {   9, "Power-On Hours" },
  {  10, "Spin Retry Count" },
  {  11, "Calibration Retry Count" },
  {  12, "Power Cycle Count" },
  { 170, "Available Reserved Space" },
  { 171, "Program Fail Count" },
  { 172, "Erase Fail Count" },
  { 173, "Wear Leveling Count" },
  { 174, "Unexpected Power Loss Count" },
  { 175, "Power Loss Protection Failure" },
  { 177, "Wear Range Delta" },
  { 179, "Used Reserved Block Count" },
  { 180, "Unused Reserved Block Count" },
  { 181, "Program Fail Count Total" },
  { 182, "Erase Fail Count Total" },
  { 183, "SATA Downshift Error Count" },
  { 184, "End-to-End Error" },
  { 187, "Reported Uncorrectable Errors" },
  { 188, "Command Timeout" },
  { 189, "High Fly Writes" },
  { 190, "Airflow Temperature" },
  { 191, "G-Sense Error Rate" },
  { 192, "Power-off Retract Count" },
  { 193, "Load Cycle Count" },
  { 194, "Temperature" },
  { 195, "Hardware ECC Recovered" },
  { 196, "Reallocation Event Count" },
  { 197, "Current Pending Sector Count" },
  { 198, "Offline Uncorrectable" },
  { 199, "UDMA CRC Error Count" },
  { 200, "Write Error Rate" },
  { 230, "Drive Life Protection Status" },
  { 231, "SSD Life Left" },
  { 232, "Endurance Remaining" },
  { 233, "Media Wearout Indicator" },
  { 234, "Average / Max Erase Count" },
  { 235, "Good Block Count" },
  { 240, "Head Flying Hours" },
  { 241, "Total LBAs Written" },
  { 242, "Total LBAs Read" },
};

CONST CHAR8 *
RwAtaSmartAttrName (
  IN UINT8  Id
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mSmartNames); Index++) {
    if (mSmartNames[Index].Id == Id) {
      return mSmartNames[Index].Name;
    }
  }

  return "";
}

STATIC
CONST CHAR8 *
SelfTestText (
  IN UINT8  Status
  )
{
  switch (Status >> 4) {
    case 0:  return "Passed or never run";
    case 15: return "In progress";
    default: return "Failed or aborted";
  }
}

/**
  SMART READ DATA: 30 attribute entries of 12 bytes from offset 2 (ID, flags,
  current, worst, 6-byte raw); SMART READ THRESHOLDS: same layout, the
  threshold in byte 1 of the matching ID.
**/
STATIC
VOID
AtaSmartDecode (
  IN     CONST UINT8  *Data,
  IN     CONST UINT8  *Thresh,
  IN OUT RW_INFO      *Info
  )
{
  UINTN        Index;
  UINTN        T;
  CONST UINT8  *E;
  UINT8        Limit;
  UINT64       Raw;
  UINTN        Shown;
  BOOLEAN      Failing;
  CONST CHAR8  *Name;
  CHAR8        Label[48];
  UINT32       Ext;

  RwInfoSection (Info, 0, "SMART attributes (revision %d)", Data[0] | (Data[1] << 8));
  Shown = 0;
  for (Index = 0; Index < 30; Index++) {
    E = &Data[2 + Index * 12];
    if (E[0] == 0) {
      continue;
    }

    Limit = 0;
    for (T = 0; T < 30; T++) {
      if (Thresh[2 + T * 12] == E[0]) {
        Limit = Thresh[2 + T * 12 + 1];
        break;
      }
    }

    Raw = (UINT64)E[5] | ((UINT64)E[6] << 8) | ((UINT64)E[7] << 16) | ((UINT64)E[8] << 24) |
          LShiftU64 (E[9], 32) | LShiftU64 (E[10], 40);
    Failing = (BOOLEAN)((Limit != 0) && (Limit != 0xFE) && (E[3] <= Limit));
    Name    = RwAtaSmartAttrName (E[0]);
    AsciiSPrint (Label, sizeof (Label), "%3d %a", E[0], (Name[0] != '\0') ? Name : "Vendor specific");
    RwInfoSetReg (
      RwInfoField (
        Info, 1, Failing ? RwEmAlert : RwEmNormal, Label,
        "Current %3d  Worst %3d  Threshold %3d  Raw %Ld%a%a",
        E[3], E[4], Limit, Raw,
        ((E[1] & BIT0) != 0) ? "  pre-fail" : "",
        Failing ? "  FAILING" : ""
        ),
      (UINT32)(2 + Index * 12),
      12
      );
    Shown++;
  }

  if (Shown == 0) {
    RwInfoText (Info, 1, RwEmDim, "No attributes reported");
  }

  RwInfoSection (Info, 0, "SMART status");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Offline data collection", "%02Xh", Data[362]), 362, 1);
  RwInfoSetReg (
    RwInfoField (Info, 1, ((Data[363] >> 4) != 0 && (Data[363] >> 4) != 15) ? RwEmAlert : RwEmNormal, "Last self-test", "%a (%02Xh)", SelfTestText (Data[363]), Data[363]),
    363,
    1
    );
  Ext = (Data[373] == 0xFF) ? (UINT32)(Data[375] | (Data[376] << 8)) : Data[373];
  RwInfoField (Info, 1, RwEmNormal, "Short / extended self-test", "%d / %d minutes", Data[372], Ext);
}

STATIC
UINT64
Le64 (
  IN CONST UINT8  *P
  )
{
  return ReadUnaligned64 ((CONST UINT64 *)P);
}

/**
  NVMe SMART / Health (02h), Firmware Slot (03h) and Error Information (01h)
  log pages as stored in Dev->Health.
**/
STATIC
VOID
NvmeHealthDecode (
  IN     CONST RW_STORAGE_DEV  *Dev,
  IN OUT RW_INFO               *Info
  )
{
  STATIC CONST CHAR8  *Warn[] = {
    "spare below threshold", "temperature", "reliability degraded", "read-only",
    "volatile backup failed", "PMR read-only"
  };
  CONST UINT8         *H;
  CONST UINT8         *E;
  CHAR8               Text[160];
  CHAR8               Slot[9];
  UINTN               Index;
  UINT16              Kelvin;
  UINTN               Shown;
  UINT64              Units;

  H = &Dev->Health[RW_NVME_LOG_HEALTH];
  RwInfoSection (Info, 0, "SMART / Health log (02h)");
  Text[0] = '\0';
  for (Index = 0; Index < ARRAY_SIZE (Warn); Index++) {
    if ((H[0] & (1 << Index)) != 0) {
      AsciiStrCatS (Text, sizeof (Text), (Text[0] != '\0') ? ", " : " ");
      AsciiStrCatS (Text, sizeof (Text), Warn[Index]);
    }
  }

  RwInfoSetReg (RwInfoField (Info, 1, (H[0] != 0) ? RwEmAlert : RwEmNormal, "Critical warning", "%02Xh %a", H[0], (Text[0] != '\0') ? Text : " none"), 0, 1);
  Kelvin = (UINT16)(H[1] | (H[2] << 8));
  RwInfoSetReg (RwInfoField (Info, 1, RwEmStrong, "Composite temperature", "%d C (%d K)", (INT32)Kelvin - 273, Kelvin), 1, 2);
  RwInfoSetReg (RwInfoField (Info, 1, (H[3] < H[4]) ? RwEmAlert : RwEmNormal, "Available spare", "%d%% (threshold %d%%)", H[3], H[4]), 3, 2);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Percentage used", "%d%%", H[5]), 5, 1);
  Units = Le64 (&H[32]);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Data units read", "%Ld (%Ld MB)", Units, DivU64x32 (MultU64x32 (Units, 512), 1000)), 32, 16);
  Units = Le64 (&H[48]);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Data units written", "%Ld (%Ld MB)", Units, DivU64x32 (MultU64x32 (Units, 512), 1000)), 48, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Host read commands", "%Ld", Le64 (&H[64])), 64, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Host write commands", "%Ld", Le64 (&H[80])), 80, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Controller busy time", "%Ld minutes", Le64 (&H[96])), 96, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Power cycles", "%Ld", Le64 (&H[112])), 112, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Power-on hours", "%Ld", Le64 (&H[128])), 128, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Unsafe shutdowns", "%Ld", Le64 (&H[144])), 144, 16);
  RwInfoSetReg (RwInfoField (Info, 1, (Le64 (&H[160]) != 0) ? RwEmAlert : RwEmNormal, "Media and data integrity errors", "%Ld", Le64 (&H[160])), 160, 16);
  RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, "Error log entries", "%Ld", Le64 (&H[176])), 176, 16);
  RwInfoSetReg (
    RwInfoField (Info, 1, RwEmNormal, "Warning / critical temperature time", "%d / %d minutes", ReadUnaligned32 ((CONST UINT32 *)&H[192]), ReadUnaligned32 ((CONST UINT32 *)&H[196])),
    192,
    8
    );
  for (Index = 0; Index < 8; Index++) {
    Kelvin = (UINT16)(H[200 + Index * 2] | (H[201 + Index * 2] << 8));
    if (Kelvin != 0) {
      AsciiSPrint (Text, sizeof (Text), "Temperature sensor %d", Index + 1);
      RwInfoSetReg (RwInfoField (Info, 1, RwEmNormal, Text, "%d C", (INT32)Kelvin - 273), (UINT32)(200 + Index * 2), 2);
    }
  }

  H = &Dev->Health[RW_NVME_LOG_FW_SLOT];
  RwInfoSection (Info, 0, "Firmware slot log (03h)");
  RwInfoSetReg (RwInfoField (Info, 1, RwEmStrong, "Active slot", "%d (next reset: %d)", H[0] & 0x07, (H[0] >> 4) & 0x07), RW_NVME_LOG_FW_SLOT, 1);
  for (Index = 1; Index <= 7; Index++) {
    E = &H[8 * Index];
    if (E[0] == 0) {
      continue;
    }

    CopyMem (Slot, E, 8);
    Slot[8] = '\0';
    AsciiSPrint (Text, sizeof (Text), "Slot %d", Index);
    RwInfoSetReg (RwInfoField (Info, 1, (Index == (UINTN)(H[0] & 0x07)) ? RwEmStrong : RwEmNormal, Text, "%a", Slot), (UINT32)(RW_NVME_LOG_FW_SLOT + 8 * Index), 8);
  }

  RwInfoSection (Info, 0, "Error information log (01h), %d entries read", Dev->HealthErrors);
  Shown = 0;
  for (Index = 0; Index < Dev->HealthErrors; Index++) {
    E = &Dev->Health[RW_NVME_LOG_ERRORS + Index * RW_NVME_ERROR_ENTRY];
    if (Le64 (E) == 0) {
      continue;
    }

    AsciiSPrint (Text, sizeof (Text), "Error %Ld", Le64 (E));
    RwInfoSetReg (
      RwInfoField (
        Info, 1, RwEmAlert, Text, "SQ %d  CID %04Xh  status %04Xh  LBA %Lxh  NSID %d",
        E[8] | (E[9] << 8), E[10] | (E[11] << 8), E[12] | (E[13] << 8),
        Le64 (&E[16]), ReadUnaligned32 ((CONST UINT32 *)&E[24])
        ),
      (UINT32)(RW_NVME_LOG_ERRORS + Index * RW_NVME_ERROR_ENTRY),
      RW_NVME_ERROR_ENTRY
      );
    Shown++;
  }

  if (Shown == 0) {
    RwInfoText (Info, 1, RwEmDim, "No errors logged");
  }
}

VOID
RwStorageHealthDecode (
  IN     CONST RW_STORAGE_DEV  *Dev,
  IN OUT RW_INFO               *Info
  )
{
  if (Dev->Kind == RwStorNvmeNs) {
    RwInfoSection (Info, 0, "Health");
    RwInfoText (Info, 1, RwEmDim, "NVMe health logs belong to the controller: select it in the list.");
    return;
  }

  if (Dev->Kind == RwStorAtapi) {
    RwInfoSection (Info, 0, "Health");
    RwInfoText (Info, 1, RwEmDim, "ATAPI devices have no SMART data.");
    return;
  }

  if ((Dev->Health == NULL) || EFI_ERROR (Dev->HealthStatus)) {
    RwInfoSection (Info, 0, "Health");
    if (Dev->HealthStatus == EFI_UNSUPPORTED) {
      RwInfoText (Info, 1, RwEmDim, "SMART is not supported or not enabled on this drive (it is never enabled from here).");
    } else {
      RwInfoText (Info, 1, RwEmAlert, "The drive refused the request (%r).", Dev->HealthStatus);
    }

    return;
  }

  if (Dev->Kind == RwStorAta) {
    AtaSmartDecode (&Dev->Health[RW_ATA_SMART_DATA], &Dev->Health[RW_ATA_SMART_THRESH], Info);
    RwInfoSection (Info, 0, "Raw SMART data (512 bytes)");
    RwInfoAddHex (Info, 1, &Dev->Health[RW_ATA_SMART_DATA], 512, 0);
    RwInfoSection (Info, 0, "Raw SMART thresholds (512 bytes)");
    RwInfoAddHex (Info, 1, &Dev->Health[RW_ATA_SMART_THRESH], 512, 0);
  } else {
    NvmeHealthDecode (Dev, Info);
    RwInfoSection (Info, 0, "Raw log data (%d bytes)", Dev->HealthSize);
    RwInfoAddHex (Info, 1, Dev->Health, Dev->HealthSize, 0);
  }
}

typedef struct {
  CONST RW_STORAGE_DEV    *Dev;
} STOR_DS;

STATIC
EFI_STATUS
EFIAPI
StorDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  CONST RW_STORAGE_DEV  *Dev;
  UINTN                 Index;

  Dev    = ((STOR_DS *)Source->Ctx)->Dev;
  *Value = 0;
  if (Address + Width > Dev->Size) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < Width; Index++) {
    *Value |= LShiftU64 (Dev->Data[Address + Index], 8 * Index);
  }

  return EFI_SUCCESS;
}

VOID
RwStorageSourceInit (
  OUT RW_DATA_SOURCE        *Source,
  IN  CONST RW_STORAGE_DEV  *Dev
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiSPrint (Source->Name, sizeof (Source->Name), "%a identify data (read-only copy)", RwStorageKindName (Dev->Kind));
  Source->Base       = 0;
  Source->Limit      = Dev->Size - 1;
  Source->AddrDigits = 3;
  Source->WidthMask  = RW_DS_WIDTH_8 | RW_DS_WIDTH_16 | RW_DS_WIDTH_32 | RW_DS_WIDTH_64;
  Source->Read       = StorDsRead;
  ((STOR_DS *)Source->Ctx)->Dev = Dev;
}
