/** @file
  RwCoreLib SPD: reading (DDR4 / DDR5 paging) and decoding of DDR3, DDR4 and
  DDR5 serial presence detect data (JEDEC 21-C 4.1.2.11, 4.1.2.L-x, SPD5).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define DDR4_SPA0  0x36
#define DDR4_SPA1  0x37
#define SPD5_MR0   0x00
#define SPD5_MR1   0x01
#define SPD5_MR11  0x0B

//
// ---- Reading -------------------------------------------------------------
//

STATIC
EFI_STATUS
ReadBlock (
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr,
  IN  UINT8           First,
  IN  UINTN           Count,
  OUT UINT8           *Data
  )
{
  UINTN       Index;
  EFI_STATUS  Status;

  for (Index = 0; Index < Count; Index++) {
    Status = RwSmbusReadByte (Bus, Addr, (UINT8)(First + Index), &Data[Index]);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
IsSpd5Hub (
  IN CONST RW_SMBUS  *Bus,
  IN UINT8           Addr
  )
{
  UINT8  Mr0;
  UINT8  Mr1;

  //
  // SPD5 hub (JESD300): MR0 = 51h (device type MSB), MR1 = 18h (SPD5118).
  //
  if (EFI_ERROR (RwSmbusReadByte (Bus, Addr, SPD5_MR0, &Mr0)) || EFI_ERROR (RwSmbusReadByte (Bus, Addr, SPD5_MR1, &Mr1))) {
    return FALSE;
  }

  return (BOOLEAN)((Mr0 == 0x51) && ((Mr1 & 0xF0) == 0x10));
}

EFI_STATUS
RwSpdRead (
  IN  CONST RW_SMBUS  *Bus,
  IN  UINT8           Addr,
  OUT RW_SPD          *Spd
  )
{
  EFI_STATUS  Status;
  UINT8       Page;
  UINT8       Mr11;

  ZeroMem (Spd, sizeof (*Spd));
  Spd->Addr = Addr;

  if (IsSpd5Hub (Bus, Addr)) {
    //
    // DDR5: eight 128-byte pages, the selected one at 80h-FFh (legacy mode,
    // MR11 bits 2:0). Put back the page that was selected.
    //
    if (EFI_ERROR (RwSmbusReadByte (Bus, Addr, SPD5_MR11, &Mr11))) {
      return EFI_DEVICE_ERROR;
    }

    Status = EFI_SUCCESS;
    for (Page = 0; Page < 8; Page++) {
      Status = RwSmbusWriteByte (Bus, Addr, SPD5_MR11, (UINT8)((Mr11 & ~0x07) | Page));
      if (!EFI_ERROR (Status)) {
        Status = ReadBlock (Bus, Addr, 0x80, 128, &Spd->Data[Page * 128]);
      }

      if (EFI_ERROR (Status)) {
        break;
      }

      Spd->Size = (Page + 1) * 128;
    }

    RwSmbusWriteByte (Bus, Addr, SPD5_MR11, Mr11);
    Spd->Type = Spd->Data[2];
    return (Spd->Size > 0) ? EFI_SUCCESS : Status;
  }

  Status = ReadBlock (Bus, Addr, 0, 256, Spd->Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Spd->Size = 256;
  Spd->Type = Spd->Data[2];
  if (Spd->Type == RW_SPD_DDR4) {
    //
    // DDR4: the upper 256 bytes are page 1 (set page address 37h), page 0
    // is selected again afterwards (36h).
    //
    if (!EFI_ERROR (RwSmbusQuickWrite (Bus, DDR4_SPA1))) {
      if (!EFI_ERROR (ReadBlock (Bus, Addr, 0, 256, &Spd->Data[256]))) {
        Spd->Size = 512;
      }

      RwSmbusQuickWrite (Bus, DDR4_SPA0);
    }
  }

  return EFI_SUCCESS;
}

//
// ---- Names -----------------------------------------------------------------
//

VOID
RwSpdTypeName (
  IN  UINT8  Type,
  OUT CHAR8  *Buffer,
  IN  UINTN  BufferSize
  )
{
  STATIC CONST CHAR8  *Names[] = {
    NULL,           "FPM DRAM",   "EDO",            "Pipelined Nibble", "SDRAM",       "ROM",
    "DDR SGRAM",    "DDR SDRAM",  "DDR2 SDRAM",     "DDR2 FB-DIMM",     "DDR2 FB-DIMM PROBE", "DDR3 SDRAM",
    "DDR4 SDRAM",   NULL,         "DDR4E SDRAM",    "LPDDR3 SDRAM",     "LPDDR4 SDRAM", "LPDDR4X SDRAM",
    "DDR5 SDRAM",   "LPDDR5 SDRAM", "DDR5 NVDIMM-P", "LPDDR5X SDRAM"
  };

  if ((Type < ARRAY_SIZE (Names)) && (Names[Type] != NULL)) {
    AsciiStrCpyS (Buffer, BufferSize, Names[Type]);
  } else {
    AsciiSPrint (Buffer, BufferSize, "Unknown (%02Xh)", Type);
  }
}

typedef struct {
  UINT8          Bank;    // 1-based bank (number of continuation codes + 1)
  UINT8          Id;      // without parity
  CONST CHAR8    *Name;
} JEDEC_ID;

//
// Common module and DRAM makers, as SPD stores them (bank byte, ID byte):
// Samsung 80 CE, SK hynix 80 AD, Micron 80 2C, Kingston 01 98, Corsair 02 9E,
// Crucial 85 9B, G.Skill 04 CD, ADATA 04 CB, Team 04 EF.
//
STATIC CONST JEDEC_ID  mJedec[] = {
  { 1, 0x01, "AMD"        },
  { 1, 0x09, "Intel"      },
  { 1, 0x2C, "Micron"     },
  { 1, 0x2D, "SK hynix"   },
  { 1, 0x4E, "Samsung"    },
  { 2, 0x18, "Kingston"   },
  { 2, 0x4F, "Transcend"  },
  { 2, 0x7A, "Apacer"     },
  { 3, 0x1E, "Corsair"    },
  { 4, 0x0B, "Nanya"      },
  { 5, 0x4B, "ADATA"      },
  { 5, 0x4D, "G.Skill"    },
  { 5, 0x6F, "Team Group" },
  { 6, 0x02, "Patriot"    },
  { 6, 0x1B, "Crucial"    }
};

CONST CHAR8 *
RwSpdManufacturer (
  IN UINT8  Bank,
  IN UINT8  Id
  )
{
  UINTN  Index;
  UINT8  B;

  B = (UINT8)((Bank & 0x7F) + 1);
  for (Index = 0; Index < ARRAY_SIZE (mJedec); Index++) {
    if ((mJedec[Index].Bank == B) && (mJedec[Index].Id == (Id & 0x7F))) {
      return mJedec[Index].Name;
    }
  }

  return NULL;
}

UINT16
RwSpdCrc16 (
  IN CONST UINT8  *Data,
  IN UINTN        Size
  )
{
  UINT16  Crc;
  UINTN   Index;
  UINTN   Bit;

  Crc = 0;
  for (Index = 0; Index < Size; Index++) {
    Crc ^= (UINT16)(Data[Index] << 8);
    for (Bit = 0; Bit < 8; Bit++) {
      Crc = (UINT16)(((Crc & 0x8000) != 0) ? ((Crc << 1) ^ 0x1021) : (Crc << 1));
    }
  }

  return Crc;
}

//
// ---- Layout of the fields that differ per generation ----------------------
//
typedef struct {
  UINT32    MfgBank;       // module manufacturer, 2 bytes
  UINT32    MfgDate;       // BCD year, BCD week
  UINT32    Serial;        // 4 bytes
  UINT32    Part;
  UINT32    PartLen;
  UINT32    DramMfg;       // DRAM manufacturer, 2 bytes
  UINT32    CrcEnd;        // CRC covers 0..CrcEnd-1, stored at CrcEnd
} SPD_LAYOUT;

STATIC CONST SPD_LAYOUT  mDdr3 = { 117, 120, 122, 128, 18, 148, 126 };
STATIC CONST SPD_LAYOUT  mDdr4 = { 320, 323, 325, 329, 20, 350, 126 };
STATIC CONST SPD_LAYOUT  mDdr5 = { 512, 515, 517, 521, 30, 552, 510 };

STATIC
CONST SPD_LAYOUT *
Layout (
  IN CONST RW_SPD  *Spd
  )
{
  switch (Spd->Type) {
    case RW_SPD_DDR3:
      return (Spd->Size >= 256) ? &mDdr3 : NULL;
    case RW_SPD_DDR4:
      return (Spd->Size >= 512) ? &mDdr4 : NULL;
    case RW_SPD_DDR5:
      return (Spd->Size >= 1024) ? &mDdr5 : NULL;
    default:
      return NULL;
  }
}

STATIC
CONST CHAR8 *
ModuleTypeName (
  IN CONST RW_SPD  *Spd
  )
{
  STATIC CONST CHAR8  *Ddr34[] = {
    "Extended", "RDIMM", "UDIMM", "SO-DIMM", "LRDIMM", "Mini-RDIMM", "Mini-UDIMM", "Unknown",
    "72b-SO-RDIMM", "72b-SO-UDIMM", "Unknown", "Unknown", "16b-SO-DIMM", "32b-SO-DIMM", "Unknown", "Unknown"
  };
  STATIC CONST CHAR8  *Ddr5[] = {
    "Unknown", "RDIMM", "UDIMM", "SO-DIMM", "LRDIMM", "CUDIMM", "CSODIMM", "MRDIMM",
    "CAMM2", "Unknown", "DDIMM", "Solder down", "Unknown", "Unknown", "Unknown", "Unknown"
  };

  return (Spd->Type == RW_SPD_DDR5) ? Ddr5[Spd->Data[3] & 0x0F] : Ddr34[Spd->Data[3] & 0x0F];
}

//
// Capacity in MB, 0 when it cannot be worked out.
//
STATIC
UINT64
CapacityMb (
  IN CONST RW_SPD  *Spd
  )
{
  CONST UINT8  *D;
  UINT64       DieMbit;
  UINT32       DevWidth;
  UINT32       BusWidth;
  UINT32       Ranks;
  UINT32       Dies;
  UINT32       SubCh;
  STATIC CONST UINT32  Ddr5Density[] = { 0, 4, 8, 12, 16, 24, 32, 48, 64 };

  D = Spd->Data;
  switch (Spd->Type) {
    case RW_SPD_DDR3:
      DieMbit  = 256ULL << (D[4] & 0x0F);
      DevWidth = 4U << (D[7] & 0x07);
      Ranks    = ((D[7] >> 3) & 0x07) + 1;
      BusWidth = 8U << (D[8] & 0x07);
      return DieMbit / 8 * BusWidth / DevWidth * Ranks;

    case RW_SPD_DDR4:
      DieMbit  = 256ULL << (D[4] & 0x0F);
      DevWidth = 4U << (D[12] & 0x07);
      Ranks    = ((D[12] >> 3) & 0x07) + 1;
      BusWidth = 8U << (D[13] & 0x07);
      Dies     = ((D[6] & 0x80) != 0) ? ((D[6] >> 4) & 0x07) + 1 : 1;   // 3DS stacks
      return DieMbit / 8 * BusWidth / DevWidth * Ranks * (((D[6] & 0x03) == 0x02) ? Dies : 1);

    case RW_SPD_DDR5:
      if ((D[4] & 0x1F) >= ARRAY_SIZE (Ddr5Density)) {
        return 0;
      }

      DieMbit  = (UINT64)Ddr5Density[D[4] & 0x1F] * 1024;
      Dies     = ((D[4] >> 5) == 0) ? 1 : (1U << ((D[4] >> 5) - 1));
      DevWidth = 4U << ((D[6] >> 5) & 0x07);
      Ranks    = ((D[234] >> 3) & 0x07) + 1;
      BusWidth = 8U << (D[235] & 0x07);
      SubCh    = ((D[235] >> 5) & 0x03) + 1;
      return DieMbit / 8 * Dies * (BusWidth / DevWidth) * SubCh * Ranks;

    default:
      return 0;
  }
}

//
// Minimum clock period in picoseconds, 0 when unknown.
//
STATIC
UINT32
TckPs (
  IN CONST RW_SPD  *Spd
  )
{
  CONST UINT8  *D;
  INT32        Ps;
  UINT32       MtbFs;

  D = Spd->Data;
  switch (Spd->Type) {
    case RW_SPD_DDR3:
      //
      // MTB = dividend / divisor ns (normally 1/8 ns = 125 ps), fine offset in ps.
      //
      if (D[11] == 0) {
        return 0;
      }

      MtbFs = (UINT32)D[10] * 1000000 / D[11];              // femtoseconds
      Ps    = (INT32)(D[12] * MtbFs / 1000) + (INT8)D[34];
      return (Ps > 0) ? (UINT32)Ps : 0;

    case RW_SPD_DDR4:
      Ps = (INT32)D[18] * 125 + (INT8)D[125];
      return (Ps > 0) ? (UINT32)Ps : 0;

    case RW_SPD_DDR5:
      return (UINT32)(D[20] | (D[21] << 8));

    default:
      return 0;
  }
}

STATIC
UINT32
SpeedMts (
  IN UINT32  Tck
  )
{
  UINT32  Mts;

  if (Tck == 0) {
    return 0;
  }

  //
  // Round to the JEDEC speed grade (e.g. 1250 ps -> 1600, 625 ps -> 3200).
  //
  Mts = (2000000 + Tck / 2) / Tck;
  if ((Mts % 100) >= 90) {
    Mts += 100 - (Mts % 100);
  } else if ((Mts % 100) <= 10) {
    Mts -= Mts % 100;
  } else if ((Mts % 100 > 25) && (Mts % 100 < 42)) {
    Mts = Mts - (Mts % 100) + 33;
  } else if ((Mts % 100 > 58) && (Mts % 100 < 75)) {
    Mts = Mts - (Mts % 100) + 66;
  }

  return Mts;
}

STATIC
VOID
CopyText (
  IN  CONST UINT8  *Src,
  IN  UINTN        Len,
  OUT CHAR8        *Dst,
  IN  UINTN        DstSize
  )
{
  UINTN  Index;
  UINTN  Out;

  Out = 0;
  for (Index = 0; (Index < Len) && (Out + 1 < DstSize); Index++) {
    Dst[Out++] = ((Src[Index] >= 0x20) && (Src[Index] < 0x7F)) ? (CHAR8)Src[Index] : ' ';
  }

  while ((Out > 0) && (Dst[Out - 1] == ' ')) {
    Out--;
  }

  Dst[Out] = '\0';
}

STATIC
BOOLEAN
IsBlank (
  IN CONST RW_SPD  *Spd
  )
{
  UINTN  Index;

  for (Index = 0; Index < Spd->Size; Index++) {
    if ((Spd->Data[Index] != 0x00) && (Spd->Data[Index] != 0xFF)) {
      return FALSE;
    }
  }

  return TRUE;
}

VOID
RwSpdSummary (
  IN  CONST RW_SPD  *Spd,
  OUT CHAR8         *Buffer,
  IN  UINTN         BufferSize
  )
{
  CONST SPD_LAYOUT  *L;
  CHAR8             Type[32];
  CHAR8             Part[40];
  CONST CHAR8       *Mfg;
  UINT64            Mb;

  if (IsBlank (Spd)) {
    AsciiStrCpyS (Buffer, BufferSize, "Blank SPD");
    return;
  }

  L = Layout (Spd);
  if (L == NULL) {
    AsciiSPrint (Buffer, BufferSize, "Unknown SPD type %02Xh", Spd->Type);
    return;
  }

  RwSpdTypeName (Spd->Type, Type, sizeof (Type));
  Type[4] = '\0';                                            // "DDR4"
  CopyText (&Spd->Data[L->Part], L->PartLen, Part, sizeof (Part));
  Mfg = RwSpdManufacturer (Spd->Data[L->MfgBank], Spd->Data[L->MfgBank + 1]);
  Mb  = CapacityMb (Spd);
  AsciiSPrint (
    Buffer,
    BufferSize,
    "%a %a %Ld GB %d MT/s  %a %a",
    Type,
    ModuleTypeName (Spd),
    Mb / 1024,
    SpeedMts (TckPs (Spd)),
    (Mfg != NULL) ? Mfg : "",
    Part
    );
}

//
// ---- Decode ----------------------------------------------------------------
//

STATIC
VOID
Field (
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

VOID
RwSpdDecode (
  IN     CONST RW_SPD  *Spd,
  IN OUT RW_INFO       *Info
  )
{
  CONST UINT8       *D;
  CONST SPD_LAYOUT  *L;
  CHAR8             Text[96];
  CHAR8             Part[40];
  CONST CHAR8       *Mfg;
  UINT32            Tck;
  UINT16            Crc;
  UINT16            Stored;
  UINT32            CrcStart;

  D = Spd->Data;
  RwInfoSection (Info, 0, "SPD at SMBus %02Xh", Spd->Addr);
  RwSpdTypeName (Spd->Type, Text, sizeof (Text));
  Field (Info, 2, 1, RwEmStrong, "Memory type", Text);
  if (IsBlank (Spd)) {
    RwInfoText (Info, 1, RwEmDim, "The EEPROM is blank (all 00h / FFh).");
    RwInfoSection (Info, 0, "Raw (%d bytes)", Spd->Size);
    RwInfoAddHex (Info, 1, D, Spd->Size, 0);
    return;
  }

  L = Layout (Spd);
  if (L == NULL) {
    RwInfoText (Info, 1, RwEmDim, "This SPD type is not decoded.");
    RwInfoSection (Info, 0, "Raw (%d bytes)", Spd->Size);
    RwInfoAddHex (Info, 1, D, Spd->Size, 0);
    return;
  }

  AsciiSPrint (Text, sizeof (Text), "%d.%d", D[1] >> 4, D[1] & 0x0F);
  Field (Info, 1, 1, RwEmNormal, "SPD revision", Text);
  Field (Info, 3, 1, RwEmNormal, "Module type", ModuleTypeName (Spd));
  AsciiSPrint (Text, sizeof (Text), "%Ld MB", CapacityMb (Spd));
  Field (Info, (Spd->Type == RW_SPD_DDR5) ? 4 : 4, 1, RwEmStrong, "Module size", Text);

  switch (Spd->Type) {
    case RW_SPD_DDR3:
      AsciiSPrint (Text, sizeof (Text), "%d Mbit, %d banks", 256 << (D[4] & 0x0F), 8 << ((D[4] >> 4) & 0x07));
      Field (Info, 4, 1, RwEmNormal, "SDRAM density", Text);
      AsciiSPrint (Text, sizeof (Text), "x%d, %d rank(s)", 4 << (D[7] & 0x07), ((D[7] >> 3) & 0x07) + 1);
      Field (Info, 7, 1, RwEmNormal, "Organization", Text);
      AsciiSPrint (Text, sizeof (Text), "%d bit%a", 8 << (D[8] & 0x07), ((D[8] & 0x18) != 0) ? " + ECC" : "");
      Field (Info, 8, 1, RwEmNormal, "Bus width", Text);
      AsciiSPrint (Text, sizeof (Text), "%a%a%a", ((D[6] & BIT0) == 0) ? "1.5 V " : "", ((D[6] & BIT1) != 0) ? "1.35 V " : "", ((D[6] & BIT2) != 0) ? "1.25 V" : "");
      Field (Info, 6, 1, RwEmNormal, "Voltage", Text);
      break;

    case RW_SPD_DDR4:
      AsciiSPrint (Text, sizeof (Text), "%d Mbit, %d bank groups x %d banks", 256 << (D[4] & 0x0F), (((D[4] >> 6) & 3) == 0) ? 1 : (2 << (((D[4] >> 6) & 3) - 1)), 4 << ((D[4] >> 4) & 0x03));
      Field (Info, 4, 1, RwEmNormal, "SDRAM density", Text);
      AsciiSPrint (Text, sizeof (Text), "x%d, %d rank(s)%a", 4 << (D[12] & 0x07), ((D[12] >> 3) & 0x07) + 1, ((D[6] & 0x80) != 0) ? ", stacked" : "");
      Field (Info, 12, 1, RwEmNormal, "Organization", Text);
      AsciiSPrint (Text, sizeof (Text), "%d bit%a", 8 << (D[13] & 0x07), ((D[13] & 0x18) != 0) ? " + ECC" : "");
      Field (Info, 13, 1, RwEmNormal, "Bus width", Text);
      Field (Info, 11, 1, RwEmNormal, "Voltage", ((D[11] & BIT0) != 0) ? "1.2 V" : "not 1.2 V");
      break;

    case RW_SPD_DDR5:
      AsciiSPrint (Text, sizeof (Text), "density code %d, %d die(s)", D[4] & 0x1F, ((D[4] >> 5) == 0) ? 1 : (1 << ((D[4] >> 5) - 1)));
      Field (Info, 4, 1, RwEmNormal, "SDRAM density", Text);
      AsciiSPrint (Text, sizeof (Text), "x%d, %d rank(s)", 4 << ((D[6] >> 5) & 0x07), ((D[234] >> 3) & 0x07) + 1);
      Field (Info, 234, 1, RwEmNormal, "Organization", Text);
      AsciiSPrint (Text, sizeof (Text), "%d sub-channel(s) x %d bit%a", ((D[235] >> 5) & 0x03) + 1, 8 << (D[235] & 0x07), ((D[235] & 0x18) != 0) ? " + ECC" : "");
      Field (Info, 235, 1, RwEmNormal, "Bus width", Text);
      break;
  }

  Tck = TckPs (Spd);
  AsciiSPrint (Text, sizeof (Text), "%d ps (%d MT/s)", Tck, SpeedMts (Tck));
  Field (Info, (Spd->Type == RW_SPD_DDR3) ? 12 : ((Spd->Type == RW_SPD_DDR4) ? 18 : 20), (Spd->Type == RW_SPD_DDR5) ? 2 : 1, RwEmStrong, "tCK min", Text);

  RwInfoSection (Info, 0, "Module");
  Mfg = RwSpdManufacturer (D[L->MfgBank], D[L->MfgBank + 1]);
  AsciiSPrint (Text, sizeof (Text), "%a (%02X %02Xh)", (Mfg != NULL) ? Mfg : "Unknown", D[L->MfgBank], D[L->MfgBank + 1]);
  Field (Info, L->MfgBank, 2, RwEmStrong, "Manufacturer", Text);
  CopyText (&D[L->Part], L->PartLen, Part, sizeof (Part));
  Field (Info, L->Part, L->PartLen, RwEmStrong, "Part number", Part);
  AsciiSPrint (Text, sizeof (Text), "%02X%02X%02X%02X", D[L->Serial], D[L->Serial + 1], D[L->Serial + 2], D[L->Serial + 3]);
  Field (Info, L->Serial, 4, RwEmNormal, "Serial number", Text);
  AsciiSPrint (Text, sizeof (Text), "week %02x of 20%02x", D[L->MfgDate + 1], D[L->MfgDate]);
  Field (Info, L->MfgDate, 2, RwEmNormal, "Manufactured", Text);
  Mfg = RwSpdManufacturer (D[L->DramMfg], D[L->DramMfg + 1]);
  AsciiSPrint (Text, sizeof (Text), "%a (%02X %02Xh)", (Mfg != NULL) ? Mfg : "Unknown", D[L->DramMfg], D[L->DramMfg + 1]);
  Field (Info, L->DramMfg, 2, RwEmNormal, "DRAM manufacturer", Text);

  //
  // CRC of the base section (DDR3 may exclude bytes 117-125: byte 0 bit 7).
  //
  CrcStart = 0;
  Crc      = RwSpdCrc16 (&D[CrcStart], ((Spd->Type == RW_SPD_DDR3) && ((D[0] & 0x80) != 0)) ? 117 : L->CrcEnd);
  Stored   = (UINT16)(D[L->CrcEnd] | (D[L->CrcEnd + 1] << 8));
  AsciiSPrint (Text, sizeof (Text), "%04Xh %a (computed %04Xh)", Stored, (Crc == Stored) ? "OK" : "BAD", Crc);
  Field (Info, L->CrcEnd, 2, (Crc == Stored) ? RwEmNormal : RwEmAlert, "Base CRC", Text);

  RwInfoSection (Info, 0, "Raw (%d bytes)", Spd->Size);
  RwInfoAddHex (Info, 1, D, Spd->Size, 0);
}

//
// ---- Read-only source over the bytes -------------------------------------
//
typedef struct {
  CONST RW_SPD    *Spd;
} SPD_DS;

STATIC
EFI_STATUS
EFIAPI
SpdDsRead (
  IN  RW_DATA_SOURCE  *Source,
  IN  UINT64          Address,
  IN  UINTN           Width,
  OUT UINT64          *Value
  )
{
  CONST RW_SPD  *Spd;

  Spd = ((SPD_DS *)Source->Ctx)->Spd;
  if (Address >= Spd->Size) {
    *Value = 0;
    return EFI_INVALID_PARAMETER;
  }

  *Value = Spd->Data[Address];
  return EFI_SUCCESS;
}

VOID
RwSpdSourceInit (
  OUT RW_DATA_SOURCE  *Source,
  IN  CONST RW_SPD    *Spd
  )
{
  ZeroMem (Source, sizeof (*Source));
  AsciiSPrint (Source->Name, sizeof (Source->Name), "SPD %02Xh (%d bytes, read-only copy)", Spd->Addr, Spd->Size);
  Source->Base       = 0;
  Source->Limit      = (Spd->Size > 0) ? Spd->Size - 1 : 0;
  Source->AddrDigits = 3;
  Source->WidthMask  = RW_DS_WIDTH_8;
  Source->Read       = SpdDsRead;
  ((SPD_DS *)Source->Ctx)->Spd = Spd;
}
