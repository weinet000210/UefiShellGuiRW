/** @file
  RwCoreLib PCI: names and the decoded device information page
  (header, BARs, bridge windows, capabilities, extended capabilities).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <IndustryStandard/Pci.h>

typedef struct {
  UINT8          BaseClass;
  INT16          SubClass;     // -1 = any
  CONST CHAR8    *Name;
} RW_PCI_CLASS_NAME;

STATIC CONST RW_PCI_CLASS_NAME  mClassNames[] = {
  { 0x00, -1,   "Unclassified"            },
  { 0x01, 0x01, "IDE Controller"          },
  { 0x01, 0x06, "SATA Controller"         },
  { 0x01, 0x08, "NVMe Controller"         },
  { 0x01, -1,   "Mass Storage"            },
  { 0x02, 0x00, "Ethernet Controller"     },
  { 0x02, -1,   "Network Controller"      },
  { 0x03, 0x00, "VGA Controller"          },
  { 0x03, -1,   "Display Controller"      },
  { 0x04, 0x03, "HD Audio"                },
  { 0x04, -1,   "Multimedia"              },
  { 0x05, -1,   "Memory Controller"       },
  { 0x06, 0x00, "Host Bridge"             },
  { 0x06, 0x01, "ISA Bridge"              },
  { 0x06, 0x04, "PCI-to-PCI Bridge"       },
  { 0x06, -1,   "Bridge"                  },
  { 0x07, -1,   "Communication"           },
  { 0x08, -1,   "System Peripheral"       },
  { 0x09, -1,   "Input Device"            },
  { 0x0C, 0x03, "USB Controller"          },
  { 0x0C, 0x05, "SMBus Controller"        },
  { 0x0C, -1,   "Serial Bus"              },
  { 0x0D, -1,   "Wireless"                },
  { 0x10, -1,   "Encryption"              },
  { 0x11, -1,   "Signal Processing"       },
  { 0x12, -1,   "Processing Accelerator"  },
  { 0x13, -1,   "Non-Essential Instrument"},
};

STATIC CONST CHAR8  *mPcieTypes[] = {
  "Endpoint", "Legacy Endpoint", NULL, NULL, "Root Port", "Switch Upstream",
  "Switch Downstream", "PCIe-to-PCI Bridge", "PCI-to-PCIe Bridge",
  "RC Integrated Endpoint", "RC Event Collector",
};

STATIC CONST CHAR8  *mCapNames[] = {
  NULL, "Power Management", "AGP", "VPD", "Slot ID", "MSI", "CompactPCI Hot Swap",
  "PCI-X", "HyperTransport", "Vendor Specific", "Debug Port", "CompactPCI Resource",
  "PCI Hot-Plug", "Bridge Subsystem ID", "AGP 8x", "Secure Device", "PCI Express",
  "MSI-X", "SATA Config", "Advanced Features", "Enhanced Allocation", "Flattening Portal Bridge",
};

STATIC CONST CHAR8  *mExtCapNames[] = {
  NULL, "Advanced Error Reporting", "Virtual Channel", "Device Serial Number", "Power Budgeting",
  "RC Link Declaration", "RC Internal Link Control", "RC Event Collector Assoc", "MFVC",
  "Virtual Channel (MFVC)", "RCRB Header", "Vendor Specific (VSEC)", "Config Access Correlation",
  "Access Control Services", "ARI", "ATS", "SR-IOV", "MR-IOV", "Multicast", "Page Request",
  "Reserved (AMD)", "Resizable BAR", "Dynamic Power Allocation", "TPH Requester", "LTR",
  "Secondary PCIe", "PMUX", "PASID", "LN Requester", "DPC", "L1 PM Substates", "PTM",
  "M-PCIe", "FRS Queueing", "Readiness Time Reporting", "DVSEC", "VF Resizable BAR",
  "Data Link Feature", "Physical Layer 16.0 GT/s", "Lane Margining at Receiver", "Hierarchy ID",
  "NPEM", "Physical Layer 32.0 GT/s", "Alternate Protocol", "SFI", "Shadow Functions", "DOE",
  "Device 3", "IDE", "Physical Layer 64.0 GT/s", "Flit Logging", "Flit Perf Measurement",
  "Flit Error Injection",
};

STATIC CONST CHAR8  *mSpeedNames[] = { "?", "2.5 GT/s", "5.0 GT/s", "8.0 GT/s", "16.0 GT/s", "32.0 GT/s", "64.0 GT/s" };
STATIC CONST CHAR8  *mPins[]       = { "INTA", "INTB", "INTC", "INTD" };
STATIC CONST CHAR8  *mAspmNames[]  = { "none", "L0s", "L1", "L0s+L1" };

STATIC CONST CHAR8  *mUeNames[32] = {
  NULL, NULL, NULL, NULL, "DataLinkProtocol", "SurpriseDown", NULL, NULL, NULL, NULL, NULL, NULL,
  "PoisonedTLP", "FlowCtlProtocol", "CompletionTimeout", "CompleterAbort", "UnexpectedCompletion",
  "ReceiverOverflow", "MalformedTLP", "ECRC", "UnsupportedRequest", "ACSViolation",
  "UncorrInternal", "MCBlockedTLP", "AtomicOpEgressBlocked", "TLPPrefixBlocked", "PoisonedTLPEgress",
};

STATIC CONST CHAR8  *mCeNames[16] = {
  "ReceiverError", NULL, NULL, NULL, NULL, NULL, "BadTLP", "BadDLLP", "ReplayNumRollover",
  NULL, NULL, NULL, "ReplayTimerTimeout", "AdvisoryNonFatal", "CorrInternal", "HeaderLogOverflow",
};


CONST CHAR8 *
RwPciClassName (
  IN UINT8  BaseClass,
  IN UINT8  SubClass
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mClassNames); Index++) {
    if ((mClassNames[Index].BaseClass == BaseClass) &&
        ((mClassNames[Index].SubClass < 0) || (mClassNames[Index].SubClass == SubClass)))
    {
      return mClassNames[Index].Name;
    }
  }

  return "Other";
}

CONST CHAR8 *
RwPciPcieTypeName (
  IN UINT8  PcieType
  )
{
  return (PcieType < ARRAY_SIZE (mPcieTypes)) ? mPcieTypes[PcieType] : NULL;
}

CONST CHAR8 *
RwPciSpeedName (
  IN UINTN  Code
  )
{
  return (Code < ARRAY_SIZE (mSpeedNames)) ? mSpeedNames[Code] : "?";
}

CONST CHAR8 *
RwPciCapName (
  IN UINT8  Id
  )
{
  return ((Id < ARRAY_SIZE (mCapNames)) && (mCapNames[Id] != NULL)) ? mCapNames[Id] : "Unknown";
}

CONST CHAR8 *
RwPciExtCapName (
  IN UINT16  Id
  )
{
  return ((Id < ARRAY_SIZE (mExtCapNames)) && (mExtCapNames[Id] != NULL)) ? mExtCapNames[Id] : "Unknown";
}

VOID
RwPciDescribe (
  IN  CONST RW_PCI_DEVICE  *Device,
  OUT CHAR8                *Buffer,
  IN  UINTN                BufferSize
  )
{
  AsciiSPrint (
    Buffer,
    BufferSize,
    "PCI %04X:%02X:%02X.%X  %04X:%04X  %a",
    Device->Segment,
    Device->Bus,
    Device->Dev,
    Device->Func,
    Device->VendorId,
    Device->DeviceId,
    RwPciClassName (Device->BaseClass, Device->SubClass)
    );
}

//
// Shorthands for the decoders below.
//
#define RD(Reg, Width)  RwPciRd (E, (UINT32)(Reg), (Width))

STATIC
RW_INFO_ROW *
EFIAPI
Reg (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     UINT8        Emphasis,
  IN     UINT32       Offset,
  IN     UINT32       Size,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Format,
  ...
  )
{
  RW_STR       Str;
  VA_LIST      Marker;
  RW_INFO_ROW  *Row;

  RwStrInit (&Str);
  VA_START (Marker, Format);
  RwStrAppendV (&Str, Format, Marker);
  VA_END (Marker);
  Row = RwInfoAdd (Info, RwKindField, Level, Emphasis, Name, Str.Buf);
  RwInfoSetReg (Row, Offset, Size);
  RwStrFree (&Str);
  return Row;
}

/**
  "Name  XXXXXXXX  bit names" with Alert emphasis when named bits are set.
**/
STATIC
VOID
AddBits (
  IN OUT RW_INFO      *Info,
  IN     UINT32       Offset,
  IN     CONST CHAR8  *Label,
  IN     UINT32       Value,
  IN     CONST CHAR8  **Names,
  IN     UINTN        Count
  )
{
  RW_STR  Str;

  RwStrInit (&Str);
  RwStrAppendF (&Str, "%08X", Value);
  RwStrAppendBits (&Str, Value, Names, Count, NULL);
  RwInfoSetReg (
    RwInfoAdd (Info, RwKindField, 2, ((Value != 0) && (Count > 0)) ? RwEmAlert : RwEmNormal, Label, Str.Buf),
    Offset,
    4
    );
  RwStrFree (&Str);
}

STATIC
VOID
DecodePcie (
  IN OUT RW_INFO              *Info,
  IN     CONST RW_PCI_DEVICE  *E,
  IN     UINT32               P
  )
{
  UINT32  Cap;
  UINT32  DevCap;
  UINT32  DevCtl;
  UINT32  DevSta;
  UINT32  LnkCap;
  UINT32  LnkCtl;
  UINT32  LnkSta;
  UINT32  SltCap;
  UINT32  SltSta;
  UINT32  LnkCap2;
  UINT32  LnkCtl2;
  UINT32  DevCap2;
  UINT32  DevCtl2;
  UINTN   Type;
  UINTN   Bit;
  RW_STR  Speeds;

  Cap    = RD (P + 2, 2);
  DevCap = RD (P + 4, 4);
  DevCtl = RD (P + 8, 2);
  DevSta = RD (P + 0x0A, 2);
  LnkCap = RD (P + 0x0C, 4);
  LnkCtl = RD (P + 0x10, 2);
  LnkSta = RD (P + 0x12, 2);
  Type   = (Cap >> 4) & 0x0F;

  Reg (
    Info, 2, RwEmNormal, P + 2, 2, "Capabilities", "%04X  version %d, %a%a",
    Cap,
    Cap & 0x0F,
    ((Type < ARRAY_SIZE (mPcieTypes)) && (mPcieTypes[Type] != NULL)) ? mPcieTypes[Type] : "Unknown type",
    (Cap & BIT8) ? ", slot implemented" : ""
    );
  Reg (
    Info, 2, RwEmNormal, P + 4, 4, "DevCap", "%08X  MaxPayload supported %d B%a",
    DevCap,
    128 << (DevCap & 7),
    (DevCap & BIT28) ? ", FLR" : ""
    );
  Reg (
    Info, 2, RwEmNormal, P + 8, 2, "DevCtl", "%04X  MaxPayload %d B, MaxReadReq %d B%a%a",
    DevCtl,
    128 << ((DevCtl >> 5) & 7),
    128 << ((DevCtl >> 12) & 7),
    (DevCtl & BIT4) ? ", RelaxedOrd" : "",
    (DevCtl & BIT11) ? ", NoSnoop" : ""
    );
  Reg (
    Info, 2, (DevSta & 0x0F) ? RwEmAlert : RwEmNormal, P + 0x0A, 2, "DevSta", "%04X  %a%a%a%a%a",
    DevSta,
    (DevSta & BIT0) ? "CorrErr " : "",
    (DevSta & BIT1) ? "NonFatalErr " : "",
    (DevSta & BIT2) ? "FatalErr " : "",
    (DevSta & BIT3) ? "UnsuppReq " : "",
    (DevSta & 0x0F) ? "" : "no errors"
    );

  if ((Type != 9) && (Type != 0x0A)) {
    Reg (
      Info, 2, RwEmNormal, P + 0x0C, 4, "LnkCap", "%08X  Port %d, max %a x%d, ASPM %a",
      LnkCap,
      LnkCap >> 24,
      RwPciSpeedName (LnkCap & 0x0F),
      (LnkCap >> 4) & 0x3F,
      mAspmNames[(LnkCap >> 10) & 3]
      );
    Reg (
      Info, 2, RwEmNormal, P + 0x10, 2, "LnkCtl", "%04X  ASPM %a%a%a",
      LnkCtl,
      mAspmNames[LnkCtl & 3],
      (LnkCtl & BIT4) ? ", LINK DISABLED" : "",
      (LnkCtl & BIT6) ? ", common clock" : ""
      );
    Reg (
      Info, 2, RwEmStrong, P + 0x12, 2, "LnkSta", "%04X  %a x%d%a%a%a",
      LnkSta,
      ((LnkSta >> 4) & 0x3F) ? RwPciSpeedName (LnkSta & 0x0F) : "link down",
      (LnkSta >> 4) & 0x3F,
      (LnkSta & BIT11) ? ", training" : "",
      (LnkSta & BIT12) ? ", slot clock" : "",
      (LnkSta & BIT13) ? ", DL active" : ""
      );
  }

  if (Cap & BIT8) {
    SltCap = RD (P + 0x14, 4);
    SltSta = RD (P + 0x1A, 2);
    Reg (
      Info, 2, RwEmNormal, P + 0x14, 4, "SltCap", "%08X  Slot #%d%a",
      SltCap,
      SltCap >> 19,
      (SltCap & BIT6) ? ", hot-plug capable" : ""
      );
    Reg (
      Info, 2, RwEmNormal, P + 0x1A, 2, "SltSta", "%04X  %a",
      SltSta,
      (SltSta & BIT6) ? "card present" : "empty"
      );
  }

  if ((Cap & 0x0F) >= 2) {
    DevCap2 = RD (P + 0x24, 4);
    DevCtl2 = RD (P + 0x28, 2);
    LnkCap2 = RD (P + 0x2C, 4);
    LnkCtl2 = RD (P + 0x30, 2);
    Reg (Info, 2, RwEmNormal, P + 0x24, 4, "DevCap2", "%08X  LTR %a", DevCap2, (DevCap2 & BIT11) ? "supported" : "no");
    Reg (Info, 2, RwEmNormal, P + 0x28, 2, "DevCtl2", "%04X  LTR %a", DevCtl2, (DevCtl2 & BIT10) ? "enabled" : "off");
    if ((Type != 9) && (Type != 0x0A) && (LnkCap2 != 0)) {
      RwStrInit (&Speeds);
      for (Bit = 1; Bit <= 6; Bit++) {
        if (LnkCap2 & (1u << Bit)) {
          RwStrAppendF (&Speeds, " %a", RwPciSpeedName (Bit));
        }
      }

      Reg (Info, 2, RwEmNormal, P + 0x2C, 4, "LnkCap2", "%08X  speeds:%a", LnkCap2, Speeds.Buf);
      Reg (Info, 2, RwEmNormal, P + 0x30, 2, "LnkCtl2", "%04X  target %a", LnkCtl2, RwPciSpeedName (LnkCtl2 & 0x0F));
      RwStrFree (&Speeds);
    }
  }
}

STATIC
VOID
DecodeCap (
  IN OUT RW_INFO              *Info,
  IN     CONST RW_PCI_DEVICE  *E,
  IN     UINT8                Id,
  IN     UINT32               P
  )
{
  UINT32                    V;
  UINT32                    Ctl;
  STATIC CONST CHAR8 *CONST  DStates[] = { "D0", "D1", "D2", "D3hot" };

  switch (Id) {
    case 0x01:
      V   = RD (P + 2, 2);
      Ctl = RD (P + 4, 2);
      Reg (
        Info, 2, RwEmNormal, P + 2, 2, "PMC", "%04X  version %d%a%a, PME from %a%a%a%a%a%a",
        V,
        V & 7,
        (V & BIT9) ? ", D1" : "",
        (V & BIT10) ? ", D2" : "",
        (V & BIT11) ? "D0 " : "",
        (V & BIT12) ? "D1 " : "",
        (V & BIT13) ? "D2 " : "",
        (V & BIT14) ? "D3hot " : "",
        (V & BIT15) ? "D3cold" : "",
        (V & 0xF800) ? "" : "none"
        );
      Reg (
        Info, 2, RwEmStrong, P + 4, 2, "PMCSR", "%04X  state %a%a%a%a",
        Ctl,
        DStates[Ctl & 3],
        (Ctl & BIT3) ? ", NoSoftReset" : "",
        (Ctl & BIT8) ? ", PME enabled" : "",
        (Ctl & BIT15) ? ", PME status" : ""
        );
      break;
    case 0x05:
      Ctl = RD (P + 2, 2);
      Reg (
        Info, 2, (Ctl & BIT0) ? RwEmStrong : RwEmNormal, P + 2, 2, "Control", "%04X  %a, %d of %d vectors, %a%a",
        Ctl,
        (Ctl & BIT0) ? "ENABLED" : "disabled",
        1 << ((Ctl >> 4) & 7),
        1 << ((Ctl >> 1) & 7),
        (Ctl & BIT7) ? "64-bit" : "32-bit",
        (Ctl & BIT8) ? ", per-vector mask" : ""
        );
      if (Ctl & BIT7) {
        Reg (Info, 2, RwEmNormal, P + 4, 8, "Address", "%08X%08X", RD (P + 8, 4), RD (P + 4, 4));
        Reg (Info, 2, RwEmNormal, P + 0x0C, 2, "Data", "%04X", RD (P + 0x0C, 2));
      } else {
        Reg (Info, 2, RwEmNormal, P + 4, 4, "Address", "%08X", RD (P + 4, 4));
        Reg (Info, 2, RwEmNormal, P + 8, 2, "Data", "%04X", RD (P + 8, 2));
      }

      break;
    case 0x11:
      Ctl = RD (P + 2, 2);
      Reg (
        Info, 2, (Ctl & BIT15) ? RwEmStrong : RwEmNormal, P + 2, 2, "Control", "%04X  %a, %d vectors%a",
        Ctl,
        (Ctl & BIT15) ? "ENABLED" : "disabled",
        (Ctl & 0x7FF) + 1,
        (Ctl & BIT14) ? ", function masked" : ""
        );
      V = RD (P + 4, 4);
      Reg (Info, 2, RwEmNormal, P + 4, 4, "Table", "BAR%d + %Xh", V & 7, V & ~7u);
      V = RD (P + 8, 4);
      Reg (Info, 2, RwEmNormal, P + 8, 4, "PBA", "BAR%d + %Xh", V & 7, V & ~7u);
      break;
    case 0x10:
      DecodePcie (Info, E, P);
      break;
    case 0x09:
      Reg (Info, 2, RwEmNormal, P + 2, 1, "Length", "%d bytes", RD (P + 2, 1));
      break;
    case 0x0D:
      V = RD (P + 4, 4);
      Reg (Info, 2, RwEmNormal, P + 4, 4, "Subsystem", "%04X:%04X", V & 0xFFFF, V >> 16);
      break;
    default:
      Reg (Info, 2, RwEmDim, P, 8, "Raw", "%08X %08X", RD (P, 4), RD (P + 4, 4));
      break;
  }
}

STATIC
VOID
DecodeExtCap (
  IN OUT RW_INFO              *Info,
  IN     CONST RW_PCI_DEVICE  *E,
  IN     UINT16               Id,
  IN     UINT32               P
  )
{
  UINT32  V;
  UINT32  Ctl;
  UINTN   Count;
  UINTN   Index;
  UINTN   Bit;
  RW_STR  Sizes;
  CHAR8   Name[16];

  switch (Id) {
    case 0x0001:
      AddBits (Info, P + 4, "Uncorrectable status", RD (P + 4, 4), mUeNames, 32);
      AddBits (Info, P + 8, "Uncorrectable mask", RD (P + 8, 4), NULL, 0);
      AddBits (Info, P + 0x0C, "Uncorrectable severity", RD (P + 0x0C, 4), NULL, 0);
      AddBits (Info, P + 0x10, "Correctable status", RD (P + 0x10, 4), mCeNames, 16);
      AddBits (Info, P + 0x14, "Correctable mask", RD (P + 0x14, 4), NULL, 0);
      AddBits (Info, P + 0x18, "Cap & control", RD (P + 0x18, 4), NULL, 0);
      break;
    case 0x0003:
      Reg (Info, 2, RwEmStrong, P + 4, 8, "Serial number", "%08X%08X", RD (P + 8, 4), RD (P + 4, 4));
      break;
    case 0x000B:
      V = RD (P + 4, 4);
      Reg (Info, 2, RwEmNormal, P + 4, 4, "VSEC", "ID %04X rev %d, length %d", V & 0xFFFF, (V >> 16) & 0xF, V >> 20);
      break;
    case 0x0023:
      V = RD (P + 4, 4);
      Reg (Info, 2, RwEmNormal, P + 4, 6, "DVSEC", "vendor %04X rev %d, length %d, ID %04X", V & 0xFFFF, (V >> 16) & 0xF, V >> 20, RD (P + 8, 2));
      break;
    case 0x0010:
      Ctl = RD (P + 8, 2);
      Reg (Info, 2, (Ctl & BIT0) ? RwEmStrong : RwEmNormal, P + 8, 2, "VF control", "%04X  VF %a", Ctl, (Ctl & BIT0) ? "ENABLED" : "disabled");
      Reg (Info, 2, RwEmNormal, P + 0x0C, 6, "NumVFs", "%d of %d (initial %d)", RD (P + 0x10, 2), RD (P + 0x0E, 2), RD (P + 0x0C, 2));
      Reg (Info, 2, RwEmNormal, P + 0x14, 4, "VF offset / stride", "%d / %d", RD (P + 0x14, 2), RD (P + 0x16, 2));
      Reg (Info, 2, RwEmNormal, P + 0x1A, 2, "VF device", "%04X", RD (P + 0x1A, 2));
      break;
    case 0x0015:
    case 0x0024:
      Count = (RD (P + 8, 4) >> 5) & 7;
      for (Index = 0; (Index < Count) && (Index < 6); Index++) {
        V   = RD (P + 4 + (UINT32)Index * 8, 4);
        Ctl = RD (P + 8 + (UINT32)Index * 8, 4);
        RwStrInit (&Sizes);
        for (Bit = 4; Bit < 32; Bit++) {
          if (V & (1u << Bit)) {
            if (Bit - 4 >= 10) {
              RwStrAppendF (&Sizes, " %dG", 1u << (Bit - 14));
            } else {
              RwStrAppendF (&Sizes, " %dM", 1u << (Bit - 4));
            }
          }
        }

        AsciiSPrint (Name, sizeof (Name), "BAR%d", Ctl & 7);
        Reg (Info, 2, RwEmNormal, P + 4 + (UINT32)Index * 8, 8, Name, "current %d MB, supported:%a", 1u << ((Ctl >> 8) & 0x3F), Sizes.Buf);
        RwStrFree (&Sizes);
      }

      break;
    case 0x0018:
      V = RD (P + 4, 4);
      Reg (Info, 2, RwEmNormal, P + 4, 2, "Max snoop latency", "%d x 32^%d ns", V & 0x3FF, (V >> 10) & 7);
      Reg (Info, 2, RwEmNormal, P + 6, 2, "Max no-snoop latency", "%d x 32^%d ns", (V >> 16) & 0x3FF, (V >> 26) & 7);
      break;
    case 0x001E:
      V   = RD (P + 4, 4);
      Ctl = RD (P + 8, 4);
      Reg (
        Info, 2, RwEmNormal, P + 4, 4, "Supported", "%a%a%a%a",
        (V & BIT0) ? " PCI-PM_L1.2" : "",
        (V & BIT1) ? " PCI-PM_L1.1" : "",
        (V & BIT2) ? " ASPM_L1.2" : "",
        (V & BIT3) ? " ASPM_L1.1" : ""
        );
      Reg (
        Info, 2, RwEmNormal, P + 8, 4, "Enabled", "%a%a%a%a%a",
        (Ctl & BIT0) ? " PCI-PM_L1.2" : "",
        (Ctl & BIT1) ? " PCI-PM_L1.1" : "",
        (Ctl & BIT2) ? " ASPM_L1.2" : "",
        (Ctl & BIT3) ? " ASPM_L1.1" : "",
        (Ctl & 0x0F) ? "" : " none"
        );
      break;
    case 0x000E:
      V = RD (P + 4, 2);
      Reg (Info, 2, RwEmNormal, P + 4, 2, "Next function number", "%d", (V >> 8) & 0xFF);
      break;
    case 0x0019:
      AddBits (Info, P + 8, "Lane error status", RD (P + 8, 4), NULL, 0);
      break;
    default:
      Reg (Info, 2, RwEmDim, P + 4, 12, "Raw", "%08X %08X %08X", RD (P + 4, 4), RD (P + 8, 4), RD (P + 0x0C, 4));
      break;
  }
}

STATIC
VOID
AddBars (
  IN OUT RW_INFO              *Info,
  IN     CONST RW_PCI_DEVICE  *E,
  IN     UINTN                Count
  )
{
  UINTN   Index;
  UINT32  Lo;
  UINT32  Hi;
  CHAR8   Name[8];
  UINT32  Off;

  for (Index = 0; Index < Count; Index++) {
    Off = 0x10 + (UINT32)Index * 4;
    Lo  = RD (Off, 4);
    AsciiSPrint (Name, sizeof (Name), "BAR%d", Index);
    if (Lo == 0) {
      Reg (Info, 1, RwEmDim, Off, 4, Name, "unused");
      continue;
    }

    if (Lo & BIT0) {
      Reg (Info, 1, RwEmNormal, Off, 4, Name, "IO     %04X", Lo & ~3u);
    } else if (((Lo >> 1) & 3) == 2) {
      Hi = (Index + 1 < Count) ? RD (Off + 4, 4) : 0;
      Reg (Info, 1, RwEmNormal, Off, 8, Name, "MEM64  %08X%08X%a", Hi, Lo & ~0xFu, (Lo & BIT3) ? "  prefetchable" : "");
      Index++;
    } else {
      Reg (Info, 1, RwEmNormal, Off, 4, Name, "MEM32  %08X%a", Lo & ~0xFu, (Lo & BIT3) ? "  prefetchable" : "");
    }
  }
}

VOID
RwPciDecode (
  IN     CONST RW_PCI_DEVICE  *E,
  IN OUT RW_INFO              *Info
  )
{
  UINT32       Cmd;
  UINT32       Sts;
  UINT32       V;
  UINT8        Ptr;
  UINT8        Id;
  UINTN        Guard;
  UINT32       Ext;
  UINT32       ExtPtr;
  UINT16       ExtId;
  UINTN        Type;
  RW_INFO_ROW  *Row;

  Type = E->HeaderType & 0x7F;
  Cmd  = RD (4, 2);
  Sts  = RD (6, 2);
  RwInfoSection (Info, 0, "Header");
  Reg (Info, 1, RwEmNormal, 0, 4, "Vendor:Device", "%04X:%04X", E->VendorId, E->DeviceId);
  Reg (Info, 1, RwEmNormal, 8, 1, "Revision", "%02X", RD (8, 1));
  Reg (Info, 1, RwEmNormal, 9, 3, "Class", "%02X%02X%02X  %a", E->BaseClass, E->SubClass, E->ProgIf, RwPciClassName (E->BaseClass, E->SubClass));
  Reg (
    Info, 1, RwEmNormal, 0x0E, 1, "Header type", "%02X  %a%a",
    E->HeaderType,
    (Type == 0) ? "device" : ((Type == 1) ? "PCI bridge" : "CardBus bridge"),
    (E->HeaderType & 0x80) ? ", multi-function" : ""
    );
  Reg (
    Info, 1, RwEmNormal, 4, 2, "Command", "%04X  %a%a%a%a%a%a",
    Cmd,
    (Cmd & BIT0) ? "IO " : "",
    (Cmd & BIT1) ? "MEM " : "",
    (Cmd & BIT2) ? "BusMaster " : "",
    (Cmd & BIT6) ? "ParityErr " : "",
    (Cmd & BIT8) ? "SERR " : "",
    (Cmd & BIT10) ? "INTxDisable" : ""
    );
  Reg (
    Info, 1, (Sts & 0xF900) ? RwEmAlert : RwEmNormal, 6, 2, "Status", "%04X  %a%a%a%a%a%a%a",
    Sts,
    (Sts & BIT3) ? "INTx " : "",
    (Sts & BIT4) ? "CapList " : "",
    (Sts & BIT8) ? "MasterDataParity " : "",
    (Sts & BIT11) ? "SigTargetAbort " : "",
    (Sts & BIT12) ? "RcvTargetAbort " : "",
    (Sts & BIT13) ? "RcvMasterAbort " : "",
    (Sts & (BIT14 | BIT15)) ? "SERR/ParityErr" : ""
    );
  V = RD (0x3C, 2);
  Reg (
    Info, 1, RwEmNormal, 0x3C, 2, "Interrupt", "line %02X, pin %a",
    V & 0xFF,
    (((V >> 8) >= 1) && ((V >> 8) <= 4)) ? mPins[(V >> 8) - 1] : "none"
    );
  if (Type == 0) {
    V = RD (0x2C, 4);
    Reg (Info, 1, RwEmNormal, 0x2C, 4, "Subsystem", "%04X:%04X", V & 0xFFFF, V >> 16);
  }

  RwInfoSection (Info, 0, "Base Address Registers");
  AddBars (Info, E, (Type == 0) ? 6 : ((Type == 1) ? 2 : 1));
  if (Type == 1) {
    RwInfoSection (Info, 0, "Bridge");
    Reg (Info, 1, RwEmNormal, 0x18, 3, "Bus", "primary %02X  secondary %02X  subordinate %02X", RD (0x18, 1), RD (0x19, 1), RD (0x1A, 1));
    V = RD (0x1C, 2);
    Reg (Info, 1, RwEmNormal, 0x1C, 2, "IO window", "%04X - %04X", (V & 0xF0) << 8, (((V >> 8) & 0xF0) << 8) | 0xFFF);
    V = RD (0x20, 4);
    Reg (Info, 1, RwEmNormal, 0x20, 4, "Memory window", "%08X - %08X", (V & 0xFFF0) << 16, (V & 0xFFF00000) | 0xFFFFF);
    V = RD (0x24, 4);
    Reg (Info, 1, RwEmNormal, 0x24, 12, "Prefetch window", "%08X%08X - %08X%08X", RD (0x28, 4), (V & 0xFFF0) << 16, RD (0x2C, 4), (V & 0xFFF00000) | 0xFFFFF);
    V = RD (0x3E, 2);
    Reg (Info, 1, RwEmNormal, 0x3E, 2, "Bridge control", "%04X%a%a", V, (V & BIT3) ? "  VGA" : "", (V & BIT6) ? "  SECONDARY BUS RESET" : "");
  }

  RwInfoSection (Info, 0, "Capabilities");
  if ((Sts & BIT4) == 0) {
    RwInfoText (Info, 1, RwEmDim, "(none)");
  } else {
    Ptr = (UINT8)RD ((Type == 2) ? 0x14 : 0x34, 1) & 0xFC;
    for (Guard = 0; (Ptr >= 0x40) && (Guard < 48); Guard++) {
      V   = RD (Ptr, 2);
      Id  = (UINT8)V;
      Row = RwInfoSection (Info, 1, "[%02X] %a", Ptr, RwPciCapName (Id));
      RwInfoSetReg (Row, Ptr, 2);
      DecodeCap (Info, E, Id, Ptr);
      Ptr = (UINT8)(V >> 8) & 0xFC;
    }
  }

  Ext = RD (0x100, 4);
  if ((Ext != 0xFFFFFFFF) && (Ext != 0) && (E->PcieType != RW_PCI_NO_PCIE)) {
    RwInfoSection (Info, 0, "Extended Capabilities");
    ExtPtr = 0x100;
    for (Guard = 0; (ExtPtr >= 0x100) && (ExtPtr < 0x1000) && (Guard < 64); Guard++) {
      Ext = RD (ExtPtr, 4);
      if ((Ext == 0) || (Ext == 0xFFFFFFFF)) {
        break;
      }

      ExtId = (UINT16)Ext;
      Row   = RwInfoSection (Info, 1, "[%03X] %a  v%d", ExtPtr, RwPciExtCapName (ExtId), (Ext >> 16) & 0xF);
      RwInfoSetReg (Row, ExtPtr, 4);
      DecodeExtCap (Info, E, ExtId, ExtPtr);
      ExtPtr = (Ext >> 20) & 0xFFC;
    }
  }
}

STATIC
VOID
TreeLine (
  IN     CONST RW_PCI_LIST  *List,
  IN     UINTN              Index,
  IN OUT RW_INFO            *Info
  )
{
  CONST RW_PCI_DEVICE  *E;
  CONST CHAR8          *Type;
  RW_STR               Str;

  E = &List->Devices[Index];
  RwStrInit (&Str);
  RwStrAppendF (
    &Str,
    "%02X:%02X.%X  %04X:%04X  %a",
    E->Bus,
    E->Dev,
    E->Func,
    E->VendorId,
    E->DeviceId,
    RwPciClassName (E->BaseClass, E->SubClass)
    );
  Type = (E->PcieType != RW_PCI_NO_PCIE) ? RwPciPcieTypeName (E->PcieType) : NULL;
  if (Type != NULL) {
    RwStrAppendF (&Str, "  [%a]", Type);
  }

  if (RwPciIsBridge (E)) {
    RwStrAppendF (&Str, "  Bus %02X-%02X", E->SecBus, E->SubBus);
  }

  if ((E->PcieType != RW_PCI_NO_PCIE) && (E->MaxWidth != 0)) {
    if (E->LinkWidth == 0) {
      RwStrAppendF (&Str, "  link down (max Gen%d x%d)", E->MaxSpeed, E->MaxWidth);
    } else {
      RwStrAppendF (&Str, "  Gen%d x%d (max Gen%d x%d)", E->LinkSpeed, E->LinkWidth, E->MaxSpeed, E->MaxWidth);
    }
  }

  RwInfoSetTag (RwInfoText (Info, (UINT8)(E->Depth + 1), RwEmNormal, "%a", Str.Buf), (INT32)Index);
  RwStrFree (&Str);
}

VOID
RwPciDecodeTree (
  IN     CONST RW_PCI_LIST  *List,
  IN OUT RW_INFO            *Info
  )
{
  UINTN  *Order;
  UINTN  Count;
  UINTN  Index;
  UINTN  Root;

  if (List->DeviceCount == 0) {
    return;
  }

  Order = AllocatePool (List->DeviceCount * sizeof (UINTN));
  if (Order == NULL) {
    Info->Oom = TRUE;
    return;
  }

  Count = RwPciTreeOrder (List, Order);
  for (Root = 0; Root < List->RootCount; Root++) {
    RwInfoSection (
      Info,
      0,
      "PCI Root Bridge  Segment %04X  Bus %02X-%02X",
      List->Roots[Root].Segment,
      List->Roots[Root].BusMin,
      List->Roots[Root].BusMax
      );
    for (Index = 0; Index < Count; Index++) {
      if (List->Devices[Order[Index]].RootIndex == Root) {
        TreeLine (List, Order[Index], Info);
      }
    }
  }

  FreePool (Order);
}

VOID
RwPciDump (
  IN     CONST RW_PCI_LIST  *List,
  IN OUT RW_INFO            *Info
  )
{
  UINTN                Index;
  CONST RW_PCI_DEVICE  *E;
  CHAR8                Title[96];
  RW_INFO              Page;

  RwInfoSection (Info, 0, "PCI Devices (%d)", List->DeviceCount);
  RwInfoText (Info, 1, RwEmNormal, "Seg   Bus Dev Fn  Vendor Device  Class   Type");
  for (Index = 0; Index < List->DeviceCount; Index++) {
    E = &List->Devices[Index];
    RwInfoSetTag (
      RwInfoText (
        Info,
        1,
        RwEmNormal,
        "%04X  %02X  %02X  %X   %04X   %04X    %02X%02X%02X  %a",
        E->Segment,
        E->Bus,
        E->Dev,
        E->Func,
        E->VendorId,
        E->DeviceId,
        E->BaseClass,
        E->SubClass,
        E->ProgIf,
        RwPciClassName (E->BaseClass, E->SubClass)
        ),
      (INT32)Index
      );
  }

  RwInfoSection (Info, 0, "PCIe Tree");
  RwInfoInit (&Page);
  RwPciDecodeTree (List, &Page);
  RwInfoAppend (Info, &Page, 1);
  RwInfoFree (&Page);
  for (Index = 0; Index < List->DeviceCount; Index++) {
    RwPciDescribe (&List->Devices[Index], Title, sizeof (Title));
    RwInfoSection (Info, 0, "%a", Title);
    RwInfoInit (&Page);
    RwPciDecode (&List->Devices[Index], &Page);
    RwInfoAppend (Info, &Page, 1);
    RwInfoFree (&Page);
  }
}
