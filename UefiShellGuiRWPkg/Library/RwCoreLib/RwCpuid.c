/** @file
  RwCoreLib CPUID: leaf enumeration on any processor and decoders.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>

#define CPUID_AP_TIMEOUT_US  500000

//
// ---- Bit names -------------------------------------------------------------
//

STATIC CONST CHAR8  *mL1Edx[32] = {
  "FPU",  "VME",   "DE",    "PSE",   "TSC",  "MSR",  "PAE",  "MCE",
  "CX8",  "APIC",  NULL,    "SEP",   "MTRR", "PGE",  "MCA",  "CMOV",
  "PAT",  "PSE36", "PSN",   "CLFSH", NULL,   "DS",   "ACPI", "MMX",
  "FXSR", "SSE",   "SSE2",  "SS",    "HTT",  "TM",   "IA64", "PBE"
};

STATIC CONST CHAR8  *mL1Ecx[32] = {
  "SSE3",   "PCLMULQDQ", "DTES64",  "MONITOR", "DS-CPL",  "VMX",  "SMX",    "EIST",
  "TM2",    "SSSE3",     "CNXT-ID", "SDBG",    "FMA",     "CX16", "xTPR",   "PDCM",
  NULL,     "PCID",      "DCA",     "SSE4.1",  "SSE4.2",  "x2APIC", "MOVBE", "POPCNT",
  "TSC-DEADLINE", "AES", "XSAVE",   "OSXSAVE", "AVX",     "F16C", "RDRAND", "HYPERVISOR"
};

STATIC CONST CHAR8  *mL7Ebx[32] = {
  "FSGSBASE", "TSC_ADJUST", "SGX",        "BMI1",       "HLE",      "AVX2",      "FDP_EXCPTN_ONLY", "SMEP",
  "BMI2",     "ERMS",       "INVPCID",    "RTM",        "RDT-M",    "FPU_CS_DS_DEPR", "MPX",       "RDT-A",
  "AVX512F",  "AVX512DQ",   "RDSEED",     "ADX",        "SMAP",     "AVX512_IFMA", NULL,           "CLFLUSHOPT",
  "CLWB",     "PT",         "AVX512PF",   "AVX512ER",   "AVX512CD", "SHA",       "AVX512BW",    "AVX512VL"
};

STATIC CONST CHAR8  *mL7Ecx[32] = {
  "PREFETCHWT1", "AVX512_VBMI", "UMIP",         "PKU",         "OSPKE",     "WAITPKG",    "AVX512_VBMI2", "CET_SS",
  "GFNI",        "VAES",        "VPCLMULQDQ",   "AVX512_VNNI", "AVX512_BITALG", "TME_EN", "AVX512_VPOPCNTDQ", NULL,
  "LA57",        NULL,          NULL,           NULL,          NULL,        NULL,         "RDPID",        "KL",
  "BUS_LOCK_DETECT", "CLDEMOTE", NULL,          "MOVDIRI",     "MOVDIR64B", "ENQCMD",     "SGX_LC",       "PKS"
};

STATIC CONST CHAR8  *mL7Edx[32] = {
  NULL,        "SGX_KEYS",   "AVX512_4VNNIW", "AVX512_4FMAPS", "FSRM",      "UINTR",       NULL,        NULL,
  "AVX512_VP2INTERSECT", "SRBDS_CTRL", "MD_CLEAR", "RTM_ALWAYS_ABORT", NULL, "RTM_FORCE_ABORT", "SERIALIZE", "HYBRID",
  "TSXLDTRK",  NULL,         "PCONFIG",       "ARCH_LBR",      "CET_IBT",   NULL,          "AMX_BF16",  "AVX512_FP16",
  "AMX_TILE",  "AMX_INT8",   "IBRS_IBPB",     "STIBP",         "L1D_FLUSH", "ARCH_CAPABILITIES", "CORE_CAPABILITIES", "SSBD"
};

STATIC CONST CHAR8  *mL71Eax[32] = {
  "SHA512",  "SM3",     "SM4",    "RAO_INT", "AVX_VNNI", "AVX512_BF16", "LASS",  "CMPCCXADD",
  "ARCH_PERFMON_EXT", NULL, "FZLRM", "FSRS", "FSRCS",   NULL,          NULL,    NULL,
  NULL,      "FRED",    "LKGS",   "WRMSRNS", NULL,       "AMX_FP16",    "HRESET", "AVX_IFMA",
  NULL,      NULL,      "LAM",    "MSRLIST", NULL,       NULL,          NULL,    NULL
};

STATIC CONST CHAR8  *mL6Eax[16] = {
  "DTS",      "TURBO",   "ARAT",    NULL,         "PLN",      "ECMD",     "PTM",       "HWP",
  "HWP_NOTIFY", "HWP_ACT_WINDOW", "HWP_EPP", "HWP_PLR", NULL, "HDC",     "TURBO3",    "HWP_CAP"
};

STATIC CONST CHAR8  *mX1Edx[32] = {
  NULL,  NULL,  NULL,  NULL,  NULL,     NULL,      NULL,     NULL,
  NULL,  NULL,  NULL,  "SYSCALL", NULL, NULL,      NULL,     NULL,
  NULL,  NULL,  NULL,  "MP",  "NX",     NULL,      "MMXEXT", NULL,
  NULL,  "FFXSR", "PDPE1GB", "RDTSCP", NULL, "LM",  "3DNOWEXT", "3DNOW"
};

STATIC CONST CHAR8  *mX1Ecx[32] = {
  "LAHF_LM", "CMP_LEGACY", "SVM",  "EXTAPIC",  "CR8_LEGACY", "ABM",       "SSE4A",     "MISALIGNSSE",
  "3DNOWPREFETCH", "OSVW", "IBS",  "XOP",      "SKINIT",     "WDT",       NULL,        "LWP",
  "FMA4",    "TCE",        NULL,   "NODEID_MSR", NULL,        "TBM",       "TOPOEXT",   "PERFCTR_CORE",
  "PERFCTR_NB", NULL,      "BPEXT", "PERFTSC", "PERFCTR_LLC", "MONITORX",  "ADDR_MASK_EXT", NULL
};

STATIC CONST CHAR8  *mX7Edx[16] = {
  "TS",  "FID", "VID", "TTP", "TM", NULL, "100MHZ_STEPS", "HW_PSTATE",
  "INVARIANT_TSC", "CPB", "EFF_FREQ_RO", "PROC_FEEDBACK", "PROC_POWER_REPORT", NULL, NULL, NULL
};

STATIC CONST CHAR8  *mX8Ebx[32] = {
  "CLZERO", "INST_RET_CNT_MSR", "RSTR_FP_ERR_PTRS", NULL, "RDPRU", NULL, NULL, NULL,
  "MCOMMIT", "WBNOINVD", NULL,  NULL,     "IBPB",     NULL,      "IBRS",   "STIBP",
  "IBRS_ALWAYS_ON", "STIBP_ALWAYS_ON", "IBRS_PREFERRED", "IBRS_SAME_MODE", NULL, NULL, NULL, "PPIN",
  "SSBD",   "VIRT_SSBD", "SSB_NO", NULL,    NULL,       NULL,      NULL,     NULL
};

STATIC CONST CHAR8  *mXcr0[20] = {
  "x87", "SSE", "AVX", "BNDREGS", "BNDCSR", "OPMASK", "ZMM_HI256", "HI16_ZMM",
  "PT",  "PKRU", "PASID", "CET_U", "CET_S", "HDC", "UINTR", "LBR", "HWP", "XTILECFG", "XTILEDATA", "APX"
};

STATIC CONST CHAR8  *mXsaveFeat[8] = { "XSAVEOPT", "XSAVEC", "XGETBV_ECX1", "XSAVES", "XFD", NULL, NULL, NULL };

typedef struct {
  UINT32         Leaf;
  CONST CHAR8    *Name;
} LEAF_NAME;

STATIC CONST LEAF_NAME  mLeafNames[] = {
  { 0x00000000, "Vendor and highest leaf"            },
  { 0x00000001, "Signature and features"             },
  { 0x00000002, "Cache and TLB descriptors"          },
  { 0x00000003, "Processor serial number"            },
  { 0x00000004, "Deterministic cache parameters"     },
  { 0x00000005, "MONITOR / MWAIT"                    },
  { 0x00000006, "Thermal and power management"       },
  { 0x00000007, "Structured extended features"       },
  { 0x00000009, "Direct cache access"                },
  { 0x0000000A, "Architectural performance monitoring" },
  { 0x0000000B, "Extended topology"                  },
  { 0x0000000D, "Processor extended state (XSAVE)"   },
  { 0x0000000F, "RDT monitoring"                     },
  { 0x00000010, "RDT allocation"                     },
  { 0x00000012, "SGX"                                },
  { 0x00000014, "Processor trace"                    },
  { 0x00000015, "TSC and crystal clock"              },
  { 0x00000016, "Processor frequency"                },
  { 0x00000017, "SoC vendor"                         },
  { 0x00000018, "Address translation (TLB)"          },
  { 0x00000019, "Key locker"                         },
  { 0x0000001A, "Hybrid core type"                   },
  { 0x0000001B, "PCONFIG"                            },
  { 0x0000001C, "Last branch records"                },
  { 0x0000001D, "Tile information"                   },
  { 0x0000001E, "TMUL information"                   },
  { 0x0000001F, "V2 extended topology"               },
  { 0x00000020, "HRESET"                             },
  { 0x00000023, "Architectural perfmon extended"     },
  { 0x00000024, "AVX10"                              },
  { 0x40000000, "Hypervisor vendor"                  },
  { 0x80000000, "Highest extended leaf"              },
  { 0x80000001, "Extended features"                  },
  { 0x80000002, "Brand string 1"                     },
  { 0x80000003, "Brand string 2"                     },
  { 0x80000004, "Brand string 3"                     },
  { 0x80000005, "L1 cache and TLB (AMD)"             },
  { 0x80000006, "L2 / L3 cache"                      },
  { 0x80000007, "Power management (invariant TSC)"   },
  { 0x80000008, "Address sizes"                      },
  { 0x8000000A, "SVM"                                },
  { 0x80000019, "1 GB page TLB (AMD)"                },
  { 0x8000001A, "Performance optimization (AMD)"     },
  { 0x8000001B, "IBS (AMD)"                          },
  { 0x8000001D, "Cache topology (AMD)"               },
  { 0x8000001E, "Processor topology (AMD)"           },
  { 0x8000001F, "Encrypted memory (AMD)"             },
  { 0x80000020, "Platform QoS (AMD)"                 },
  { 0x80000021, "Extended features 2 (AMD)"          },
  { 0x80000022, "Performance monitoring (AMD)"       },
  { 0x80000026, "Extended CPU topology (AMD)"        },
};

CONST CHAR8 *
RwCpuidLeafName (
  IN UINT32  Leaf
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mLeafNames); Index++) {
    if (mLeafNames[Index].Leaf == Leaf) {
      return mLeafNames[Index].Name;
    }
  }

  if ((Leaf > 0x40000000) && (Leaf <= 0x400000FF)) {
    return "Hypervisor";
  }

  return "";
}

//
// ---- Enumeration (runs on the target processor: no allocation, no I/O) ------
//

STATIC
RW_CPUID_ENTRY *
Probe (
  IN OUT RW_CPUID_LIST   *List,
  IN     UINT32          Leaf,
  IN     UINT32          Sub,
  OUT    RW_CPUID_ENTRY  *Tmp
  )
{
  Tmp->Leaf = Leaf;
  Tmp->Sub  = Sub;
  AsmCpuidEx (Leaf, Sub, &Tmp->Eax, &Tmp->Ebx, &Tmp->Ecx, &Tmp->Edx);
  return Tmp;
}

STATIC
VOID
Keep (
  IN OUT RW_CPUID_LIST         *List,
  IN     CONST RW_CPUID_ENTRY  *Entry
  )
{
  if (List->Count < RW_CPUID_MAX) {
    List->Entries[List->Count++] = *Entry;
  }
}

STATIC
VOID
CollectLeaf (
  IN OUT RW_CPUID_LIST  *List,
  IN     UINT32         Leaf
  )
{
  RW_CPUID_ENTRY  E;
  UINT32          Sub;
  UINT32          Last;

  Keep (List, Probe (List, Leaf, 0, &E));
  switch (Leaf) {
    case 0x00000004:
    case 0x8000001D:
      //
      // Cache levels until the "no more caches" type.
      //
      if ((E.Eax & 0x1F) == 0) {
        List->Count--;
        break;
      }

      for (Sub = 1; Sub < 16; Sub++) {
        if ((Probe (List, Leaf, Sub, &E)->Eax & 0x1F) == 0) {
          break;
        }

        Keep (List, &E);
      }

      break;

    case 0x0000000B:
    case 0x0000001F:
    case 0x80000026:
      //
      // Topology levels until the invalid level type.
      //
      for (Sub = 1; Sub < 8; Sub++) {
        if (((Probe (List, Leaf, Sub, &E)->Ecx >> 8) & 0xFF) == 0) {
          break;
        }

        Keep (List, &E);
      }

      break;

    case 0x0000000D:
      Keep (List, Probe (List, Leaf, 1, &E));
      for (Sub = 2; Sub < 63; Sub++) {
        Probe (List, Leaf, Sub, &E);
        if ((E.Eax | E.Ebx | E.Ecx | E.Edx) != 0) {
          Keep (List, &E);
        }
      }

      break;

    case 0x0000000F:
      Keep (List, Probe (List, Leaf, 1, &E));
      break;

    case 0x00000010:
    case 0x80000020:
      for (Sub = 1; Sub < 4; Sub++) {
        Keep (List, Probe (List, Leaf, Sub, &E));
      }

      break;

    case 0x00000012:
      Keep (List, Probe (List, Leaf, 1, &E));
      for (Sub = 2; Sub < 10; Sub++) {
        if ((Probe (List, Leaf, Sub, &E)->Eax & 0xF) == 0) {
          break;
        }

        Keep (List, &E);
      }

      break;

    case 0x00000007:
    case 0x00000014:
    case 0x00000017:
    case 0x00000018:
    case 0x0000001D:
    case 0x00000020:
    case 0x00000023:
    case 0x00000024:
      //
      // EAX of subleaf 0 is the highest subleaf.
      //
      Last = MIN (E.Eax, 8);
      for (Sub = 1; Sub <= Last; Sub++) {
        Keep (List, Probe (List, Leaf, Sub, &E));
      }

      break;

    default:
      break;
  }
}

STATIC
VOID
EFIAPI
CollectHere (
  IN OUT VOID  *Buffer
  )
{
  RW_CPUID_LIST   *List;
  RW_CPUID_ENTRY  E;
  UINT32          Max;
  UINT32          Leaf;
  BOOLEAN         Hypervisor;

  List        = (RW_CPUID_LIST *)Buffer;
  List->Count = 0;
  Max         = Probe (List, 0, 0, &E)->Eax;
  Hypervisor  = FALSE;
  for (Leaf = 0; (Leaf <= Max) && (Leaf < 0x80); Leaf++) {
    CollectLeaf (List, Leaf);
    if (Leaf == 1) {
      Hypervisor = (BOOLEAN)((List->Entries[List->Count - 1].Ecx & BIT31) != 0);
    }
  }

  if (Hypervisor) {
    Max = Probe (List, 0x40000000, 0, &E)->Eax;
    if ((Max >= 0x40000000) && (Max <= 0x400000FF)) {
      for (Leaf = 0x40000000; (Leaf <= Max) && (Leaf <= 0x40000010); Leaf++) {
        CollectLeaf (List, Leaf);
      }
    }
  }

  Max = Probe (List, 0x80000000, 0, &E)->Eax;
  if ((Max >= 0x80000000) && (Max <= 0x800000FF)) {
    for (Leaf = 0x80000000; (Leaf <= Max) && (Leaf <= 0x80000040); Leaf++) {
      CollectLeaf (List, Leaf);
    }
  }
}

EFI_STATUS
RwCpuidCollect (
  IN  UINTN          Cpu,
  OUT RW_CPUID_LIST  *List
  )
{
  EFI_MP_SERVICES_PROTOCOL  *Mp;
  RW_CPU_INFO               Info;
  EFI_STATUS                Status;

  ZeroMem (List, sizeof (*List));
  List->Cpu = Cpu;
  Mp        = (EFI_MP_SERVICES_PROTOCOL *)RwCoreMp ();
  if ((Mp == NULL) || (Cpu == RwCpuBsp ())) {
    CollectHere (List);
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (RwCpuGetInfo (Cpu, &Info))) {
    return EFI_NOT_FOUND;
  }

  if (!Info.Enabled) {
    return EFI_NOT_READY;
  }

  Status = Mp->StartupThisAP (Mp, CollectHere, Cpu, NULL, CPUID_AP_TIMEOUT_US, List, NULL);
  if (EFI_ERROR (Status)) {
    List->Count = 0;
  }

  List->Cpu = Cpu;
  return Status;
}

CONST RW_CPUID_ENTRY *
RwCpuidFind (
  IN CONST RW_CPUID_LIST  *List,
  IN UINT32               Leaf,
  IN UINT32               Sub
  )
{
  UINTN  Index;

  for (Index = 0; Index < List->Count; Index++) {
    if ((List->Entries[Index].Leaf == Leaf) && (List->Entries[Index].Sub == Sub)) {
      return &List->Entries[Index];
    }
  }

  return NULL;
}

//
// ---- Decoders --------------------------------------------------------------
//

STATIC
VOID
Regs4 (
  IN  UINT32  A,
  IN  UINT32  B,
  IN  UINT32  C,
  OUT CHAR8   *Text      // at least 17 bytes
  )
{
  CopyMem (&Text[0], &A, 4);
  CopyMem (&Text[4], &B, 4);
  CopyMem (&Text[8], &C, 4);
  Text[12] = '\0';
}

STATIC
VOID
Brand (
  IN  CONST RW_CPUID_LIST  *List,
  OUT CHAR8                *Text,
  IN  UINTN                Size
  )
{
  CHAR8                 Raw[49];
  UINT32                Leaf;
  CONST RW_CPUID_ENTRY  *E;
  UINTN                 Start;
  UINTN                 Index;

  ZeroMem (Raw, sizeof (Raw));
  for (Leaf = 0; Leaf < 3; Leaf++) {
    E = RwCpuidFind (List, 0x80000002 + Leaf, 0);
    if (E == NULL) {
      AsciiStrCpyS (Text, Size, "");
      return;
    }

    CopyMem (&Raw[Leaf * 16], &E->Eax, 4);
    CopyMem (&Raw[Leaf * 16 + 4], &E->Ebx, 4);
    CopyMem (&Raw[Leaf * 16 + 8], &E->Ecx, 4);
    CopyMem (&Raw[Leaf * 16 + 12], &E->Edx, 4);
  }

  for (Index = 0; Index < 48; Index++) {
    if ((Raw[Index] != 0) && ((Raw[Index] < 0x20) || (Raw[Index] > 0x7E))) {
      Raw[Index] = '?';
    }
  }

  for (Start = 0; Raw[Start] == ' '; Start++) {
  }

  AsciiStrCpyS (Text, Size, &Raw[Start]);
}

STATIC
VOID
BitsField (
  IN OUT RW_INFO      *Info,
  IN     UINT8        Level,
  IN     CONST CHAR8  *Name,
  IN     UINT32       Value,
  IN     CONST CHAR8  **Names,
  IN     UINTN        Count
  )
{
  RW_STR  Str;

  RwStrInit (&Str);
  RwStrAppendBits (&Str, Value, Names, Count, "none");
  RwInfoField (Info, Level, RwEmNormal, Name, "%a", Str.Buf);
  RwStrFree (&Str);
}

STATIC
VOID
Signature (
  IN  UINT32  Eax,
  OUT CHAR8   *Text,
  IN  UINTN   Size
  )
{
  UINT32  Family;
  UINT32  Model;

  Family = (Eax >> 8) & 0xF;
  Model  = (Eax >> 4) & 0xF;
  if ((Family == 6) || (Family == 0xF)) {
    Model |= ((Eax >> 16) & 0xF) << 4;
  }

  if (Family == 0xF) {
    Family += (Eax >> 20) & 0xFF;
  }

  AsciiSPrint (Text, Size, "Family %02Xh, Model %02Xh, Stepping %Xh", Family, Model, Eax & 0xF);
}

STATIC
CONST CHAR8 *
CacheType (
  IN UINT32  Type
  )
{
  switch (Type) {
    case 1:
      return "Data";
    case 2:
      return "Instruction";
    case 3:
      return "Unified";
    default:
      return "Unknown";
  }
}

STATIC
VOID
CacheText (
  IN  CONST RW_CPUID_ENTRY  *E,
  OUT CHAR8                 *Name,
  IN  UINTN                 NameSize,
  OUT CHAR8                 *Text,
  IN  UINTN                 TextSize
  )
{
  UINT32  Ways;
  UINT32  Parts;
  UINT32  Line;
  UINT64  Bytes;

  Ways  = (E->Ebx >> 22) + 1;
  Parts = ((E->Ebx >> 12) & 0x3FF) + 1;
  Line  = (E->Ebx & 0xFFF) + 1;
  Bytes = (UINT64)Ways * Parts * Line * ((UINT64)E->Ecx + 1);
  AsciiSPrint (Name, NameSize, "L%d %a", (E->Eax >> 5) & 7, CacheType (E->Eax & 0x1F));
  if (Bytes >= SIZE_1MB) {
    AsciiSPrint (Text, TextSize, "%Ld MB", RShiftU64 (Bytes, 20));
  } else {
    AsciiSPrint (Text, TextSize, "%Ld KB", RShiftU64 (Bytes, 10));
  }

  AsciiSPrint (
    Text + AsciiStrLen (Text),
    TextSize - AsciiStrLen (Text),
    ", %d-way%a, %d-byte line, shared by %d thread%a",
    Ways,
    ((E->Eax & BIT9) != 0) ? " (fully associative)" : "",
    Line,
    ((E->Eax >> 14) & 0xFFF) + 1,
    (((E->Eax >> 14) & 0xFFF) == 0) ? "" : "s"
    );
}

STATIC
CONST CHAR8 *
TopologyType (
  IN UINT32  Type
  )
{
  STATIC CONST CHAR8  *Names[] = { "Invalid", "SMT", "Core", "Module", "Tile", "Die", "Die group" };

  return (Type < ARRAY_SIZE (Names)) ? Names[Type] : "Unknown";
}

VOID
RwCpuidDecode (
  IN     CONST RW_CPUID_LIST  *List,
  IN OUT RW_INFO              *Info
  )
{
  CONST RW_CPUID_ENTRY  *E;
  CONST RW_CPUID_ENTRY  *L1;
  CHAR8                 Text[128];
  CHAR8                 Name[48];
  UINT32                Leaf;
  UINTN                 Index;

  RwInfoSection (Info, 0, "Processor");
  E = RwCpuidFind (List, 0, 0);
  if (E == NULL) {
    RwInfoText (Info, 1, RwEmAlert, "No CPUID data");
    return;
  }

  Regs4 (E->Ebx, E->Edx, E->Ecx, Text);
  RwInfoField (Info, 1, RwEmStrong, "Vendor", "%a", Text);
  Brand (List, Text, sizeof (Text));
  if (Text[0] != '\0') {
    RwInfoField (Info, 1, RwEmStrong, "Brand", "%a", Text);
  }

  L1 = RwCpuidFind (List, 1, 0);
  if (L1 != NULL) {
    Signature (L1->Eax, Text, sizeof (Text));
    RwInfoField (Info, 1, RwEmNormal, "Signature", "%a (%08X)", Text, L1->Eax);
    RwInfoField (Info, 1, RwEmNormal, "Initial APIC ID", "%d", L1->Ebx >> 24);
    RwInfoField (Info, 1, RwEmNormal, "Logical processor IDs", "%d", (L1->Ebx >> 16) & 0xFF);
    RwInfoField (Info, 1, RwEmNormal, "CLFLUSH line size", "%d bytes", ((L1->Ebx >> 8) & 0xFF) * 8);
  }

  RwInfoField (Info, 1, RwEmNormal, "Highest standard leaf", "%08X", E->Eax);
  E = RwCpuidFind (List, 0x80000000, 0);
  if (E != NULL) {
    RwInfoField (Info, 1, RwEmNormal, "Highest extended leaf", "%08X", E->Eax);
  }

  E = RwCpuidFind (List, 0x40000000, 0);
  if (E != NULL) {
    Regs4 (E->Ebx, E->Ecx, E->Edx, Text);
    RwInfoField (Info, 1, RwEmNormal, "Hypervisor", "%a", Text);
  }

  E = RwCpuidFind (List, 0x16, 0);
  if ((E != NULL) && (E->Eax != 0)) {
    RwInfoField (Info, 1, RwEmNormal, "Base / max / bus frequency", "%d / %d / %d MHz", E->Eax & 0xFFFF, E->Ebx & 0xFFFF, E->Ecx & 0xFFFF);
  }

  E = RwCpuidFind (List, 0x15, 0);
  if ((E != NULL) && (E->Eax != 0) && (E->Ebx != 0)) {
    RwInfoField (Info, 1, RwEmNormal, "TSC / crystal ratio", "%d / %d, crystal %d Hz", E->Ebx, E->Eax, E->Ecx);
  }

  E = RwCpuidFind (List, 0x80000008, 0);
  if (E != NULL) {
    RwInfoField (Info, 1, RwEmNormal, "Address sizes", "%d-bit physical, %d-bit linear", E->Eax & 0xFF, (E->Eax >> 8) & 0xFF);
  }

  RwInfoSection (Info, 0, "Features");
  if (L1 != NULL) {
    BitsField (Info, 1, "Leaf 1 EDX", L1->Edx, mL1Edx, 32);
    BitsField (Info, 1, "Leaf 1 ECX", L1->Ecx, mL1Ecx, 32);
  }

  E = RwCpuidFind (List, 7, 0);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 7 EBX", E->Ebx, mL7Ebx, 32);
    BitsField (Info, 1, "Leaf 7 ECX", E->Ecx, mL7Ecx, 32);
    BitsField (Info, 1, "Leaf 7 EDX", E->Edx, mL7Edx, 32);
  }

  E = RwCpuidFind (List, 7, 1);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 7.1 EAX", E->Eax, mL71Eax, 32);
  }

  E = RwCpuidFind (List, 6, 0);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 6 EAX", E->Eax, mL6Eax, 16);
  }

  E = RwCpuidFind (List, 0xD, 1);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 0Dh.1 EAX", E->Eax, mXsaveFeat, 8);
  }

  E = RwCpuidFind (List, 0x80000001, 0);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 80000001h EDX", E->Edx, mX1Edx, 32);
    BitsField (Info, 1, "Leaf 80000001h ECX", E->Ecx, mX1Ecx, 32);
  }

  E = RwCpuidFind (List, 0x80000007, 0);
  if (E != NULL) {
    BitsField (Info, 1, "Leaf 80000007h EDX", E->Edx, mX7Edx, 16);
  }

  E = RwCpuidFind (List, 0x80000008, 0);
  if ((E != NULL) && (E->Ebx != 0)) {
    BitsField (Info, 1, "Leaf 80000008h EBX", E->Ebx, mX8Ebx, 32);
  }

  E = RwCpuidFind (List, 0xD, 0);
  if (E != NULL) {
    RwInfoSection (Info, 0, "Extended state (XSAVE)");
    BitsField (Info, 1, "Supported XCR0", E->Eax, mXcr0, 20);
    RwInfoField (Info, 1, RwEmNormal, "Save area (enabled / all)", "%d / %d bytes", E->Ebx, E->Ecx);
  }

  //
  // Caches: leaf 4 (Intel) or 8000001Dh (AMD).
  //
  Leaf = (RwCpuidFind (List, 4, 0) != NULL) ? 4 : 0x8000001D;
  if (RwCpuidFind (List, Leaf, 0) != NULL) {
    RwInfoSection (Info, 0, "Caches (leaf %Xh)", Leaf);
    for (Index = 0; Index < List->Count; Index++) {
      E = &List->Entries[Index];
      if (E->Leaf == Leaf) {
        CacheText (E, Name, sizeof (Name), Text, sizeof (Text));
        RwInfoField (Info, 1, RwEmNormal, Name, "%a", Text);
      }
    }
  } else if ((E = RwCpuidFind (List, 0x80000006, 0)) != NULL) {
    RwInfoSection (Info, 0, "Caches (leaf 80000006h)");
    RwInfoField (Info, 1, RwEmNormal, "L2", "%d KB, %d-byte line", E->Ecx >> 16, E->Ecx & 0xFF);
    if ((E->Edx >> 18) != 0) {
      RwInfoField (Info, 1, RwEmNormal, "L3", "%d KB, %d-byte line", (E->Edx >> 18) * 512, E->Edx & 0xFF);
    }
  }

  //
  // Topology: leaf 1Fh, else 0Bh.
  //
  Leaf = (RwCpuidFind (List, 0x1F, 0) != NULL) ? 0x1F : 0xB;
  if ((RwCpuidFind (List, Leaf, 0) != NULL) && (RwCpuidFind (List, Leaf, 0)->Ebx != 0)) {
    RwInfoSection (Info, 0, "Topology (leaf %Xh)", Leaf);
    for (Index = 0; Index < List->Count; Index++) {
      E = &List->Entries[Index];
      if ((E->Leaf == Leaf) && (((E->Ecx >> 8) & 0xFF) != 0)) {
        RwInfoField (
          Info,
          1,
          RwEmNormal,
          TopologyType ((E->Ecx >> 8) & 0xFF),
          "%d logical processors, APIC ID shift %d",
          E->Ebx & 0xFFFF,
          E->Eax & 0x1F
          );
      }
    }

    E = RwCpuidFind (List, Leaf, 0);
    RwInfoField (Info, 1, RwEmStrong, "x2APIC ID", "%d", E->Edx);
  }
}

VOID
RwCpuidDecodeLeaf (
  IN     CONST RW_CPUID_LIST   *List,
  IN     CONST RW_CPUID_ENTRY  *E,
  IN OUT RW_INFO               *Info
  )
{
  CHAR8  Text[128];
  CHAR8  Name[48];

  RwInfoSection (Info, 0, "Leaf %08X.%X  %a", E->Leaf, E->Sub, RwCpuidLeafName (E->Leaf));
  RwInfoField (Info, 1, RwEmStrong, "EAX", "%08X", E->Eax);
  RwInfoField (Info, 1, RwEmStrong, "EBX", "%08X", E->Ebx);
  RwInfoField (Info, 1, RwEmStrong, "ECX", "%08X", E->Ecx);
  RwInfoField (Info, 1, RwEmStrong, "EDX", "%08X", E->Edx);
  switch (E->Leaf) {
    case 0x00000000:
      Regs4 (E->Ebx, E->Edx, E->Ecx, Text);
      RwInfoField (Info, 1, RwEmNormal, "Vendor", "%a", Text);
      break;
    case 0x40000000:
      Regs4 (E->Ebx, E->Ecx, E->Edx, Text);
      RwInfoField (Info, 1, RwEmNormal, "Hypervisor", "%a", Text);
      break;
    case 0x00000001:
      Signature (E->Eax, Text, sizeof (Text));
      RwInfoField (Info, 1, RwEmNormal, "Signature", "%a, type %d", Text, (E->Eax >> 12) & 3);
      RwInfoField (Info, 1, RwEmNormal, "Brand index", "%d", E->Ebx & 0xFF);
      RwInfoField (Info, 1, RwEmNormal, "Initial APIC ID", "%d", E->Ebx >> 24);
      BitsField (Info, 1, "EDX features", E->Edx, mL1Edx, 32);
      BitsField (Info, 1, "ECX features", E->Ecx, mL1Ecx, 32);
      break;
    case 0x00000006:
      BitsField (Info, 1, "EAX features", E->Eax, mL6Eax, 16);
      RwInfoField (Info, 1, RwEmNormal, "Thermal thresholds", "%d", E->Ebx & 0xF);
      break;
    case 0x00000007:
      if (E->Sub == 0) {
        RwInfoField (Info, 1, RwEmNormal, "Highest subleaf", "%d", E->Eax);
        BitsField (Info, 1, "EBX features", E->Ebx, mL7Ebx, 32);
        BitsField (Info, 1, "ECX features", E->Ecx, mL7Ecx, 32);
        BitsField (Info, 1, "EDX features", E->Edx, mL7Edx, 32);
      } else if (E->Sub == 1) {
        BitsField (Info, 1, "EAX features", E->Eax, mL71Eax, 32);
      }

      break;
    case 0x00000004:
    case 0x8000001D:
      CacheText (E, Name, sizeof (Name), Text, sizeof (Text));
      RwInfoField (Info, 1, RwEmNormal, Name, "%a", Text);
      RwInfoField (Info, 1, RwEmNormal, "Sets", "%d", E->Ecx + 1);
      break;
    case 0x0000000B:
    case 0x0000001F:
    case 0x80000026:
      RwInfoField (Info, 1, RwEmNormal, "Level type", "%a", TopologyType ((E->Ecx >> 8) & 0xFF));
      RwInfoField (Info, 1, RwEmNormal, "Logical processors", "%d", E->Ebx & 0xFFFF);
      RwInfoField (Info, 1, RwEmNormal, "APIC ID shift", "%d", E->Eax & 0x1F);
      RwInfoField (Info, 1, RwEmNormal, "x2APIC ID", "%d", E->Edx);
      break;
    case 0x0000000D:
      if (E->Sub == 0) {
        BitsField (Info, 1, "Supported XCR0 (low)", E->Eax, mXcr0, 20);
        RwInfoField (Info, 1, RwEmNormal, "Save area (enabled / all)", "%d / %d bytes", E->Ebx, E->Ecx);
      } else if (E->Sub == 1) {
        BitsField (Info, 1, "EAX features", E->Eax, mXsaveFeat, 8);
      } else {
        RwInfoField (Info, 1, RwEmNormal, "Component", "%a: %d bytes at offset %d", (E->Sub < 20) ? mXcr0[E->Sub] : "?", E->Eax, E->Ebx);
      }

      break;
    case 0x00000016:
      RwInfoField (Info, 1, RwEmNormal, "Base / max / bus", "%d / %d / %d MHz", E->Eax & 0xFFFF, E->Ebx & 0xFFFF, E->Ecx & 0xFFFF);
      break;
    case 0x80000001:
      BitsField (Info, 1, "EDX features", E->Edx, mX1Edx, 32);
      BitsField (Info, 1, "ECX features", E->Ecx, mX1Ecx, 32);
      break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
      CopyMem (&Text[0], &E->Eax, 4);
      CopyMem (&Text[4], &E->Ebx, 4);
      CopyMem (&Text[8], &E->Ecx, 4);
      CopyMem (&Text[12], &E->Edx, 4);
      Text[16] = '\0';
      RwInfoField (Info, 1, RwEmNormal, "Brand part", "\"%a\"", Text);
      Brand (List, Text, sizeof (Text));
      RwInfoField (Info, 1, RwEmNormal, "Brand", "%a", Text);
      break;
    case 0x80000007:
      BitsField (Info, 1, "EDX features", E->Edx, mX7Edx, 16);
      break;
    case 0x80000008:
      RwInfoField (Info, 1, RwEmNormal, "Physical address bits", "%d", E->Eax & 0xFF);
      RwInfoField (Info, 1, RwEmNormal, "Linear address bits", "%d", (E->Eax >> 8) & 0xFF);
      BitsField (Info, 1, "EBX features", E->Ebx, mX8Ebx, 32);
      break;
    default:
      break;
  }
}
