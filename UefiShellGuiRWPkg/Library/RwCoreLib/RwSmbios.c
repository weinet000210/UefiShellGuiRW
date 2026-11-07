/** @file
  SMBIOS: entry point discovery (2.x and 3.x), structure walk, type names and
  per-type decoders producing RW_INFO documents.

  Decoding follows the DMTF SMBIOS Reference Specification (DSP0134).
  Ported from uefirrw SmbiosView.c without its screens.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Library/UefiLib.h>
#include <IndustryStandard/SmBios.h>
#include <Guid/SmBios.h>

//
// ---- Name tables -------------------------------------------------------------
//

STATIC CONST CHAR8  *mTypeNames[] = {
  "BIOS Information",                       // 0
  "System Information",                     // 1
  "Baseboard Information",                  // 2
  "System Enclosure or Chassis",            // 3
  "Processor Information",                  // 4
  "Memory Controller Information",          // 5
  "Memory Module Information",              // 6
  "Cache Information",                      // 7
  "Port Connector Information",             // 8
  "System Slots",                           // 9
  "On Board Devices Information",           // 10
  "OEM Strings",                            // 11
  "System Configuration Options",           // 12
  "BIOS Language Information",              // 13
  "Group Associations",                     // 14
  "System Event Log",                       // 15
  "Physical Memory Array",                  // 16
  "Memory Device",                          // 17
  "32-Bit Memory Error Information",        // 18
  "Memory Array Mapped Address",            // 19
  "Memory Device Mapped Address",           // 20
  "Built-in Pointing Device",               // 21
  "Portable Battery",                       // 22
  "System Reset",                           // 23
  "Hardware Security",                      // 24
  "System Power Controls",                  // 25
  "Voltage Probe",                          // 26
  "Cooling Device",                         // 27
  "Temperature Probe",                      // 28
  "Electrical Current Probe",               // 29
  "Out-of-Band Remote Access",              // 30
  "Boot Integrity Services Entry Point",    // 31
  "System Boot Information",                // 32
  "64-Bit Memory Error Information",        // 33
  "Management Device",                      // 34
  "Management Device Component",            // 35
  "Management Device Threshold Data",       // 36
  "Memory Channel",                         // 37
  "IPMI Device Information",                // 38
  "System Power Supply",                    // 39
  "Additional Information",                 // 40
  "Onboard Devices Extended Information",   // 41
  "Management Controller Host Interface",   // 42
  "TPM Device",                             // 43
  "Processor Additional Information",       // 44
  "Firmware Inventory Information",         // 45
  "String Property",                        // 46
};

STATIC CONST CHAR8  *mChassisTypes[] = {
  NULL, "Other", "Unknown", "Desktop", "Low Profile Desktop", "Pizza Box",
  "Mini Tower", "Tower", "Portable", "Laptop", "Notebook", "Hand Held",
  "Docking Station", "All in One", "Sub Notebook", "Space-saving", "Lunch Box",
  "Main Server Chassis", "Expansion Chassis", "SubChassis", "Bus Expansion Chassis",
  "Peripheral Chassis", "RAID Chassis", "Rack Mount Chassis", "Sealed-case PC",
  "Multi-system Chassis", "Compact PCI", "Advanced TCA", "Blade", "Blade Enclosure",
  "Tablet", "Convertible", "Detachable", "IoT Gateway", "Embedded PC", "Mini PC",
  "Stick PC",
};

STATIC CONST CHAR8  *mChassisStates[] = {
  NULL, "Other", "Unknown", "Safe", "Warning", "Critical", "Non-recoverable",
};

STATIC CONST CHAR8  *mChassisSecurity[] = {
  NULL, "Other", "Unknown", "None", "External Interface Locked Out", "External Interface Enabled",
};

STATIC CONST CHAR8  *mWakeUpTypes[] = {
  "Reserved", "Other", "Unknown", "APM Timer", "Modem Ring", "LAN Remote",
  "Power Switch", "PCI PME#", "AC Power Restored",
};

STATIC CONST CHAR8  *mBoardTypes[] = {
  NULL, "Unknown", "Other", "Server Blade", "Connectivity Switch",
  "System Management Module", "Processor Module", "I/O Module", "Memory Module",
  "Daughter Board", "Motherboard", "Processor/Memory Module", "Processor/IO Module",
  "Interconnect Board",
};

STATIC CONST CHAR8  *mProcTypes[] = {
  NULL, "Other", "Unknown", "Central Processor", "Math Processor", "DSP Processor",
  "Video Processor",
};

STATIC CONST CHAR8  *mEccTypes[] = {
  NULL, "Other", "Unknown", "None", "Parity", "Single-bit ECC", "Multi-bit ECC", "CRC",
};

STATIC CONST CHAR8  *mCacheTypes[] = {
  NULL, "Other", "Unknown", "Instruction", "Data", "Unified",
};

STATIC CONST CHAR8  *mCacheAssoc[] = {
  NULL, "Other", "Unknown", "Direct Mapped", "2-way Set-Associative",
  "4-way Set-Associative", "Fully Associative", "8-way Set-Associative",
  "16-way Set-Associative", "12-way Set-Associative", "24-way Set-Associative",
  "32-way Set-Associative", "48-way Set-Associative", "64-way Set-Associative",
  "20-way Set-Associative",
};

STATIC CONST CHAR8  *mSlotUsage[] = {
  NULL, "Other", "Unknown", "Available", "In Use", "Unavailable",
};

STATIC CONST CHAR8  *mSlotWidth[] = {
  NULL, "Other", "Unknown", "8 bit", "16 bit", "32 bit", "64 bit", "128 bit",
  "x1", "x2", "x4", "x8", "x12", "x16", "x32",
};

STATIC CONST CHAR8  *mArrayLocation[] = {
  NULL, "Other", "Unknown", "System Board or Motherboard", "ISA Add-on Card",
  "EISA Add-on Card", "PCI Add-on Card", "MCA Add-on Card", "PCMCIA Add-on Card",
  "Proprietary Add-on Card", "NuBus",
};

STATIC CONST CHAR8  *mArrayUse[] = {
  NULL, "Other", "Unknown", "System Memory", "Video Memory", "Flash Memory",
  "Non-volatile RAM", "Cache Memory",
};

STATIC CONST CHAR8  *mMemFormFactors[] = {
  NULL, "Other", "Unknown", "SIMM", "SIP", "Chip", "DIP", "ZIP",
  "Proprietary Card", "DIMM", "TSOP", "Row of Chips", "RIMM", "SODIMM",
  "SRIMM", "FB-DIMM", "Die", "CAMM",
};

STATIC CONST CHAR8  *mMemTypes[] = {
  NULL, "Other", "Unknown", "DRAM", "EDRAM", "VRAM", "SRAM", "RAM", "ROM",
  "FLASH", "EEPROM", "FEPROM", "EPROM", "CDRAM", "3DRAM", "SDRAM", "SGRAM",
  "RDRAM", "DDR", "DDR2", "DDR2 FB-DIMM", NULL, NULL, NULL, "DDR3", "FBD2",
  "DDR4", "LPDDR", "LPDDR2", "LPDDR3", "LPDDR4", "Logical non-volatile device",
  "HBM", "HBM2", "DDR5", "LPDDR5", "HBM3",
};

STATIC CONST CHAR8  *mMemTypeDetail[] = {
  "Reserved", "Other", "Unknown", "Fast-paged", "Static column", "Pseudo-static",
  "RAMBUS", "Synchronous", "CMOS", "EDO", "Window DRAM", "Cache DRAM",
  "Non-volatile", "Registered (Buffered)", "Unbuffered (Unregistered)", "LRDIMM",
};

STATIC CONST CHAR8  *mOnboardTypes[] = {
  NULL, "Other", "Unknown", "Video", "SCSI Controller", "Ethernet", "Token Ring",
  "Sound", "PATA Controller", "SATA Controller", "SAS Controller",
  "Wireless LAN", "Bluetooth", "WWAN", "eMMC", "NVMe Controller", "UFS Controller",
};

STATIC CONST CHAR8  *mBiosChar[32] = {
  NULL, NULL, "Unknown", "BIOS Characteristics Not Supported", "ISA supported",
  "MCA supported", "EISA supported", "PCI supported", "PC Card (PCMCIA) supported",
  "Plug and Play supported", "APM supported", "BIOS is upgradeable (Flash)",
  "BIOS shadowing allowed", "VL-VESA supported", "ESCD support available",
  "Boot from CD supported", "Selectable boot supported", "BIOS ROM is socketed",
  "Boot from PC Card supported", "EDD specification supported",
  "Int 13h - NEC 9800 1.2 MB floppy", "Int 13h - Toshiba 1.2 MB floppy",
  "Int 13h - 5.25\" 360 KB floppy", "Int 13h - 5.25\" 1.2 MB floppy",
  "Int 13h - 3.5\" 720 KB floppy", "Int 13h - 3.5\" 2.88 MB floppy",
  "Int 5h - Print screen service", "Int 9h - 8042 keyboard services",
  "Int 14h - Serial services", "Int 17h - Printer services",
  "Int 10h - CGA/Mono video services", "NEC PC-98",
};

STATIC CONST CHAR8  *mBiosExt1[8] = {
  "ACPI supported", "USB Legacy supported", "AGP supported", "I2O boot supported",
  "LS-120 SuperDisk boot supported", "ATAPI ZIP drive boot supported",
  "1394 boot supported", "Smart battery supported",
};

STATIC CONST CHAR8  *mBiosExt2[8] = {
  "BIOS Boot Specification supported", "Function key-initiated network boot supported",
  "Targeted content distribution supported", "UEFI Specification supported",
  "SMBIOS table describes a virtual machine", "Manufacturing mode supported",
  "Manufacturing mode enabled", NULL,
};

STATIC CONST CHAR8  *mProcChar[16] = {
  NULL, "Unknown", "64-bit Capable", "Multi-Core", "Hardware Thread",
  "Execute Protection", "Enhanced Virtualization", "Power/Performance Control",
  "128-bit Capable", "Arm64 SoC ID", NULL, NULL, NULL, NULL, NULL, NULL,
};

STATIC CONST CHAR8  *mProcStatus[8] = {
  "Unknown", "CPU Enabled", "CPU Disabled by User (BIOS Setup)",
  "CPU Disabled by BIOS (POST Error)", "CPU Idle, Waiting to be Enabled",
  "Reserved", "Reserved", "Other",
};

#define ENUM_NAME(Table, Value) \
  ((((UINTN)(Value)) < ARRAY_SIZE (Table)) && ((Table)[(Value)] != NULL) ? (Table)[(Value)] : "Unknown")

CONST CHAR8 *
RwSmbiosTypeName (
  IN UINT8  Type
  )
{
  if (Type < ARRAY_SIZE (mTypeNames)) {
    return mTypeNames[Type];
  }

  if (Type == 126) {
    return "Inactive";
  }

  if (Type == RW_SMBIOS_TYPE_END) {
    return "End-of-Table";
  }

  return (Type >= 128) ? "OEM Specific" : "Unknown";
}

//
// ---- Table discovery ---------------------------------------------------------
//

STATIC
UINTN
StructTotalLength (
  IN CONST UINT8  *Ptr,
  IN CONST UINT8  *End
  )
{
  CONST UINT8  *Str;

  Str = Ptr + Ptr[1];
  while (Str + 1 < End) {
    if ((Str[0] == 0) && (Str[1] == 0)) {
      return (UINTN)(Str + 2 - Ptr);
    }

    Str++;
  }

  return 0;
}

/**
  Walk the structures from Ptr to End. With Structs == NULL only count them.
**/
STATIC
UINTN
WalkStructs (
  IN  CONST UINT8       *Ptr,
  IN  CONST UINT8       *End,
  OUT RW_SMBIOS_STRUCT  *Structs   OPTIONAL,
  OUT BOOLEAN           *Truncated
  )
{
  UINTN  Count;
  UINTN  Total;

  Count      = 0;
  *Truncated = FALSE;
  while (Ptr + sizeof (SMBIOS_STRUCTURE) <= End) {
    if (Count >= RW_SMBIOS_MAX_STRUCTS) {
      *Truncated = TRUE;
      break;
    }

    if (Ptr[1] < sizeof (SMBIOS_STRUCTURE)) {
      break;
    }

    Total = StructTotalLength (Ptr, End);
    if (Total == 0) {
      break;
    }

    if (Structs != NULL) {
      Structs[Count].Type        = Ptr[0];
      Structs[Count].Length      = Ptr[1];
      Structs[Count].Handle      = ReadUnaligned16 ((CONST UINT16 *)(Ptr + 2));
      Structs[Count].TotalLength = Total;
      Structs[Count].Data        = Ptr;
      Structs[Count].TypeName    = RwSmbiosTypeName (Ptr[0]);
    }

    Count++;
    if (Ptr[0] == RW_SMBIOS_TYPE_END) {
      break;
    }

    Ptr += Total;
  }

  return Count;
}

EFI_STATUS
RwSmbiosLoad (
  OUT RW_SMBIOS_TABLE  *Table
  )
{
  SMBIOS_TABLE_3_0_ENTRY_POINT  *Ep3;
  SMBIOS_TABLE_ENTRY_POINT      *Ep2;
  CONST UINT8                   *Ptr;
  CONST UINT8                   *End;
  UINTN                         Count;

  if (Table == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Table, sizeof (*Table));
  Ep3 = NULL;
  Ep2 = NULL;
  EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, (VOID **)&Ep3);
  EfiGetSystemConfigurationTable (&gEfiSmbiosTableGuid, (VOID **)&Ep2);

  if (Ep3 != NULL) {
    Table->Is64Bit      = TRUE;
    Table->MajorVersion = Ep3->MajorVersion;
    Table->MinorVersion = Ep3->MinorVersion;
    Table->DocRev       = Ep3->DocRev;
    Table->EntryPoint   = Ep3;
    Table->TableAddress = Ep3->TableAddress;
    Table->TableSize    = Ep3->TableMaximumSize;
    AsciiSPrint (
      Table->Summary,
      sizeof (Table->Summary),
      "SMBIOS %d.%d.%d (64-bit entry @ %p)  Table @ %08lX  Max size %d",
      Ep3->MajorVersion,
      Ep3->MinorVersion,
      Ep3->DocRev,
      Ep3,
      Ep3->TableAddress,
      Ep3->TableMaximumSize
      );
  } else if (Ep2 != NULL) {
    Table->Is64Bit       = FALSE;
    Table->MajorVersion  = Ep2->MajorVersion;
    Table->MinorVersion  = Ep2->MinorVersion;
    Table->EntryPoint    = Ep2;
    Table->TableAddress  = Ep2->TableAddress;
    Table->TableSize     = Ep2->TableLength;
    Table->EpStructCount = Ep2->NumberOfSmbiosStructures;
    AsciiSPrint (
      Table->Summary,
      sizeof (Table->Summary),
      "SMBIOS %d.%d (32-bit entry @ %p)  Table @ %08X  Length %d  Structures %d",
      Ep2->MajorVersion,
      Ep2->MinorVersion,
      Ep2,
      Ep2->TableAddress,
      Ep2->TableLength,
      Ep2->NumberOfSmbiosStructures
      );
  } else {
    return EFI_NOT_FOUND;
  }

  Ptr   = (CONST UINT8 *)(UINTN)Table->TableAddress;
  End   = Ptr + Table->TableSize;
  Count = WalkStructs (Ptr, End, NULL, &Table->Truncated);
  if (Count == 0) {
    return EFI_SUCCESS;
  }

  Table->Structs = AllocateZeroPool (Count * sizeof (RW_SMBIOS_STRUCT));
  if (Table->Structs == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Table->Count = WalkStructs (Ptr, End, Table->Structs, &Table->Truncated);
  return EFI_SUCCESS;
}

VOID
RwSmbiosFree (
  IN OUT RW_SMBIOS_TABLE  *Table
  )
{
  if (Table == NULL) {
    return;
  }

  if (Table->Structs != NULL) {
    FreePool (Table->Structs);
  }

  ZeroMem (Table, sizeof (*Table));
}

INTN
RwSmbiosFindType (
  IN CONST RW_SMBIOS_TABLE  *Table,
  IN UINT8                  Type,
  IN UINTN                  Start
  )
{
  UINTN  Index;

  if (Table == NULL) {
    return -1;
  }

  for (Index = Start; Index < Table->Count; Index++) {
    if (Table->Structs[Index].Type == Type) {
      return (INTN)Index;
    }
  }

  return -1;
}

/**
  String Number (1-based) of the string set at Data + Len, bounded by Total.
**/
STATIC
CONST CHAR8 *
SmbStringRaw (
  IN CONST UINT8  *Data,
  IN UINTN        Len,
  IN UINTN        Total,
  IN UINT8        Number
  )
{
  UINTN  Off;

  if (Number == 0) {
    return NULL;
  }

  Off = Len;
  while ((Off < Total) && (Data[Off] != '\0')) {
    if (--Number == 0) {
      return (CONST CHAR8 *)(Data + Off);
    }

    while ((Off < Total) && (Data[Off] != '\0')) {
      Off++;
    }

    Off++;
  }

  return NULL;
}

CONST CHAR8 *
RwSmbiosGetString (
  IN CONST RW_SMBIOS_STRUCT  *Struct,
  IN UINT8                   Number
  )
{
  if ((Struct == NULL) || (Struct->Data == NULL)) {
    return NULL;
  }

  return SmbStringRaw (Struct->Data, Struct->Length, Struct->TotalLength, Number);
}

VOID
RwSmbiosStructTitle (
  IN  CONST RW_SMBIOS_STRUCT  *Struct,
  OUT CHAR8                   *Buffer,
  IN  UINTN                   BufferSize
  )
{
  if ((Buffer == NULL) || (BufferSize == 0)) {
    return;
  }

  if (Struct == NULL) {
    Buffer[0] = '\0';
    return;
  }

  AsciiSPrint (
    Buffer,
    BufferSize,
    "Type %d (%02Xh)  %a   Handle %04Xh   Length %02Xh   @ %p",
    Struct->Type,
    Struct->Type,
    RwSmbiosTypeName (Struct->Type),
    Struct->Handle,
    Struct->Length,
    Struct->Data
    );
}

VOID
RwSmbiosEntryInfo (
  IN     CONST RW_SMBIOS_TABLE  *Table,
  IN OUT RW_INFO                *Info
  )
{
  if ((Table == NULL) || (Info == NULL)) {
    return;
  }

  if (Table->Is64Bit) {
    RwInfoField (Info, 0, RwEmStrong, "Version", "%d.%d.%d", Table->MajorVersion, Table->MinorVersion, Table->DocRev);
    RwInfoField (Info, 0, RwEmNormal, "Entry Point", "64-bit (SMBIOS 3.x) @ %p", Table->EntryPoint);
    RwInfoField (Info, 0, RwEmNormal, "Table Address", "%08lX", Table->TableAddress);
    RwInfoField (Info, 0, RwEmNormal, "Table Maximum Size", "%d", Table->TableSize);
  } else {
    RwInfoField (Info, 0, RwEmStrong, "Version", "%d.%d", Table->MajorVersion, Table->MinorVersion);
    RwInfoField (Info, 0, RwEmNormal, "Entry Point", "32-bit (SMBIOS 2.x) @ %p", Table->EntryPoint);
    RwInfoField (Info, 0, RwEmNormal, "Table Address", "%08lX", Table->TableAddress);
    RwInfoField (Info, 0, RwEmNormal, "Table Length", "%d", Table->TableSize);
    RwInfoField (Info, 0, RwEmNormal, "Structures (entry point)", "%d", Table->EpStructCount);
  }

  RwInfoField (
    Info,
    0,
    Table->Truncated ? RwEmAlert : RwEmNormal,
    "Structures found",
    Table->Truncated ? "%d (stopped at the limit)" : "%d",
    Table->Count
    );
}

//
// ---- Field helpers -------------------------------------------------------------
//

typedef struct {
  CONST UINT8    *P;
  UINTN          Len;     // formatted length
  UINTN          Total;   // formatted length + string set
  RW_INFO        *Info;
} SMB_CTX;

STATIC
BOOLEAN
Has (
  IN SMB_CTX  *C,
  IN UINTN    Offset,
  IN UINTN    Size
  )
{
  return (BOOLEAN)(Offset + Size <= C->Len);
}

STATIC
UINT8
B (
  IN SMB_CTX  *C,
  IN UINTN    Offset
  )
{
  return C->P[Offset];
}

STATIC
UINT16
W (
  IN SMB_CTX  *C,
  IN UINTN    Offset
  )
{
  return ReadUnaligned16 ((CONST UINT16 *)(C->P + Offset));
}

STATIC
UINT32
D (
  IN SMB_CTX  *C,
  IN UINTN    Offset
  )
{
  return ReadUnaligned32 ((CONST UINT32 *)(C->P + Offset));
}

STATIC
UINT64
Q (
  IN SMB_CTX  *C,
  IN UINTN    Offset
  )
{
  return ReadUnaligned64 ((CONST UINT64 *)(C->P + Offset));
}

STATIC
CONST CHAR8 *
SmbString (
  IN SMB_CTX  *C,
  IN UINT8    Number
  )
{
  return SmbStringRaw (C->P, C->Len, C->Total, Number);
}

STATIC
RW_INFO_ROW *
FieldV (
  IN SMB_CTX      *C,
  IN UINT8        Level,
  IN UINT8        Emphasis,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Fmt,
  IN VA_LIST      Args
  )
{
  RW_STR       Value;
  RW_INFO_ROW  *Row;

  RwStrInit (&Value);
  RwStrAppendV (&Value, Fmt, Args);
  Row = RwInfoAdd (C->Info, RwKindField, Level, Emphasis, Name, Value.Buf);
  RwStrFree (&Value);
  if (Size != 0) {
    RwInfoSetReg (Row, (UINT32)Offset, (UINT32)Size);
  }

  return Row;
}

/**
  Field of the decoded structure (level 1) that decodes Size bytes at Offset
  (Size 0: no register reference).
**/
STATIC
RW_INFO_ROW *
EFIAPI
Field (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Fmt,
  ...
  )
{
  VA_LIST      Args;
  RW_INFO_ROW  *Row;

  VA_START (Args, Fmt);
  Row = FieldV (C, 1, RwEmNormal, Offset, Size, Name, Fmt, Args);
  VA_END (Args);
  return Row;
}

/**
  Field with an emphasis.
**/
STATIC
RW_INFO_ROW *
EFIAPI
FieldEm (
  IN SMB_CTX      *C,
  IN UINT8        Emphasis,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Fmt,
  ...
  )
{
  VA_LIST      Args;
  RW_INFO_ROW  *Row;

  VA_START (Args, Fmt);
  Row = FieldV (C, 1, Emphasis, Offset, Size, Name, Fmt, Args);
  VA_END (Args);
  return Row;
}

/**
  Sub-field (level 2) detailing the field before it.
**/
STATIC
RW_INFO_ROW *
EFIAPI
SubField (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Fmt,
  ...
  )
{
  VA_LIST      Args;
  RW_INFO_ROW  *Row;

  VA_START (Args, Fmt);
  Row = FieldV (C, 2, RwEmNormal, Offset, Size, Name, Fmt, Args);
  VA_END (Args);
  return Row;
}

/**
  "[x] Name" line under a flags field.
**/
STATIC
VOID
FlagLine (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name
  )
{
  RwInfoSetReg (
    RwInfoText (C->Info, 2, RwEmNormal, "[x] %a", Name),
    (UINT32)Offset,
    (UINT32)Size
    );
}

STATIC
VOID
FStr (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name
  )
{
  CONST CHAR8  *Str;

  if (!Has (C, Offset, 1)) {
    return;
  }

  Str = SmbString (C, B (C, Offset));
  if (Str != NULL) {
    Field (C, Offset, 1, Name, "%a", Str);
  } else {
    FieldEm (C, RwEmDim, Offset, 1, Name, "(none)");
  }
}

STATIC
VOID
FByte (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name
  )
{
  if (Has (C, Offset, 1)) {
    Field (C, Offset, 1, Name, "%02Xh (%d)", B (C, Offset), B (C, Offset));
  }
}

STATIC
VOID
FWord (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name
  )
{
  if (Has (C, Offset, 2)) {
    Field (C, Offset, 2, Name, "%04Xh (%d)", W (C, Offset), W (C, Offset));
  }
}

STATIC
VOID
FHandle (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name
  )
{
  if (Has (C, Offset, 2)) {
    if ((W (C, Offset) == 0xFFFF) || (W (C, Offset) == 0xFFFE)) {
      FieldEm (C, RwEmDim, Offset, 2, Name, "%04Xh (not provided)", W (C, Offset));
    } else {
      Field (C, Offset, 2, Name, "%04Xh", W (C, Offset));
    }
  }
}

STATIC
VOID
FMhz (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Unit
  )
{
  if (Has (C, Offset, 2)) {
    if (W (C, Offset) == 0) {
      Field (C, Offset, 2, Name, "Unknown");
    } else {
      Field (C, Offset, 2, Name, "%d %a", W (C, Offset), Unit);
    }
  }
}

STATIC
VOID
FEnum (
  IN SMB_CTX            *C,
  IN UINTN              Offset,
  IN CONST CHAR8        *Name,
  IN CONST CHAR8* CONST *Table,
  IN UINTN              Count,
  IN UINT8              Mask
  )
{
  UINT8        Value;
  CONST CHAR8  *Text;

  if (!Has (C, Offset, 1)) {
    return;
  }

  Value = B (C, Offset) & Mask;
  Text  = ((Value < Count) && (Table[Value] != NULL)) ? Table[Value] : "Unknown";
  Field (C, Offset, 1, Name, "%02Xh  %a", B (C, Offset), Text);
}

#define FENUM(C, Off, Name, Table)  FEnum ((C), (Off), (Name), (Table), ARRAY_SIZE (Table), 0xFF)

STATIC
VOID
FBits (
  IN SMB_CTX            *C,
  IN UINTN              Offset,
  IN UINTN              Size,
  IN UINT64             Value,
  IN UINTN              Bits,
  IN CONST CHAR8* CONST *Names
  )
{
  UINTN  Bit;

  for (Bit = 0; Bit < Bits; Bit++) {
    if (((RShiftU64 (Value, Bit) & 1) != 0) && (Names[Bit] != NULL)) {
      FlagLine (C, Offset, Size, Names[Bit]);
    }
  }
}

STATIC
VOID
FUuid (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN CONST CHAR8  *Name
  )
{
  CONST UINT8  *U;

  if (!Has (C, Offset, 16)) {
    return;
  }

  //
  // First three fields are little-endian (SMBIOS 2.6+).
  //
  U = C->P + Offset;
  Field (
    C,
    Offset,
    16,
    Name,
    "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
    ReadUnaligned32 ((CONST UINT32 *)U),
    ReadUnaligned16 ((CONST UINT16 *)(U + 4)),
    ReadUnaligned16 ((CONST UINT16 *)(U + 6)),
    U[8],
    U[9],
    U[10],
    U[11],
    U[12],
    U[13],
    U[14],
    U[15]
    );
}

/**
  A size in KB with a friendly unit.
**/
STATIC
VOID
FSizeKb (
  IN SMB_CTX      *C,
  IN UINTN        Offset,
  IN UINTN        Size,
  IN CONST CHAR8  *Name,
  IN UINT64       Kb
  )
{
  if ((Kb >= SIZE_1MB) && ((Kb & (SIZE_1MB - 1)) == 0)) {
    Field (C, Offset, Size, Name, "%ld GB", RShiftU64 (Kb, 20));
  } else if ((Kb >= SIZE_1KB) && ((Kb & (SIZE_1KB - 1)) == 0)) {
    Field (C, Offset, Size, Name, "%ld MB", RShiftU64 (Kb, 10));
  } else {
    Field (C, Offset, Size, Name, "%ld KB", Kb);
  }
}

STATIC
UINT64
CacheSizeKb (
  IN UINT32   Raw,
  IN BOOLEAN  Is32
  )
{
  UINT32  Gran;
  UINT32  Val;

  if (Is32) {
    Gran = (Raw & BIT31) ? 64 : 1;
    Val  = Raw & 0x7FFFFFFF;
  } else {
    Gran = (Raw & BIT15) ? 64 : 1;
    Val  = Raw & 0x7FFF;
  }

  return MultU64x32 (Val, Gran);
}

//
// ---- Per-type decoders -------------------------------------------------------------
//

STATIC
VOID
DecodeType0 (
  IN SMB_CTX  *C
  )
{
  UINT16  Ext;

  FStr (C, 0x04, "Vendor");
  FStr (C, 0x05, "BIOS Version");
  if (Has (C, 0x06, 2)) {
    Field (C, 0x06, 2, "BIOS Starting Address Segment", "%04Xh", W (C, 0x06));
  }

  FStr (C, 0x08, "BIOS Release Date");
  if (Has (C, 0x09, 1)) {
    if (B (C, 0x09) == 0xFF) {
      Field (C, 0x09, 1, "BIOS ROM Size", "FFh (see Extended BIOS ROM Size)");
    } else {
      Field (C, 0x09, 1, "BIOS ROM Size", "%d KB", (B (C, 0x09) + 1) * 64);
    }
  }

  if (Has (C, 0x0A, 8)) {
    Field (C, 0x0A, 8, "BIOS Characteristics", "%016lXh", Q (C, 0x0A));
    FBits (C, 0x0A, 8, Q (C, 0x0A), 32, mBiosChar);
  }

  if (Has (C, 0x12, 1)) {
    Field (C, 0x12, 1, "BIOS Characteristics Ext Byte 1", "%02Xh", B (C, 0x12));
    FBits (C, 0x12, 1, B (C, 0x12), 8, mBiosExt1);
  }

  if (Has (C, 0x13, 1)) {
    Field (C, 0x13, 1, "BIOS Characteristics Ext Byte 2", "%02Xh", B (C, 0x13));
    FBits (C, 0x13, 1, B (C, 0x13), 8, mBiosExt2);
  }

  if (Has (C, 0x15, 1)) {
    Field (C, 0x14, 2, "System BIOS Release", "%d.%d", B (C, 0x14), B (C, 0x15));
  }

  if (Has (C, 0x17, 1)) {
    if ((B (C, 0x16) == 0xFF) && (B (C, 0x17) == 0xFF)) {
      FieldEm (C, RwEmDim, 0x16, 2, "Embedded Controller Firmware", "Not supported");
    } else {
      Field (C, 0x16, 2, "Embedded Controller Firmware", "%d.%d", B (C, 0x16), B (C, 0x17));
    }
  }

  if (Has (C, 0x18, 2)) {
    Ext = W (C, 0x18);
    Field (C, 0x18, 2, "Extended BIOS ROM Size", "%d %a", Ext & 0x3FFF, ((Ext >> 14) == 1) ? "GB" : "MB");
  }
}

STATIC
VOID
DecodeType1 (
  IN SMB_CTX  *C
  )
{
  FStr (C, 0x04, "Manufacturer");
  FStr (C, 0x05, "Product Name");
  FStr (C, 0x06, "Version");
  FStr (C, 0x07, "Serial Number");
  FUuid (C, 0x08, "UUID");
  FENUM (C, 0x18, "Wake-up Type", mWakeUpTypes);
  FStr (C, 0x19, "SKU Number");
  FStr (C, 0x1A, "Family");
}

STATIC
VOID
DecodeType2 (
  IN SMB_CTX  *C
  )
{
  UINT8  Flags;
  UINTN  Index;

  FStr (C, 0x04, "Manufacturer");
  FStr (C, 0x05, "Product");
  FStr (C, 0x06, "Version");
  FStr (C, 0x07, "Serial Number");
  FStr (C, 0x08, "Asset Tag");
  if (Has (C, 0x09, 1)) {
    Flags = B (C, 0x09);
    Field (C, 0x09, 1, "Feature Flags", "%02Xh", Flags);
    if (Flags & BIT0) {
      FlagLine (C, 0x09, 1, "Hosting board (motherboard)");
    }

    if (Flags & BIT1) {
      FlagLine (C, 0x09, 1, "Requires at least one daughter board");
    }

    if (Flags & BIT2) {
      FlagLine (C, 0x09, 1, "Removable");
    }

    if (Flags & BIT3) {
      FlagLine (C, 0x09, 1, "Replaceable");
    }

    if (Flags & BIT4) {
      FlagLine (C, 0x09, 1, "Hot swappable");
    }
  }

  FStr (C, 0x0A, "Location in Chassis");
  FHandle (C, 0x0B, "Chassis Handle");
  FENUM (C, 0x0D, "Board Type", mBoardTypes);
  if (Has (C, 0x0E, 1)) {
    Field (C, 0x0E, 1, "Number of Contained Object Handles", "%d", B (C, 0x0E));
    for (Index = 0; Index < B (C, 0x0E) && Has (C, 0x0F + Index * 2, 2); Index++) {
      SubField (C, 0x0F + Index * 2, 2, "Contained Handle", "%04Xh", W (C, 0x0F + Index * 2));
    }
  }
}

STATIC
VOID
DecodeType3 (
  IN SMB_CTX  *C
  )
{
  UINTN  SkuOff;

  FStr (C, 0x04, "Manufacturer");
  if (Has (C, 0x05, 1)) {
    Field (
      C,
      0x05,
      1,
      "Type",
      "%02Xh  %a%a",
      B (C, 0x05),
      ENUM_NAME (mChassisTypes, B (C, 0x05) & 0x7F),
      (B (C, 0x05) & 0x80) ? ", lock present" : ""
      );
  }

  FStr (C, 0x06, "Version");
  FStr (C, 0x07, "Serial Number");
  FStr (C, 0x08, "Asset Tag Number");
  FENUM (C, 0x09, "Boot-up State", mChassisStates);
  FENUM (C, 0x0A, "Power Supply State", mChassisStates);
  FENUM (C, 0x0B, "Thermal State", mChassisStates);
  FENUM (C, 0x0C, "Security Status", mChassisSecurity);
  if (Has (C, 0x0D, 4)) {
    Field (C, 0x0D, 4, "OEM-defined", "%08Xh", D (C, 0x0D));
  }

  if (Has (C, 0x11, 1)) {
    if (B (C, 0x11) == 0) {
      Field (C, 0x11, 1, "Height", "Unspecified");
    } else {
      Field (C, 0x11, 1, "Height", "%dU", B (C, 0x11));
    }
  }

  FByte (C, 0x12, "Number of Power Cords");
  FByte (C, 0x13, "Contained Element Count");
  FByte (C, 0x14, "Contained Element Record Length");
  if (Has (C, 0x14, 1)) {
    SkuOff = 0x15 + (UINTN)B (C, 0x13) * B (C, 0x14);
    FStr (C, SkuOff, "SKU Number");
  }
}

STATIC
VOID
DecodeType4 (
  IN SMB_CTX  *C
  )
{
  UINT8   Volt;
  UINT8   Status;
  UINT32  Eax;

  FStr (C, 0x04, "Socket Designation");
  FENUM (C, 0x05, "Processor Type", mProcTypes);
  FByte (C, 0x06, "Processor Family");
  FStr (C, 0x07, "Processor Manufacturer");
  if (Has (C, 0x08, 8)) {
    Eax = D (C, 0x08);
    Field (C, 0x08, 8, "Processor ID", "%08X %08X", D (C, 0x0C), Eax);
    SubField (
      C,
      0x08,
      4,
      "Signature",
      "Family %Xh, Model %Xh, Stepping %Xh",
      ((Eax >> 8) & 0xF) + (((Eax >> 8) & 0xF) == 0xF ? ((Eax >> 20) & 0xFF) : 0),
      ((Eax >> 4) & 0xF) | ((Eax >> 12) & 0xF0),
      Eax & 0xF
      );
  }

  FStr (C, 0x10, "Processor Version");
  if (Has (C, 0x11, 1)) {
    Volt = B (C, 0x11);
    if (Volt & BIT7) {
      Field (C, 0x11, 1, "Voltage", "%d.%d V", (Volt & 0x7F) / 10, (Volt & 0x7F) % 10);
    } else {
      Field (
        C,
        0x11,
        1,
        "Voltage",
        "%02Xh  %a%a%a",
        Volt,
        (Volt & BIT0) ? "5V " : "",
        (Volt & BIT1) ? "3.3V " : "",
        (Volt & BIT2) ? "2.9V" : ""
        );
    }
  }

  FMhz (C, 0x12, "External Clock", "MHz");
  FMhz (C, 0x14, "Max Speed", "MHz");
  FMhz (C, 0x16, "Current Speed", "MHz");
  if (Has (C, 0x18, 1)) {
    Status = B (C, 0x18);
    FieldEm (
      C,
      (Status & BIT6) ? RwEmStrong : RwEmDim,
      0x18,
      1,
      "Status",
      "%02Xh  Socket %a, %a",
      Status,
      (Status & BIT6) ? "Populated" : "Unpopulated",
      mProcStatus[Status & 7]
      );
  }

  FByte (C, 0x19, "Processor Upgrade");
  FHandle (C, 0x1A, "L1 Cache Handle");
  FHandle (C, 0x1C, "L2 Cache Handle");
  FHandle (C, 0x1E, "L3 Cache Handle");
  FStr (C, 0x20, "Serial Number");
  FStr (C, 0x21, "Asset Tag");
  FStr (C, 0x22, "Part Number");
  FByte (C, 0x23, "Core Count");
  FByte (C, 0x24, "Core Enabled");
  FByte (C, 0x25, "Thread Count");
  if (Has (C, 0x26, 2)) {
    Field (C, 0x26, 2, "Processor Characteristics", "%04Xh", W (C, 0x26));
    FBits (C, 0x26, 2, W (C, 0x26), 16, mProcChar);
  }

  FWord (C, 0x28, "Processor Family 2");
  FWord (C, 0x2A, "Core Count 2");
  FWord (C, 0x2C, "Core Enabled 2");
  FWord (C, 0x2E, "Thread Count 2");
  FWord (C, 0x30, "Thread Enabled");
}

STATIC
VOID
DecodeType7 (
  IN SMB_CTX  *C
  )
{
  UINT16                    Cfg;
  STATIC CONST CHAR8* CONST  Loc[]  = { "Internal", "External", "Reserved", "Unknown" };
  STATIC CONST CHAR8* CONST  Mode[] = { "Write Through", "Write Back", "Varies with Memory Address", "Unknown" };

  FStr (C, 0x04, "Socket Designation");
  if (Has (C, 0x05, 2)) {
    Cfg = W (C, 0x05);
    Field (C, 0x05, 2, "Cache Configuration", "%04Xh", Cfg);
    SubField (C, 0x05, 2, "Level", "L%d", (Cfg & 7) + 1);
    SubField (C, 0x05, 2, "Socketed", "%a", (Cfg & BIT3) ? "Yes" : "No");
    SubField (C, 0x05, 2, "Location", "%a", Loc[(Cfg >> 5) & 3]);
    SubField (C, 0x05, 2, "Enabled", "%a", (Cfg & BIT7) ? "Yes" : "No");
    SubField (C, 0x05, 2, "Operational Mode", "%a", Mode[(Cfg >> 8) & 3]);
  }

  if (Has (C, 0x07, 2)) {
    FSizeKb (C, 0x07, 2, "Maximum Cache Size", CacheSizeKb (W (C, 0x07), FALSE));
  }

  if (Has (C, 0x09, 2)) {
    FSizeKb (C, 0x09, 2, "Installed Size", CacheSizeKb (W (C, 0x09), FALSE));
  }

  if (Has (C, 0x0B, 2)) {
    Field (C, 0x0B, 2, "Supported SRAM Type", "%04Xh", W (C, 0x0B));
  }

  if (Has (C, 0x0D, 2)) {
    Field (C, 0x0D, 2, "Current SRAM Type", "%04Xh", W (C, 0x0D));
  }

  if (Has (C, 0x0F, 1)) {
    if (B (C, 0x0F) == 0) {
      Field (C, 0x0F, 1, "Cache Speed", "Unknown");
    } else {
      Field (C, 0x0F, 1, "Cache Speed", "%d ns", B (C, 0x0F));
    }
  }

  FENUM (C, 0x10, "Error Correction Type", mEccTypes);
  FENUM (C, 0x11, "System Cache Type", mCacheTypes);
  FENUM (C, 0x12, "Associativity", mCacheAssoc);
  if (Has (C, 0x13, 4)) {
    FSizeKb (C, 0x13, 4, "Maximum Cache Size 2", CacheSizeKb (D (C, 0x13), TRUE));
  }

  if (Has (C, 0x17, 4)) {
    FSizeKb (C, 0x17, 4, "Installed Cache Size 2", CacheSizeKb (D (C, 0x17), TRUE));
  }
}

STATIC
VOID
DecodeType8 (
  IN SMB_CTX  *C
  )
{
  FStr (C, 0x04, "Internal Reference Designator");
  FByte (C, 0x05, "Internal Connector Type");
  FStr (C, 0x06, "External Reference Designator");
  FByte (C, 0x07, "External Connector Type");
  FByte (C, 0x08, "Port Type");
}

STATIC
VOID
DecodeType9 (
  IN SMB_CTX  *C
  )
{
  FStr (C, 0x04, "Slot Designation");
  FByte (C, 0x05, "Slot Type");
  FENUM (C, 0x06, "Slot Data Bus Width", mSlotWidth);
  FENUM (C, 0x07, "Current Usage", mSlotUsage);
  FByte (C, 0x08, "Slot Length");
  FWord (C, 0x09, "Slot ID");
  FByte (C, 0x0B, "Slot Characteristics 1");
  FByte (C, 0x0C, "Slot Characteristics 2");
  if (Has (C, 0x11, 1)) {
    Field (
      C,
      0x0D,
      4,
      "Segment:Bus:Dev.Fn",
      "%04X:%02X:%02X.%X",
      W (C, 0x0D),
      B (C, 0x0F),
      B (C, 0x10) >> 3,
      B (C, 0x10) & 7
      );
  }
}

STATIC
VOID
DecodeStringList (
  IN SMB_CTX      *C,
  IN CONST CHAR8  *CountName
  )
{
  UINTN        Index;
  CONST CHAR8  *Str;

  if (!Has (C, 0x04, 1)) {
    return;
  }

  Field (C, 0x04, 1, CountName, "%d", B (C, 0x04));
  for (Index = 1; Index <= B (C, 0x04); Index++) {
    Str = SmbString (C, (UINT8)Index);
    RwInfoText (
      C->Info,
      2,
      (Str != NULL) ? RwEmNormal : RwEmAlert,
      "%2d: %a",
      Index,
      (Str != NULL) ? Str : "(missing)"
      );
  }
}

STATIC
VOID
DecodeType13 (
  IN SMB_CTX  *C
  )
{
  FByte (C, 0x04, "Installable Languages");
  if (Has (C, 0x05, 1)) {
    Field (C, 0x05, 1, "Language Format", "%a", (B (C, 0x05) & BIT0) ? "Abbreviated" : "Long");
  }

  FStr (C, 0x15, "Current Language");
}

STATIC
VOID
DecodeType16 (
  IN SMB_CTX  *C
  )
{
  FENUM (C, 0x04, "Location", mArrayLocation);
  FENUM (C, 0x05, "Use", mArrayUse);
  FENUM (C, 0x06, "Memory Error Correction", mEccTypes);
  if (Has (C, 0x07, 4)) {
    if ((D (C, 0x07) == 0x80000000) && Has (C, 0x0F, 8)) {
      FSizeKb (C, 0x0F, 8, "Maximum Capacity", RShiftU64 (Q (C, 0x0F), 10));
    } else {
      FSizeKb (C, 0x07, 4, "Maximum Capacity", D (C, 0x07));
    }
  }

  FHandle (C, 0x0B, "Memory Error Information Handle");
  FWord (C, 0x0D, "Number of Memory Devices");
}

STATIC
VOID
DecodeType17 (
  IN SMB_CTX  *C
  )
{
  UINT16  Size;
  UINT16  Detail;
  UINTN   Bit;

  FHandle (C, 0x04, "Physical Memory Array Handle");
  FHandle (C, 0x06, "Memory Error Information Handle");
  if (Has (C, 0x08, 2)) {
    Field (C, 0x08, 2, "Total Width", (W (C, 0x08) == 0xFFFF) ? "Unknown" : "%d bits", W (C, 0x08));
  }

  if (Has (C, 0x0A, 2)) {
    Field (C, 0x0A, 2, "Data Width", (W (C, 0x0A) == 0xFFFF) ? "Unknown" : "%d bits", W (C, 0x0A));
  }

  if (Has (C, 0x0C, 2)) {
    Size = W (C, 0x0C);
    if (Size == 0) {
      FieldEm (C, RwEmDim, 0x0C, 2, "Size", "No Module Installed");
    } else if (Size == 0xFFFF) {
      Field (C, 0x0C, 2, "Size", "Unknown");
    } else if ((Size == 0x7FFF) && Has (C, 0x1C, 4)) {
      FSizeKb (C, 0x1C, 4, "Size", LShiftU64 (D (C, 0x1C) & 0x7FFFFFFF, 10));
    } else if (Size & BIT15) {
      FSizeKb (C, 0x0C, 2, "Size", Size & 0x7FFF);
    } else {
      FSizeKb (C, 0x0C, 2, "Size", LShiftU64 (Size, 10));
    }
  }

  FENUM (C, 0x0E, "Form Factor", mMemFormFactors);
  FByte (C, 0x0F, "Device Set");
  FStr (C, 0x10, "Device Locator");
  FStr (C, 0x11, "Bank Locator");
  FENUM (C, 0x12, "Memory Type", mMemTypes);
  if (Has (C, 0x13, 2)) {
    Detail = W (C, 0x13);
    Field (C, 0x13, 2, "Type Detail", "%04Xh", Detail);
    for (Bit = 1; Bit < 16; Bit++) {
      if (Detail & (1 << Bit)) {
        FlagLine (C, 0x13, 2, mMemTypeDetail[Bit]);
      }
    }
  }

  if (Has (C, 0x15, 2)) {
    if ((W (C, 0x15) == 0xFFFF) && Has (C, 0x54, 4)) {
      Field (C, 0x54, 4, "Speed", "%d MT/s", D (C, 0x54));
    } else {
      FMhz (C, 0x15, "Speed", "MT/s");
    }
  }

  FStr (C, 0x17, "Manufacturer");
  FStr (C, 0x18, "Serial Number");
  FStr (C, 0x19, "Asset Tag");
  FStr (C, 0x1A, "Part Number");
  if (Has (C, 0x1B, 1)) {
    if ((B (C, 0x1B) & 0x0F) == 0) {
      Field (C, 0x1B, 1, "Rank", "Unknown");
    } else {
      Field (C, 0x1B, 1, "Rank", "%d", B (C, 0x1B) & 0x0F);
    }
  }

  if (Has (C, 0x20, 2)) {
    if ((W (C, 0x20) == 0xFFFF) && Has (C, 0x58, 4)) {
      Field (C, 0x58, 4, "Configured Memory Speed", "%d MT/s", D (C, 0x58));
    } else {
      FMhz (C, 0x20, "Configured Memory Speed", "MT/s");
    }
  }

  FMhz (C, 0x22, "Minimum Voltage", "mV");
  FMhz (C, 0x24, "Maximum Voltage", "mV");
  FMhz (C, 0x26, "Configured Voltage", "mV");
  FByte (C, 0x28, "Memory Technology");
  FWord (C, 0x2B, "Module Manufacturer ID");
  FWord (C, 0x2D, "Module Product ID");
  FWord (C, 0x2F, "Memory Subsystem Ctrl Mfr ID");
  FWord (C, 0x31, "Memory Subsystem Ctrl Product ID");
}

STATIC
VOID
DecodeType19 (
  IN SMB_CTX  *C
  )
{
  if (!Has (C, 0x0F, 1)) {
    return;
  }

  if ((D (C, 0x04) == 0xFFFFFFFF) && Has (C, 0x17, 8)) {
    Field (C, 0x0F, 8, "Starting Address", "%016lXh", Q (C, 0x0F));
    Field (C, 0x17, 8, "Ending Address", "%016lXh", Q (C, 0x17));
    FSizeKb (C, 0x0F, 16, "Range Size", RShiftU64 (Q (C, 0x17) - Q (C, 0x0F) + 1, 10));
  } else {
    Field (C, 0x04, 4, "Starting Address", "%016lXh", LShiftU64 (D (C, 0x04), 10));
    Field (C, 0x08, 4, "Ending Address", "%016lXh", LShiftU64 (D (C, 0x08), 10) + 0x3FF);
    FSizeKb (C, 0x04, 8, "Range Size", (UINT64)D (C, 0x08) - D (C, 0x04) + 1);
  }

  FHandle (C, 0x0C, "Memory Array Handle");
  FByte (C, 0x0E, "Partition Width");
}

STATIC
VOID
DecodeType32 (
  IN SMB_CTX  *C
  )
{
  if (Has (C, 0x0A, 1)) {
    FieldEm (
      C,
      (B (C, 0x0A) == 0) ? RwEmNormal : RwEmAlert,
      0x0A,
      1,
      "Boot Status",
      "%02Xh  %a",
      B (C, 0x0A),
      (B (C, 0x0A) == 0) ? "No errors detected" : "See specification"
      );
  }
}

STATIC
VOID
DecodeType41 (
  IN SMB_CTX  *C
  )
{
  FStr (C, 0x04, "Reference Designation");
  if (Has (C, 0x05, 1)) {
    Field (
      C,
      0x05,
      1,
      "Device Type",
      "%02Xh  %a, %a",
      B (C, 0x05),
      ENUM_NAME (mOnboardTypes, B (C, 0x05) & 0x7F),
      (B (C, 0x05) & BIT7) ? "Enabled" : "Disabled"
      );
  }

  FByte (C, 0x06, "Device Type Instance");
  if (Has (C, 0x0A, 1)) {
    Field (
      C,
      0x07,
      4,
      "Segment:Bus:Dev.Fn",
      "%04X:%02X:%02X.%X",
      W (C, 0x07),
      B (C, 0x09),
      B (C, 0x0A) >> 3,
      B (C, 0x0A) & 7
      );
  }
}

//
// ---- Structure decode and dump ---------------------------------------------------
//

EFI_STATUS
RwSmbiosDecodeStruct (
  IN     CONST RW_SMBIOS_STRUCT  *Struct,
  IN OUT RW_INFO                 *Info
  )
{
  SMB_CTX      Ctx;
  CONST CHAR8  *Str;
  UINTN        Count;
  CHAR8        Name[8];

  if ((Struct == NULL) || (Info == NULL) || (Struct->Data == NULL) ||
      (Struct->Length < sizeof (SMBIOS_STRUCTURE)) || (Struct->TotalLength < (UINTN)Struct->Length + 2))
  {
    return EFI_INVALID_PARAMETER;
  }

  Ctx.P     = Struct->Data;
  Ctx.Len   = Struct->Length;
  Ctx.Total = Struct->TotalLength;
  Ctx.Info  = Info;

  RwInfoSection (Info, 0, "Decoded fields");
  switch (Struct->Type) {
    case 0: DecodeType0 (&Ctx);
      break;
    case 1: DecodeType1 (&Ctx);
      break;
    case 2: DecodeType2 (&Ctx);
      break;
    case 3: DecodeType3 (&Ctx);
      break;
    case 4: DecodeType4 (&Ctx);
      break;
    case 7: DecodeType7 (&Ctx);
      break;
    case 8: DecodeType8 (&Ctx);
      break;
    case 9: DecodeType9 (&Ctx);
      break;
    case 11: DecodeStringList (&Ctx, "Count");
      break;
    case 12: DecodeStringList (&Ctx, "Count");
      break;
    case 13: DecodeType13 (&Ctx);
      break;
    case 16: DecodeType16 (&Ctx);
      break;
    case 17: DecodeType17 (&Ctx);
      break;
    case 19: DecodeType19 (&Ctx);
      break;
    case 32: DecodeType32 (&Ctx);
      break;
    case 41: DecodeType41 (&Ctx);
      break;
    default:
      RwInfoText (Info, 1, RwEmDim, "(no decoder for this type; see raw data)");
      break;
  }

  RwInfoSection (Info, 0, "Strings");
  Count = 0;
  while ((Str = SmbString (&Ctx, (UINT8)(Count + 1))) != NULL) {
    AsciiSPrint (Name, sizeof (Name), "%d", Count + 1);
    RwInfoAdd (Info, RwKindField, 1, RwEmNormal, Name, Str);
    Count++;
    if (Count >= 255) {
      break;
    }
  }

  if (Count == 0) {
    RwInfoText (Info, 1, RwEmDim, "(none)");
  }

  RwInfoSection (Info, 0, "Raw data (formatted area, %d bytes)", Ctx.Len);
  RwInfoAddHex (Info, 1, Ctx.P, Ctx.Len, 0);
  if (Ctx.Total > Ctx.Len) {
    RwInfoSection (Info, 0, "Raw data (string set, %d bytes)", Ctx.Total - Ctx.Len);
    RwInfoAddHex (Info, 1, Ctx.P + Ctx.Len, Ctx.Total - Ctx.Len, Ctx.Len);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
RwSmbiosDecode (
  IN     CONST RW_SMBIOS_TABLE  *Table,
  IN     UINTN                  Index,
  IN OUT RW_INFO                *Info
  )
{
  if ((Table == NULL) || (Info == NULL) || (Index >= Table->Count)) {
    return EFI_INVALID_PARAMETER;
  }

  return RwSmbiosDecodeStruct (&Table->Structs[Index], Info);
}

EFI_STATUS
RwSmbiosDump (
  IN OUT RW_INFO  *Info
  )
{
  RW_SMBIOS_TABLE  Table;
  RW_INFO          One;
  EFI_STATUS       Status;
  UINTN            Index;
  CHAR8            Title[160];

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = RwSmbiosLoad (&Table);
  if (EFI_ERROR (Status)) {
    RwInfoSection (Info, 0, (Status == EFI_NOT_FOUND) ? "SMBIOS: not found" : "SMBIOS: %r", Status);
    return Status;
  }

  RwInfoSection (Info, 0, "SMBIOS (%d structures)", Table.Count);
  RwInfoText (Info, 1, RwEmNormal, "%a", Table.Summary);
  for (Index = 0; Index < Table.Count; Index++) {
    RwSmbiosStructTitle (&Table.Structs[Index], Title, sizeof (Title));
    RwInfoAdd (Info, RwKindSection, 0, RwEmNormal, "SMBIOS Structure", Title);
    RwInfoInit (&One);
    RwSmbiosDecodeStruct (&Table.Structs[Index], &One);
    RwInfoAppend (Info, &One, 1);
    if (One.Oom) {
      Info->Oom = TRUE;
    }

    RwInfoFree (&One);
  }

  RwSmbiosFree (&Table);
  return EFI_SUCCESS;
}
