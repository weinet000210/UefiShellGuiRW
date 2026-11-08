/** @file
  AML disassembler: turns DSDT/SSDT definition blocks into ASL source text
  (classic ASL 1.0 operator syntax), written from the ACPI specification's
  AML grammar (ACPI 6.5, chapter 20).

  Method invocations in AML carry no argument count, so a silent first pass
  over every DSDT/SSDT collects Method and External declarations; the
  display pass looks names up against that table using the ACPI namespace
  search rules.

  Ported from uefirrw AmlView.c. All parser state lives in RW_AML_CTX.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreInternal.h"

#define AML_MAX_DEPTH  96

typedef struct {
  UINT8          Op;
  CONST CHAR8    *Name;
  CONST CHAR8    *Args;   // a TermArg, s SuperName, t Target, n NameString, b/w/d constant
} AML_OP;

STATIC CONST AML_OP  mOps[] = {
  { 0x70, "Store",                  "as"     },
  { 0x71, "RefOf",                  "s"      },
  { 0x72, "Add",                    "aat"    },
  { 0x73, "Concatenate",            "aat"    },
  { 0x74, "Subtract",               "aat"    },
  { 0x75, "Increment",              "s"      },
  { 0x76, "Decrement",              "s"      },
  { 0x77, "Multiply",               "aat"    },
  { 0x78, "Divide",                 "aatt"   },
  { 0x79, "ShiftLeft",              "aat"    },
  { 0x7A, "ShiftRight",             "aat"    },
  { 0x7B, "And",                    "aat"    },
  { 0x7C, "NAnd",                   "aat"    },
  { 0x7D, "Or",                     "aat"    },
  { 0x7E, "NOr",                    "aat"    },
  { 0x7F, "XOr",                    "aat"    },
  { 0x80, "Not",                    "at"     },
  { 0x81, "FindSetLeftBit",         "at"     },
  { 0x82, "FindSetRightBit",        "at"     },
  { 0x83, "DerefOf",                "a"      },
  { 0x84, "ConcatenateResTemplate", "aat"    },
  { 0x85, "Mod",                    "aat"    },
  { 0x86, "Notify",                 "sa"     },
  { 0x87, "SizeOf",                 "s"      },
  { 0x88, "Index",                  "aat"    },
  { 0x8A, "CreateDWordField",       "aan"    },
  { 0x8B, "CreateWordField",        "aan"    },
  { 0x8C, "CreateByteField",        "aan"    },
  { 0x8D, "CreateBitField",         "aan"    },
  { 0x8E, "ObjectType",             "s"      },
  { 0x8F, "CreateQWordField",       "aan"    },
  { 0x90, "LAnd",                   "aa"     },
  { 0x91, "LOr",                    "aa"     },
  { 0x92, "LNot",                   "a"      },
  { 0x93, "LEqual",                 "aa"     },
  { 0x94, "LGreater",               "aa"     },
  { 0x95, "LLess",                  "aa"     },
  { 0x96, "ToBuffer",               "at"     },
  { 0x97, "ToDecimalString",        "at"     },
  { 0x98, "ToHexString",            "at"     },
  { 0x99, "ToInteger",              "at"     },
  { 0x9C, "ToString",               "aat"    },
  { 0x9D, "CopyObject",             "as"     },
  { 0x9E, "Mid",                    "aaat"   },
  { 0x9F, "Continue",               ""       },
  { 0xA3, "Noop",                   ""       },
  { 0xA4, "Return",                 "a"      },
  { 0xA5, "Break",                  ""       },
  { 0xCC, "BreakPoint",             ""       },
  { 0x06, "Alias",                  "nn"     }
};

STATIC CONST AML_OP  mExtOps[] = {
  { 0x01, "Mutex",      "nb"     },
  { 0x02, "Event",      "n"      },
  { 0x12, "CondRefOf",  "st"     },
  { 0x13, "CreateField", "aaan"  },
  { 0x1F, "LoadTable",  "aaaaaa" },
  { 0x20, "Load",       "st"     },
  { 0x21, "Stall",      "a"      },
  { 0x22, "Sleep",      "a"      },
  { 0x23, "Acquire",    "sw"     },
  { 0x24, "Signal",     "s"      },
  { 0x25, "Wait",       "sa"     },
  { 0x26, "Reset",      "s"      },
  { 0x27, "Release",    "s"      },
  { 0x28, "FromBCD",    "at"     },
  { 0x29, "ToBCD",      "at"     },
  { 0x30, "Revision",   ""       },
  { 0x31, "Debug",      ""       },
  { 0x32, "Fatal",      "bda"    },
  { 0x33, "Timer",      ""       },
  { 0x88, "DataRegion", "naaa"   }
};

STATIC CONST CHAR8  *mRegionSpace[] = {
  "SystemMemory", "SystemIO", "PCI_Config", "EmbeddedControl", "SMBus", "SystemCMOS",
  "PciBarTarget", "IPMI", "GeneralPurposeIo", "GenericSerialBus", "PCC", "PRM", "FFixedHW"
};
STATIC CONST CHAR8  *mAccess[]   = { "AnyAcc", "ByteAcc", "WordAcc", "DWordAcc", "QWordAcc", "BufferAcc", "AnyAcc", "AnyAcc" };
STATIC CONST CHAR8  *mUpdate[]   = { "Preserve", "WriteAsOnes", "WriteAsZeros", "Preserve" };
STATIC CONST CHAR8  *mObjType[]  = {
  "UnknownObj", "IntObj", "StrObj", "BuffObj", "PkgObj", "FieldUnitObj", "DeviceObj", "EventObj",
  "MethodObj", "MutexObj", "OpRegionObj", "PowerResObj", "ProcessorObj", "ThermalZoneObj", "BuffFieldObj", "DDBHandleObj"
};
STATIC CONST CHAR8  *mMatchOp[]  = { "MTR", "MEQ", "MLE", "MLT", "MGE", "MGT" };
STATIC BOOLEAN ParseArg (IN OUT RW_AML_CTX *Ctx, IN UINT8 *End, IN OUT RW_STR *Out, IN BOOLEAN AllowInvoke);
STATIC BOOLEAN ParseTermList (IN OUT RW_AML_CTX *Ctx, IN UINT8 *End, IN UINTN Indent);

//
// ---- String helpers --------------------------------------------------------
//

/**
  Append Src, putting Spaces spaces after every newline in it.
**/
STATIC
VOID
SbPutIndented (
  IN OUT RW_STR  *S,
  IN     RW_STR  *Src,
  IN     UINTN   Spaces
  )
{
  UINTN  Start;
  UINTN  Index;
  UINTN  K;

  Start = 0;
  for (Index = 0; Index <= Src->Len; Index++) {
    if ((Index == Src->Len) || (Src->Buf[Index] == '\n')) {
      RwStrAppendN (S, Src->Buf + Start, Index - Start);
      if (Index < Src->Len) {
        RwStrAppendChar (S, '\n');
        for (K = 0; K < Spaces; K++) {
          RwStrAppendChar (S, ' ');
        }
      }

      Start = Index + 1;
    }
  }
}

//
// ---- Output ----------------------------------------------------------------
//

/**
  Output one finished line to the RW_STR or RW_INFO the context writes to.
**/
STATIC
VOID
EmitLine (
  IN OUT RW_AML_CTX   *Ctx,
  IN     UINT8        Attr,
  IN     CONST CHAR8  *Line
  )
{
  if (Ctx->Out != NULL) {
    RwStrAppend (Ctx->Out, Line);
    RwStrAppend (Ctx->Out, Ctx->Crlf ? "\r\n" : "\n");
  }

  if (Ctx->Info != NULL) {
    RwInfoAdd (Ctx->Info, RwKindText, 0, Attr, Line, NULL);
  }
}

/**
  Emit text (may hold newlines) at Indent levels of four spaces.
**/
STATIC
VOID
Emit (
  IN OUT RW_AML_CTX   *Ctx,
  IN     UINTN        Indent,
  IN     UINT8        Attr,
  IN     CONST CHAR8  *Text
  )
{
  RW_STR  Line;
  UINTN   Pos;
  UINTN   Start;
  UINTN   Index;
  UINTN   Len;

  if (Ctx->Silent || ((Ctx->Out == NULL) && (Ctx->Info == NULL))) {
    return;
  }

  RwStrInit (&Line);
  Len   = AsciiStrLen (Text);
  Start = 0;
  for (Index = 0; Index <= Len; Index++) {
    if ((Index == Len) || (Text[Index] == '\n')) {
      RwStrReset (&Line);
      for (Pos = 0; Pos < Indent * 4; Pos++) {
        RwStrAppendChar (&Line, ' ');
      }

      while (Start < Index) {
        if (Text[Start] != '\r') {
          RwStrAppendChar (&Line, Text[Start]);
        }

        Start++;
      }

      EmitLine (Ctx, Attr, Line.Buf);
      Start = Index + 1;
    }
  }

  RwStrFree (&Line);
}

STATIC
VOID
EFIAPI
EmitFmt (
  IN OUT RW_AML_CTX   *Ctx,
  IN     UINTN        Indent,
  IN     UINT8        Attr,
  IN     CONST CHAR8  *Fmt,
  ...
  )
{
  VA_LIST  Args;
  RW_STR   Tmp;

  if (Ctx->Silent) {
    return;
  }

  RwStrInit (&Tmp);
  VA_START (Args, Fmt);
  RwStrAppendV (&Tmp, Fmt, Args);
  VA_END (Args);
  Emit (Ctx, Indent, Attr, Tmp.Buf);
  RwStrFree (&Tmp);
}

//
// ---- Method table ----------------------------------------------------------
//

STATIC
VOID
AddMethod (
  IN OUT RW_AML_CTX  *Ctx,
  IN CONST CHAR8  *Path,
  IN UINTN        Len,
  IN UINT8        Args
  )
{
  UINTN       Index;
  RW_AML_METHOD  *Grown;
  UINTN       NewCap;

  if ((Len == 0) || (Len > RW_AML_MAX_PATH)) {
    return;
  }

  for (Index = 0; Index < Ctx->MethodCount; Index++) {
    if ((Ctx->Methods[Index].Len == Len) && (CompareMem (Ctx->Methods[Index].Path, Path, Len) == 0)) {
      Ctx->Methods[Index].Args = Args;
      return;
    }
  }

  if (Ctx->MethodCount == Ctx->MethodCap) {
    NewCap = (Ctx->MethodCap == 0) ? 256 : Ctx->MethodCap * 2;
    Grown  = ReallocatePool (Ctx->MethodCap * sizeof (RW_AML_METHOD), NewCap * sizeof (RW_AML_METHOD), Ctx->Methods);
    if (Grown == NULL) {
      return;
    }

    Ctx->Methods   = Grown;
    Ctx->MethodCap = NewCap;
  }

  CopyMem (Ctx->Methods[Ctx->MethodCount].Path, Path, Len);
  Ctx->Methods[Ctx->MethodCount].Len  = (UINT8)Len;
  Ctx->Methods[Ctx->MethodCount].Args = Args;
  Ctx->MethodCount++;
}

STATIC
INTN
FindExact (
  IN OUT RW_AML_CTX  *Ctx,
  IN CONST CHAR8  *Path,
  IN UINTN        Len
  )
{
  UINTN  Index;

  for (Index = 0; Index < Ctx->MethodCount; Index++) {
    if ((Ctx->Methods[Index].Len == Len) && (CompareMem (Ctx->Methods[Index].Path, Path, Len) == 0)) {
      return Ctx->Methods[Index].Args;
    }
  }

  return -1;
}

/**
  Argument count of the method a name refers to, or -1 when it names no
  known method.
**/
STATIC
INTN
LookupMethod (
  IN OUT RW_AML_CTX  *Ctx,
  IN CONST CHAR8  *Abs,
  IN UINTN        AbsLen,
  IN BOOLEAN      Single
  )
{
  CHAR8  Try[RW_AML_MAX_PATH];
  UINTN  S;
  UINTN  Index;
  INTN   Args;

  if (AbsLen < 4) {
    return -1;
  }

  if (!Single) {
    return FindExact (Ctx, Abs, AbsLen);
  }

  //
  // Single segment: search the current scope, then each parent, up to root.
  //
  S = Ctx->ScopeLen;
  for ( ; ;) {
    if (S + 4 <= RW_AML_MAX_PATH) {
      CopyMem (Try, Ctx->Scope, S);
      CopyMem (Try + S, Abs + AbsLen - 4, 4);
      Args = FindExact (Ctx, Try, S + 4);
      if (Args >= 0) {
        return Args;
      }
    }

    if (S < 4) {
      break;
    }

    S -= 4;
  }

  for (Index = 0; Index < Ctx->MethodCount; Index++) {
    if (CompareMem (Ctx->Methods[Index].Path + Ctx->Methods[Index].Len - 4, Abs + AbsLen - 4, 4) == 0) {
      return Ctx->Methods[Index].Args;
    }
  }

  return -1;
}

//
// ---- Primitive readers -----------------------------------------------------
//

STATIC
BOOLEAN
ReadPkgValue (
  IN OUT RW_AML_CTX  *Ctx,
  IN  UINT8   *End,
  OUT UINT32  *Value
  )
{
  UINT8   Lead;
  UINTN   Count;
  UINTN   Index;
  UINT32  Len;

  if (Ctx->P >= End) {
    return FALSE;
  }

  Lead  = *Ctx->P++;
  Count = Lead >> 6;
  if (Count == 0) {
    *Value = Lead & 0x3F;
    return TRUE;
  }

  if (Ctx->P + Count > End) {
    return FALSE;
  }

  Len = Lead & 0x0F;
  for (Index = 0; Index < Count; Index++) {
    Len |= (UINT32)(*Ctx->P++) << (4 + 8 * Index);
  }

  *Value = Len;
  return TRUE;
}

STATIC
BOOLEAN
ReadPkgLength (
  IN OUT RW_AML_CTX  *Ctx,
  IN  UINT8  *End,
  OUT UINT8  **PkgEnd
  )
{
  UINT8   *Start;
  UINT32  Len;

  Start = Ctx->P;
  if (!ReadPkgValue (Ctx, End, &Len)) {
    return FALSE;
  }

  if ((Start + Len > End) || (Start + Len < Ctx->P)) {
    return FALSE;
  }

  *PkgEnd = Start + Len;
  return TRUE;
}

STATIC
BOOLEAN
IsLeadNameChar (
  IN UINT8  C
  )
{
  return (BOOLEAN)((C == '_') || ((C >= 'A') && (C <= 'Z')));
}

STATIC
BOOLEAN
IsNameStart (
  IN UINT8  C
  )
{
  return (BOOLEAN)(IsLeadNameChar (C) || (C == '\\') || (C == '^') || (C == 0x2E) || (C == 0x2F));
}

STATIC
VOID
PutSeg (
  IN OUT RW_STR           *Out,
  IN     CONST UINT8  *Seg
  )
{
  CHAR8  S[5];
  UINTN   Len;
  UINTN   Index;

  for (Index = 0; Index < 4; Index++) {
    S[Index] = ((Seg[Index] >= 0x20) && (Seg[Index] < 0x7F)) ? (CHAR8)Seg[Index] : '?';
  }

  Len = 4;
  while ((Len > 1) && (S[Len - 1] == '_')) {
    Len--;
  }

  S[Len] = '\0';
  RwStrAppend (Out, S);
}

/**
  NameString. Abs receives the absolute path as concatenated 4-byte
  segments; Single is TRUE for a bare one-segment name (search rules apply).
**/
STATIC
BOOLEAN
ParseName (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8    *End,
  IN OUT RW_STR       *Out,
  OUT    CHAR8    *Abs,
  OUT    UINTN    *AbsLen,
  OUT    BOOLEAN  *Single
  )
{
  BOOLEAN  Root;
  UINTN    Parents;
  UINTN    Count;
  UINTN    Index;
  UINTN    Len;

  Root    = FALSE;
  Parents = 0;
  if ((Ctx->P < End) && (*Ctx->P == '\\')) {
    Root = TRUE;
    RwStrAppend (Out, "\\");
    Ctx->P++;
  } else {
    while ((Ctx->P < End) && (*Ctx->P == '^')) {
      Parents++;
      RwStrAppend (Out, "^");
      Ctx->P++;
    }
  }

  if (Ctx->P >= End) {
    return FALSE;
  }

  if (*Ctx->P == 0x2E) {
    Count = 2;
    Ctx->P++;
  } else if (*Ctx->P == 0x2F) {
    Ctx->P++;
    if (Ctx->P >= End) {
      return FALSE;
    }

    Count = *Ctx->P++;
  } else if (*Ctx->P == 0x00) {
    Count = 0;
    Ctx->P++;
  } else if (IsLeadNameChar (*Ctx->P)) {
    Count = 1;
  } else {
    return FALSE;
  }

  if (Ctx->P + Count * 4 > End) {
    return FALSE;
  }

  if (Root) {
    Len = 0;
  } else {
    Len = (Parents * 4 > Ctx->ScopeLen) ? 0 : Ctx->ScopeLen - Parents * 4;
    CopyMem (Abs, Ctx->Scope, Len);
  }

  for (Index = 0; Index < Count; Index++) {
    if (Index > 0) {
      RwStrAppend (Out, ".");
    }

    PutSeg (Out, Ctx->P);
    if (Len + 4 <= RW_AML_MAX_PATH) {
      CopyMem (Abs + Len, Ctx->P, 4);
      Len += 4;
    }

    Ctx->P += 4;
  }

  *AbsLen = Len;
  *Single = (BOOLEAN)(!Root && (Parents == 0) && (Count == 1));
  return TRUE;
}

STATIC
BOOLEAN
ParseNewName (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8  *End,
  IN OUT RW_STR     *Out,
  OUT    CHAR8  *Abs,
  OUT    UINTN  *AbsLen
  )
{
  BOOLEAN  Single;

  return ParseName (Ctx, End, Out, Abs, AbsLen, &Single);
}

/**
  An integer constant opcode, consumed. FALSE (nothing consumed) otherwise.
**/
STATIC
BOOLEAN
ReadConst (
  IN OUT RW_AML_CTX  *Ctx,
  IN  UINT8   *End,
  OUT UINT64  *Value
  )
{
  UINTN  Size;

  if (Ctx->P >= End) {
    return FALSE;
  }

  switch (*Ctx->P) {
    case 0x00: *Value = 0; Ctx->P++; return TRUE;
    case 0x01: *Value = 1; Ctx->P++; return TRUE;
    case 0xFF: *Value = MAX_UINT64; Ctx->P++; return TRUE;
    case 0x0A: Size = 1; break;
    case 0x0B: Size = 2; break;
    case 0x0C: Size = 4; break;
    case 0x0E: Size = 8; break;
    default:
      return FALSE;
  }

  if (Ctx->P + 1 + Size > End) {
    return FALSE;
  }

  *Value = 0;
  CopyMem (Value, Ctx->P + 1, Size);
  Ctx->P += 1 + Size;
  return TRUE;
}

//
// ---- Resource templates ----------------------------------------------------
//

STATIC
BOOLEAN
RtValid (
  IN UINT8  *P,
  IN UINTN  Len
  )
{
  UINTN  Off;
  UINTN  DLen;

  Off = 0;
  while (Off < Len) {
    if (P[Off] & 0x80) {
      if (Off + 3 > Len) {
        return FALSE;
      }

      DLen = 3 + ReadUnaligned16 ((UINT16 *)(P + Off + 1));
    } else {
      DLen = 1 + (P[Off] & 7);
      if ((P[Off] & 0x78) == 0x78) {
        return (BOOLEAN)(Off + DLen == Len);
      }
    }

    Off += DLen;
  }

  return FALSE;
}

STATIC
VOID
RtList16 (
  IN OUT RW_STR      *Out,
  IN     UINT16  Mask
  )
{
  UINTN    Bit;
  BOOLEAN  First;

  First = TRUE;
  RwStrAppend (Out, " {");
  for (Bit = 0; Bit < 16; Bit++) {
    if (Mask & (1 << Bit)) {
      RwStrAppendF (Out, First ? "%d" : ",%d", Bit);
      First = FALSE;
    }
  }

  RwStrAppend (Out, "}");
}

STATIC
VOID
RtResSource (
  IN OUT RW_STR     *Out,
  IN     UINT8  *D,
  IN     UINTN  DLen,
  IN     UINTN  Off
  )
{
  UINTN  Index;

  RwStrAppend (Out, "\"");
  for (Index = Off; (Index < DLen) && (D[Index] != 0); Index++) {
    RwStrAppendChar (Out, ((D[Index] >= 0x20) && (D[Index] < 0x7F)) ? (CHAR8)D[Index] : '?');
  }

  RwStrAppend (Out, "\"");
}

/**
  Address space descriptors (Word/DWord/QWord): Size is 2, 4 or 8.
**/
STATIC
VOID
RtAddress (
  IN OUT RW_STR     *Out,
  IN     UINT8  *D,
  IN     UINTN  DLen,
  IN     UINTN  Size
  )
{
  CONST CHAR8  *Prefix;
  CONST CHAR8  *Kind;
  UINT8         Type;
  UINT8         Gen;
  UINT8         Spec;
  UINT64        V[5];
  UINTN         Index;
  CONST CHAR8  *Cache[] = { "NonCacheable", "Cacheable", "WriteCombining", "Prefetchable" };
  CONST CHAR8  *IoRng[] = { "EntireRange", "NonISAOnlyRanges", "ISAOnlyRanges", "EntireRange" };

  Prefix = (Size == 2) ? "Word" : ((Size == 4) ? "DWord" : "QWord");
  if (DLen < 6 + 5 * Size) {
    RwStrAppendF (Out, "%aSpace ()  /* truncated */", Prefix);
    return;
  }

  Type = D[3];
  Gen  = D[4];
  Spec = D[5];
  for (Index = 0; Index < 5; Index++) {
    V[Index] = 0;
    CopyMem (&V[Index], D + 6 + Index * Size, Size);
  }

  Kind = (Type == 0) ? "Memory" : ((Type == 1) ? "IO" : ((Type == 2) ? "BusNumber" : "Space"));
  RwStrAppendF (
    Out,
    "%a%a (%a, ",
    Prefix,
    Kind,
    (Gen & 1) ? "ResourceConsumer" : "ResourceProducer"
    );
  if (Type == 0) {
    RwStrAppendF (
      Out,
      "%a, %a, %a, %a, %a,",
      (Gen & 2) ? "SubDecode" : "PosDecode",
      (Gen & 4) ? "MinFixed" : "MinNotFixed",
      (Gen & 8) ? "MaxFixed" : "MaxNotFixed",
      Cache[(Spec >> 1) & 3],
      (Spec & 1) ? "ReadWrite" : "ReadOnly"
      );
  } else if (Type == 1) {
    RwStrAppendF (
      Out,
      "%a, %a, %a, %a,",
      (Gen & 4) ? "MinFixed" : "MinNotFixed",
      (Gen & 8) ? "MaxFixed" : "MaxNotFixed",
      (Gen & 2) ? "SubDecode" : "PosDecode",
      IoRng[Spec & 3]
      );
  } else if (Type == 2) {
    RwStrAppendF (
      Out,
      "%a, %a, %a,",
      (Gen & 4) ? "MinFixed" : "MinNotFixed",
      (Gen & 8) ? "MaxFixed" : "MaxNotFixed",
      (Gen & 2) ? "SubDecode" : "PosDecode"
      );
  } else {
    RwStrAppendF (Out, "0x%02X, 0x%02X, 0x%02X,", Type, Gen, Spec);
  }

  for (Index = 0; Index < 5; Index++) {
    RwStrAppendF (Out, " 0x%0*lX%a", Size * 2, V[Index], (Index < 4) ? "," : "");
  }

  if (DLen > 6 + 5 * Size + 1) {
    RwStrAppendF (Out, ", 0x%02X, ", D[6 + 5 * Size]);
    RtResSource (Out, D, DLen, 6 + 5 * Size + 1);
  }

  RwStrAppend (Out, ")");
}

STATIC
VOID
RtDecode (
  IN OUT RW_STR     *Out,
  IN     UINT8  *P,
  IN     UINTN  Len
  )
{
  UINTN   Off;
  UINTN   DLen;
  UINT8   *D;
  UINT8   Tag;
  UINT8   F;
  UINTN   Index;
  UINT16  W;

  RwStrAppend (Out, "ResourceTemplate ()\n{");
  for (Off = 0; Off < Len; Off += DLen) {
    D = P + Off;
    if (D[0] & 0x80) {
      DLen = 3 + ReadUnaligned16 ((UINT16 *)(D + 1));
      Tag  = D[0] & 0x7F;
    } else {
      DLen = 1 + (D[0] & 7);
      Tag  = (D[0] >> 3) & 0x0F;
      if (Tag == 0x0F) {
        break;
      }
    }

    RwStrAppend (Out, "\n    ");
    if (!(D[0] & 0x80)) {
      switch (Tag) {
        case 0x04:
          W = ReadUnaligned16 ((UINT16 *)(D + 1));
          if (DLen >= 4) {
            F = D[3];
            RwStrAppendF (
              Out,
              "IRQ (%a, %a, %a, )",
              (F & 1) ? "Edge" : "Level",
              (F & 8) ? "ActiveLow" : "ActiveHigh",
              (F & 0x10) ? "Shared" : "Exclusive"
              );
          } else {
            RwStrAppend (Out, "IRQNoFlags ()");
          }

          RtList16 (Out, W);
          break;
        case 0x05:
          {
            CONST CHAR8  *Speed[] = { "Compatibility", "TypeA", "TypeB", "TypeF" };
            CONST CHAR8  *Xfer[]  = { "Transfer8", "Transfer8_16", "Transfer16", "Transfer16" };

            F = D[2];
            RwStrAppendF (Out, "DMA (%a, %a, %a, )", Speed[(F >> 5) & 3], (F & 4) ? "BusMaster" : "NotBusMaster", Xfer[F & 3]);
            RtList16 (Out, D[1]);
          }
          break;
        case 0x06:
          if (DLen >= 2) {
            RwStrAppendF (Out, "StartDependentFn (0x%02X, 0x%02X)", D[1] & 3, (D[1] >> 2) & 3);
          } else {
            RwStrAppend (Out, "StartDependentFnNoPri ()");
          }

          break;
        case 0x07:
          RwStrAppend (Out, "EndDependentFn ()");
          break;
        case 0x08:
          RwStrAppendF (
            Out,
            "IO (%a, 0x%04X, 0x%04X, 0x%02X, 0x%02X, )",
            (D[1] & 1) ? "Decode16" : "Decode10",
            ReadUnaligned16 ((UINT16 *)(D + 2)),
            ReadUnaligned16 ((UINT16 *)(D + 4)),
            D[6],
            D[7]
            );
          break;
        case 0x09:
          RwStrAppendF (Out, "FixedIO (0x%04X, 0x%02X, )", ReadUnaligned16 ((UINT16 *)(D + 1)) & 0x3FF, D[3]);
          break;
        case 0x0A:
          RwStrAppendF (
            Out,
            "FixedDMA (0x%04X, 0x%04X, Width%dbit, )",
            ReadUnaligned16 ((UINT16 *)(D + 1)),
            ReadUnaligned16 ((UINT16 *)(D + 3)),
            8 << D[5]
            );
          break;
        case 0x0E:
          RwStrAppend (Out, "VendorShort () {");
          for (Index = 1; Index < DLen; Index++) {
            RwStrAppendF (Out, "%a0x%02X", (Index > 1) ? ", " : "", D[Index]);
          }

          RwStrAppend (Out, "}");
          break;
        default:
          RwStrAppendF (Out, "/* small descriptor type 0x%X, %d bytes */", Tag, DLen);
          break;
      }

      continue;
    }

    switch (Tag) {
      case 0x01:
        RwStrAppendF (
          Out,
          "Memory24 (%a, 0x%04X, 0x%04X, 0x%04X, 0x%04X, )",
          (D[3] & 1) ? "ReadWrite" : "ReadOnly",
          ReadUnaligned16 ((UINT16 *)(D + 4)),
          ReadUnaligned16 ((UINT16 *)(D + 6)),
          ReadUnaligned16 ((UINT16 *)(D + 8)),
          ReadUnaligned16 ((UINT16 *)(D + 10))
          );
        break;
      case 0x02:
        {
          CONST CHAR8  *Space;

          Space = (D[3] < 0x0B) ? mRegionSpace[D[3]] : ((D[3] == 0x7F) ? "FFixedHW" : "0x??");
          RwStrAppendF (
            Out,
            "Register (%a, 0x%02X, 0x%02X, 0x%016lX, 0x%02X, )",
            Space,
            D[4],
            D[5],
            ReadUnaligned64 ((UINT64 *)(D + 7)),
            D[6]
            );
        }
        break;
      case 0x05:
        RwStrAppendF (
          Out,
          "Memory32 (%a, 0x%08X, 0x%08X, 0x%08X, 0x%08X, )",
          (D[3] & 1) ? "ReadWrite" : "ReadOnly",
          ReadUnaligned32 ((UINT32 *)(D + 4)),
          ReadUnaligned32 ((UINT32 *)(D + 8)),
          ReadUnaligned32 ((UINT32 *)(D + 12)),
          ReadUnaligned32 ((UINT32 *)(D + 16))
          );
        break;
      case 0x06:
        RwStrAppendF (
          Out,
          "Memory32Fixed (%a, 0x%08X, 0x%08X, )",
          (D[3] & 1) ? "ReadWrite" : "ReadOnly",
          ReadUnaligned32 ((UINT32 *)(D + 4)),
          ReadUnaligned32 ((UINT32 *)(D + 8))
          );
        break;
      case 0x07:
        RtAddress (Out, D, DLen, 4);
        break;
      case 0x08:
        RtAddress (Out, D, DLen, 2);
        break;
      case 0x0A:
        RtAddress (Out, D, DLen, 8);
        break;
      case 0x09:
        F = D[3];
        RwStrAppendF (
          Out,
          "Interrupt (%a, %a, %a, %a, ,, ) {",
          (F & 1) ? "ResourceConsumer" : "ResourceProducer",
          (F & 2) ? "Edge" : "Level",
          (F & 4) ? "ActiveLow" : "ActiveHigh",
          (F & 8) ? "Shared" : "Exclusive"
          );
        for (Index = 0; (Index < D[4]) && (5 + Index * 4 + 4 <= DLen); Index++) {
          RwStrAppendF (Out, "%a0x%08X", (Index > 0) ? ", " : "", ReadUnaligned32 ((UINT32 *)(D + 5 + Index * 4)));
        }

        RwStrAppend (Out, "}");
        break;
      case 0x0C:
        if (DLen >= 23) {
          UINT16  Flags;
          UINT16  PinOff;
          UINT16  SrcOff;

          Flags  = ReadUnaligned16 ((UINT16 *)(D + 7));
          PinOff = ReadUnaligned16 ((UINT16 *)(D + 14));
          SrcOff = ReadUnaligned16 ((UINT16 *)(D + 17));
          if (D[4] == 0) {
            CONST CHAR8  *Pol[] = { "ActiveHigh", "ActiveLow", "ActiveBoth", "ActiveHigh" };

            RwStrAppendF (
              Out,
              "GpioInt (%a, %a, %a, PullCfg 0x%02X, 0x%04X, ",
              (Flags & 1) ? "Edge" : "Level",
              Pol[(Flags >> 1) & 3],
              (Flags & 8) ? "Shared" : "Exclusive",
              D[9],
              ReadUnaligned16 ((UINT16 *)(D + 12))
              );
          } else {
            RwStrAppendF (Out, "GpioIo (%a, PullCfg 0x%02X, 0x%04X, ", (Flags & 8) ? "Shared" : "Exclusive", D[9], ReadUnaligned16 ((UINT16 *)(D + 12)));
          }

          RtResSource (Out, D, DLen, SrcOff);
          RwStrAppend (Out, ", ) {");
          for (Index = PinOff; (Index + 2 <= SrcOff) && (Index + 2 <= DLen); Index += 2) {
            RwStrAppendF (Out, "%a0x%04X", (Index > PinOff) ? ", " : "", ReadUnaligned16 ((UINT16 *)(D + Index)));
          }

          RwStrAppend (Out, "}");
        } else {
          RwStrAppend (Out, "/* GPIO descriptor (truncated) */");
        }

        break;
      case 0x0E:
        if ((DLen >= 12) && (D[5] == 1) && (DLen >= 18)) {
          RwStrAppendF (
            Out,
            "I2cSerialBusV2 (0x%04X, %a, 0x%08X, AddressingMode%dBit, ",
            ReadUnaligned16 ((UINT16 *)(D + 16)),
            (D[6] & 1) ? "DeviceInitiated" : "ControllerInitiated",
            ReadUnaligned32 ((UINT32 *)(D + 12)),
            (D[7] & 1) ? 10 : 7
            );
          RtResSource (Out, D, DLen, 18);
          RwStrAppend (Out, ", )");
        } else if ((DLen >= 21) && (D[5] == 2)) {
          RwStrAppendF (
            Out,
            "SpiSerialBusV2 (0x%04X, %a, %d, Mode%d, 0x%08X, ",
            ReadUnaligned16 ((UINT16 *)(D + 19)),
            (D[7] & 2) ? "PolarityHigh" : "PolarityLow",
            D[16],
            ((D[17] & 1) << 1) | (D[18] & 1),
            ReadUnaligned32 ((UINT32 *)(D + 12))
            );
          RtResSource (Out, D, DLen, 21);
          RwStrAppend (Out, ", )");
        } else if ((DLen >= 22) && (D[5] == 3)) {
          RwStrAppendF (Out, "UartSerialBusV2 (0x%08X, DataBits%d, ", ReadUnaligned32 ((UINT32 *)(D + 12)), 5 + ((D[7] >> 4) & 7));
          RtResSource (Out, D, DLen, 22);
          RwStrAppend (Out, ", )");
        } else {
          RwStrAppendF (Out, "/* SerialBus type %d, %d bytes */", (DLen > 5) ? D[5] : 0, DLen);
        }

        break;
      default:
        RwStrAppendF (Out, "/* large descriptor 0x%02X, %d bytes */", D[0], DLen);
        break;
    }
  }

  RwStrAppend (Out, "\n}");
}

//
// ---- Data objects ----------------------------------------------------------
//

STATIC
BOOLEAN
ParseBuffer (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8  *End,
  IN OUT RW_STR     *Out
  )
{
  UINT8   *PkgEnd;
  UINT64  Size;
  BOOLEAN IsConst;
  RW_STR      SizeStr;
  UINTN   Count;
  UINTN   Index;
  UINT8   *Bytes;

  Ctx->P++;
  if (!ReadPkgLength (Ctx, End, &PkgEnd)) {
    return FALSE;
  }

  RwStrInit (&SizeStr);
  IsConst = ReadConst (Ctx, PkgEnd, &Size);
  if (!IsConst && !ParseArg (Ctx, PkgEnd, &SizeStr, TRUE)) {
    RwStrFree (&SizeStr);
    return FALSE;
  }

  Bytes = Ctx->P;
  Count = (UINTN)(PkgEnd - Ctx->P);
  Ctx->P    = PkgEnd;
  if (Ctx->Silent) {
    RwStrFree (&SizeStr);
    return TRUE;
  }

  if (IsConst && (Size == Count) && (Count >= 2) && RtValid (Bytes, Count)) {
    RtDecode (Out, Bytes, Count);
    RwStrFree (&SizeStr);
    return TRUE;
  }

  if (Ctx->UuidHint && IsConst && (Size == 16) && (Count == 16)) {
    RwStrAppendF (
      Out,
      "ToUUID (\"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\")",
      ReadUnaligned32 ((UINT32 *)Bytes),
      ReadUnaligned16 ((UINT16 *)(Bytes + 4)),
      ReadUnaligned16 ((UINT16 *)(Bytes + 6)),
      Bytes[8], Bytes[9], Bytes[10], Bytes[11], Bytes[12], Bytes[13], Bytes[14], Bytes[15]
      );
    return TRUE;
  }

  if (IsConst) {
    RwStrAppendF (Out, "Buffer (0x%02lX)", Size);
  } else {
    RwStrAppend (Out, "Buffer (");
    RwStrAppend (Out, SizeStr.Buf);
    RwStrAppend (Out, ")");
  }

  RwStrFree (&SizeStr);
  if (Count == 0) {
    RwStrAppend (Out, " {}");
    return TRUE;
  }

  RwStrAppend (Out, "\n{");
  for (Index = 0; Index < Count; Index++) {
    if ((Index % 8) == 0) {
      RwStrAppendF (Out, "\n    /* %04X */  ", Index);
    }

    RwStrAppendF (Out, "0x%02X%a", Bytes[Index], (Index + 1 < Count) ? ", " : "");
  }

  RwStrAppend (Out, "\n}");
  return TRUE;
}

STATIC
BOOLEAN
ParsePackage (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8    *End,
  IN OUT RW_STR       *Out,
  IN     BOOLEAN  Var
  )
{
  UINT8   *PkgEnd;
  RW_STR      Elem;
  RW_STR      Num;
  BOOLEAN First;

  Ctx->P++;
  if (!ReadPkgLength (Ctx, End, &PkgEnd)) {
    return FALSE;
  }

  RwStrInit (&Num);
  if (Var) {
    if (!ParseArg (Ctx, PkgEnd, &Num, TRUE)) {
      RwStrFree (&Num);
      return FALSE;
    }
  } else {
    if (Ctx->P >= PkgEnd) {
      return FALSE;
    }

    RwStrAppendF (&Num, "0x%02X", *Ctx->P++);
  }

  RwStrAppend (Out, "Package (");
  RwStrAppend (Out, Num.Buf);
  RwStrAppend (Out, ")");
  RwStrFree (&Num);
  if (Ctx->P >= PkgEnd) {
    RwStrAppend (Out, " {}");
    Ctx->P = PkgEnd;
    return TRUE;
  }

  RwStrAppend (Out, "\n{");
  First = TRUE;
  while (Ctx->P < PkgEnd) {
    RwStrInit (&Elem);
    if (!ParseArg (Ctx, PkgEnd, &Elem, FALSE)) {
      RwStrFree (&Elem);
      RwStrAppend (Out, "\n    /* undecodable element */");
      break;
    }

    RwStrAppend (Out, First ? "\n    " : ",\n    ");
    SbPutIndented (Out, &Elem, 4);
    RwStrFree (&Elem);
    First = FALSE;
  }

  Ctx->P = PkgEnd;
  RwStrAppend (Out, "\n}");
  return TRUE;
}

STATIC
BOOLEAN
ParseString (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8  *End,
  IN OUT RW_STR     *Out
  )
{
  UINT8  *S;

  Ctx->P++;
  S = Ctx->P;
  while ((Ctx->P < End) && (*Ctx->P != 0)) {
    Ctx->P++;
  }

  if (Ctx->P >= End) {
    return FALSE;
  }

  RwStrAppend (Out, "\"");
  for ( ; S < Ctx->P; S++) {
    if ((*S == '"') || (*S == '\\')) {
      RwStrAppendChar (Out, '\\');
      RwStrAppendChar (Out, (CHAR8)*S);
    } else if ((*S >= 0x20) && (*S < 0x7F)) {
      RwStrAppendChar (Out, (CHAR8)*S);
    } else {
      RwStrAppendF (Out, "\\x%02X", *S);
    }
  }

  RwStrAppend (Out, "\"");
  Ctx->P++;
  return TRUE;
}

//
// ---- Expressions -----------------------------------------------------------
//

/**
  Generic operator: Name (args) by its argument pattern.
**/
STATIC
BOOLEAN
ParseGeneric (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8         *End,
  IN OUT RW_STR            *Out,
  IN     CONST AML_OP  *Op
  )
{
  CONST CHAR8  *A;
  RW_STR           Arg[8];
  BOOLEAN      Null[8];
  UINTN        N;
  UINTN        Last;
  UINTN        Index;
  BOOLEAN      Ok;
  CHAR8        Abs[RW_AML_MAX_PATH];
  UINTN        AbsLen;
  UINT64       Value;
  BOOLEAN      Uuid;

  for (Index = 0; Index < ARRAY_SIZE (Arg); Index++) {
    RwStrInit (&Arg[Index]);
  }

  N  = 0;
  Ok = TRUE;
  Uuid = (BOOLEAN)((AsciiStrCmp (Op->Name, "LEqual") == 0) || (AsciiStrCmp (Op->Name, "LNotEqual") == 0));
  for (A = Op->Args; *A != '\0' && N < 8; A++, N++) {
    Null[N]   = FALSE;
    Ctx->UuidHint = Uuid;
    switch (*A) {
      case 'a':
      case 's':
        Ok = ParseArg (Ctx, End, &Arg[N], TRUE);
        break;
      case 't':
        if ((Ctx->P < End) && (*Ctx->P == 0x00)) {
          Ctx->P++;
          Null[N] = TRUE;
        } else {
          Ok = ParseArg (Ctx, End, &Arg[N], TRUE);
        }

        break;
      case 'n':
        Ok = ParseNewName (Ctx, End, &Arg[N], Abs, &AbsLen);
        break;
      case 'b':
      case 'w':
      case 'd':
        Index = (*A == 'b') ? 1 : ((*A == 'w') ? 2 : 4);
        if (Ctx->P + Index > End) {
          Ok = FALSE;
          break;
        }

        Value = 0;
        CopyMem (&Value, Ctx->P, Index);
        Ctx->P += Index;
        RwStrAppendF (&Arg[N], "0x%0*lX", Index * 2, Value);
        break;
      default:
        Ok = FALSE;
        break;
    }

    Ctx->UuidHint = FALSE;
    if (!Ok) {
      N++;
      break;
    }
  }

  Ctx->UuidHint = FALSE;

  if (Ok) {
    RwStrAppend (Out, Op->Name);
    if (N > 0) {
      Last = N;
      while ((Last > 0) && Null[Last - 1]) {
        Last--;
      }

      RwStrAppend (Out, " (");
      for (Index = 0; Index < Last; Index++) {
        if (Index > 0) {
          RwStrAppend (Out, ", ");
        }

        SbPutIndented (Out, &Arg[Index], 0);
      }

      RwStrAppend (Out, ")");
    }
  }

  for (Index = 0; Index < N; Index++) {
    RwStrFree (&Arg[Index]);
  }

  return Ok;
}

STATIC
BOOLEAN
ParseArgInner (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8    *End,
  IN OUT RW_STR       *Out,
  IN     BOOLEAN  AllowInvoke
  )
{
  UINT8    Op;
  UINT8    Ext;
  UINTN    Index;
  UINT64   Value;
  CHAR8    Abs[RW_AML_MAX_PATH];
  UINTN    AbsLen;
  BOOLEAN  Single;
  INTN     Args;
  RW_STR       A[6];
  UINT8    M1;
  UINT8    M2;
  BOOLEAN  Ok;

  if (Ctx->P >= End) {
    return FALSE;
  }

  Op = *Ctx->P;
  switch (Op) {
    case 0x00:
      Ctx->P++;
      RwStrAppend (Out, "Zero");
      return TRUE;
    case 0x01:
      Ctx->P++;
      RwStrAppend (Out, "One");
      return TRUE;
    case 0xFF:
      Ctx->P++;
      RwStrAppend (Out, "Ones");
      return TRUE;
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0E:
      Index = (Op == 0x0A) ? 2 : ((Op == 0x0B) ? 4 : ((Op == 0x0C) ? 8 : 16));
      if (!ReadConst (Ctx, End, &Value)) {
        return FALSE;
      }

      RwStrAppendF (Out, "0x%0*lX", Index, Value);
      return TRUE;
    case 0x0D:
      return ParseString (Ctx, End, Out);
    case 0x11:
      return ParseBuffer (Ctx, End, Out);
    case 0x12:
      return ParsePackage (Ctx, End, Out, FALSE);
    case 0x13:
      return ParsePackage (Ctx, End, Out, TRUE);
    default:
      break;
  }

  if ((Op >= 0x60) && (Op <= 0x67)) {
    Ctx->P++;
    RwStrAppendF (Out, "Local%d", Op - 0x60);
    return TRUE;
  }

  if ((Op >= 0x68) && (Op <= 0x6E)) {
    Ctx->P++;
    RwStrAppendF (Out, "Arg%d", Op - 0x68);
    return TRUE;
  }

  if (IsNameStart (Op)) {
    if (!ParseName (Ctx, End, Out, Abs, &AbsLen, &Single)) {
      return FALSE;
    }

    if (!AllowInvoke) {
      return TRUE;
    }

    Args = LookupMethod (Ctx, Abs, AbsLen, Single);
    if (Args < 0) {
      return TRUE;
    }

    RwStrAppend (Out, " (");
    for (Index = 0; Index < (UINTN)Args; Index++) {
      RwStrInit (&A[0]);
      if (!ParseArg (Ctx, End, &A[0], TRUE)) {
        RwStrFree (&A[0]);
        return FALSE;
      }

      if (Index > 0) {
        RwStrAppend (Out, ", ");
      }

      SbPutIndented (Out, &A[0], 0);
      RwStrFree (&A[0]);
    }

    RwStrAppend (Out, ")");
    return TRUE;
  }

  if (Op == 0x5B) {
    if (Ctx->P + 1 >= End) {
      return FALSE;
    }

    Ext = Ctx->P[1];
    for (Index = 0; Index < ARRAY_SIZE (mExtOps); Index++) {
      if (mExtOps[Index].Op == Ext) {
        Ctx->P += 2;
        return ParseGeneric (Ctx, End, Out, &mExtOps[Index]);
      }
    }

    return FALSE;
  }

  if ((Op == 0x92) && (Ctx->P + 1 < End) && (Ctx->P[1] >= 0x93) && (Ctx->P[1] <= 0x95)) {
    CONST AML_OP  Combo[] = {
      { 0, "LNotEqual",     "aa" },
      { 0, "LLessEqual",    "aa" },
      { 0, "LGreaterEqual", "aa" }
    };

    Ext = Ctx->P[1];
    Ctx->P += 2;
    return ParseGeneric (Ctx, End, Out, &Combo[Ext - 0x93]);
  }

  if (Op == 0x89) {
    Ctx->P++;
    for (Index = 0; Index < ARRAY_SIZE (A); Index++) {
      RwStrInit (&A[Index]);
    }

    Ok = ParseArg (Ctx, End, &A[0], TRUE);
    if (Ok && (Ctx->P < End)) {
      M1 = *Ctx->P++;
      Ok = ParseArg (Ctx, End, &A[1], TRUE);
      if (Ok && (Ctx->P < End)) {
        M2 = *Ctx->P++;
        Ok = ParseArg (Ctx, End, &A[2], TRUE) && ParseArg (Ctx, End, &A[3], TRUE);
        if (Ok) {
          RwStrAppend (Out, "Match (");
          RwStrAppend (Out, A[0].Buf);
          RwStrAppend (Out, ", ");
          RwStrAppend (Out, (M1 < 6) ? mMatchOp[M1] : "M??");
          RwStrAppend (Out, ", ");
          RwStrAppend (Out, A[1].Buf);
          RwStrAppend (Out, ", ");
          RwStrAppend (Out, (M2 < 6) ? mMatchOp[M2] : "M??");
          RwStrAppend (Out, ", ");
          RwStrAppend (Out, A[2].Buf);
          RwStrAppend (Out, ", ");
          RwStrAppend (Out, A[3].Buf);
          RwStrAppend (Out, ")");
        }
      } else {
        Ok = FALSE;
      }
    } else {
      Ok = FALSE;
    }

    for (Index = 0; Index < 4; Index++) {
      RwStrFree (&A[Index]);
    }

    return Ok;
  }

  for (Index = 0; Index < ARRAY_SIZE (mOps); Index++) {
    if (mOps[Index].Op == Op) {
      Ctx->P++;
      return ParseGeneric (Ctx, End, Out, &mOps[Index]);
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
ParseArg (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8    *End,
  IN OUT RW_STR       *Out,
  IN     BOOLEAN  AllowInvoke
  )
{
  BOOLEAN  Ok;

  if (Ctx->Depth >= AML_MAX_DEPTH) {
    return FALSE;
  }

  Ctx->Depth++;
  if ((Ctx->P < End) && (*Ctx->P != 0x11)) {
    Ctx->UuidHint = FALSE;
  }

  Ok = ParseArgInner (Ctx, End, Out, AllowInvoke);
  Ctx->Depth--;
  return Ok;
}

//
// ---- Statements ------------------------------------------------------------
//

STATIC
BOOLEAN
ParseFieldList (
  IN OUT RW_AML_CTX  *Ctx,
  IN UINT8  *End,
  IN UINTN  Indent
  )
{
  UINT32  Bits;
  UINT32  Width;
  UINT8   Type;
  RW_STR      S;
  CHAR8   Abs[RW_AML_MAX_PATH];
  UINTN   AbsLen;
  BOOLEAN Single;

  Bits = 0;
  while (Ctx->P < End) {
    switch (*Ctx->P) {
      case 0x00:
        Ctx->P++;
        if (!ReadPkgValue (Ctx, End, &Width)) {
          return FALSE;
        }

        Bits += Width;
        if ((Bits % 8) == 0) {
          EmitFmt (Ctx, Indent, RwEmNormal, "Offset (0x%02X)%a", Bits / 8, (Ctx->P < End) ? "," : "");
        } else {
          EmitFmt (Ctx, Indent, RwEmNormal, "    , %d%a", Width, (Ctx->P < End) ? "," : "");
        }

        break;
      case 0x01:
        if (Ctx->P + 3 > End) {
          return FALSE;
        }

        Type = Ctx->P[1];
        EmitFmt (Ctx, Indent, RwEmNormal, "AccessAs (%a, 0x%02X),", mAccess[Type & 7], Ctx->P[2]);
        Ctx->P += 3;
        break;
      case 0x03:
        if (Ctx->P + 4 > End) {
          return FALSE;
        }

        EmitFmt (Ctx, Indent, RwEmNormal, "AccessAs (%a, 0x%02X, 0x%02X),", mAccess[Ctx->P[1] & 7], Ctx->P[2], Ctx->P[3]);
        Ctx->P += 4;
        break;
      case 0x02:
        Ctx->P++;
        RwStrInit (&S);
        if ((Ctx->P < End) && (*Ctx->P == 0x11)) {
          if (!ParseBuffer (Ctx, End, &S)) {
            RwStrFree (&S);
            return FALSE;
          }
        } else if (!ParseName (Ctx, End, &S, Abs, &AbsLen, &Single)) {
          RwStrFree (&S);
          return FALSE;
        }

        RwStrAppend (&S, "),");
        if (!Ctx->Silent) {
          RW_STR  Line;

          RwStrInit (&Line);
          RwStrAppend (&Line, "Connect (");
          SbPutIndented (&Line, &S, 0);
          Emit (Ctx, Indent, RwEmNormal, Line.Buf);
          RwStrFree (&Line);
        }

        RwStrFree (&S);
        break;
      default:
        if ((Ctx->P + 4 > End) || !IsLeadNameChar (*Ctx->P)) {
          return FALSE;
        }

        RwStrInit (&S);
        PutSeg (&S, Ctx->P);
        Ctx->P += 4;
        if (!ReadPkgValue (Ctx, End, &Width)) {
          RwStrFree (&S);
          return FALSE;
        }

        Bits += Width;
        RwStrAppend (&S, ",");
        EmitFmt (Ctx, Indent, RwEmNormal, "%-7a %d%a", S.Buf, Width, (Ctx->P < End) ? "," : "");
        RwStrFree (&S);
        break;
    }
  }

  return TRUE;
}

STATIC
CONST CHAR8 *
FieldFlags (
  IN  UINT8   Flags,
  OUT CHAR8  *Buf,
  IN  UINTN   Size
  )
{
  AsciiSPrint (
    Buf,
    Size,
    "%a, %a, %a",
    mAccess[Flags & 0x0F & 7],
    (Flags & 0x10) ? "Lock" : "NoLock",
    mUpdate[(Flags >> 5) & 3]
    );
  return Buf;
}

/**
  Enter a named scope: Abs becomes the current scope for the body.
**/
STATIC
BOOLEAN
Block (
  IN OUT RW_AML_CTX  *Ctx,
  IN UINT8        *PkgEnd,
  IN UINTN        Indent,
  IN CONST CHAR8  *Abs,
  IN UINTN        AbsLen
  )
{
  CHAR8    Saved[RW_AML_MAX_PATH];
  UINTN    SavedLen;
  BOOLEAN  Ok;

  SavedLen = Ctx->ScopeLen;
  CopyMem (Saved, Ctx->Scope, Ctx->ScopeLen);
  if (Abs != NULL) {
    CopyMem (Ctx->Scope, Abs, AbsLen);
    Ctx->ScopeLen = AbsLen;
  }

  Emit (Ctx, Indent, RwEmNormal, "{");
  Ok = ParseTermList (Ctx, PkgEnd, Indent + 1);
  Emit (Ctx, Indent, RwEmNormal, "}");
  CopyMem (Ctx->Scope, Saved, SavedLen);
  Ctx->ScopeLen = SavedLen;
  Ctx->P        = PkgEnd;
  return Ok;
}

STATIC
BOOLEAN
EisaId (
  IN  UINT32  V,
  OUT CHAR8  *Out
  )
{
  UINT16  Hi;
  UINTN   Index;
  CHAR8  C;

  Hi = (UINT16)(((V & 0xFF) << 8) | ((V >> 8) & 0xFF));
  for (Index = 0; Index < 3; Index++) {
    C = (CHAR8)(((Hi >> (10 - 5 * Index)) & 0x1F) + 0x40);
    if ((C < 'A') || (C > 'Z')) {
      return FALSE;
    }

    Out[Index] = C;
  }

  AsciiSPrint (Out + 3, 5 * sizeof (CHAR8), "%02X%02X", (V >> 16) & 0xFF, (V >> 24) & 0xFF);
  return TRUE;
}

STATIC
BOOLEAN
ParseStatement (
  IN OUT RW_AML_CTX  *Ctx,
  IN UINT8  *End,
  IN UINTN  Indent
  )
{
  UINT8    Op;
  UINT8    Ext;
  UINT8    *PkgEnd;
  RW_STR       S;
  RW_STR       V;
  RW_STR       Line;
  CHAR8    Abs[RW_AML_MAX_PATH];
  UINTN    AbsLen;
  CHAR8    Abs2[RW_AML_MAX_PATH];
  UINTN    AbsLen2;
  BOOLEAN  Single;
  BOOLEAN  Ok;
  UINT8    Flags;
  UINT64   Value;
  CHAR8   Buf[64];
  CHAR8   Id[9];
  UINT8    *Save;

  RwStrInit (&S);
  RwStrInit (&V);
  RwStrInit (&Line);
  Op  = *Ctx->P;
  Ext = (Ctx->P + 1 < End) ? Ctx->P[1] : 0;
  Ok  = TRUE;

  if ((Op == 0x10) || ((Op == 0x5B) && ((Ext == 0x82) || (Ext == 0x85) || (Ext == 0x83) || (Ext == 0x84)))) {
    //
    // Scope / Device / ThermalZone / Processor / PowerResource
    //
    Ctx->P += (Op == 0x10) ? 1 : 2;
    if (!ReadPkgLength (Ctx, End, &PkgEnd) || !ParseNewName (Ctx, PkgEnd, &S, Abs, &AbsLen)) {
      RwStrFree (&S);
      return FALSE;
    }

    if (Op == 0x10) {
      RwStrAppendF (&Line, "Scope (%a)", S.Buf);
    } else if (Ext == 0x82) {
      RwStrAppendF (&Line, "Device (%a)", S.Buf);
    } else if (Ext == 0x85) {
      RwStrAppendF (&Line, "ThermalZone (%a)", S.Buf);
    } else if (Ext == 0x83) {
      if (Ctx->P + 6 > PkgEnd) {
        Ok = FALSE;
      } else {
        RwStrAppendF (&Line, "Processor (%a, 0x%02X, 0x%08X, 0x%02X)", S.Buf, Ctx->P[0], ReadUnaligned32 ((UINT32 *)(Ctx->P + 1)), Ctx->P[5]);
        Ctx->P += 6;
      }
    } else {
      if (Ctx->P + 3 > PkgEnd) {
        Ok = FALSE;
      } else {
        RwStrAppendF (&Line, "PowerResource (%a, 0x%02X, 0x%04X)", S.Buf, Ctx->P[0], ReadUnaligned16 ((UINT16 *)(Ctx->P + 1)));
        Ctx->P += 3;
      }
    }

    if (Ok) {
      Emit (Ctx, Indent, RwEmStrong, Line.Buf);
      Ok = Block (Ctx, PkgEnd, Indent, Abs, AbsLen);
    }

    RwStrFree (&S);
    RwStrFree (&Line);
    Ctx->P = PkgEnd;
    return TRUE;
  }

  switch (Op) {
    case 0x14:
      Ctx->P++;
      if (!ReadPkgLength (Ctx, End, &PkgEnd) || !ParseNewName (Ctx, PkgEnd, &S, Abs, &AbsLen) || (Ctx->P >= PkgEnd)) {
        RwStrFree (&S);
        return FALSE;
      }

      Flags = *Ctx->P++;
      if (Ctx->Prepass) {
        AddMethod (Ctx, Abs, AbsLen, Flags & 7);
        RwStrFree (&S);
        Ctx->P = PkgEnd;
        return TRUE;
      }

      if ((Flags >> 4) != 0) {
        EmitFmt (Ctx, Indent, RwEmStrong, "Method (%a, %d, %a, %d)", S.Buf, Flags & 7, (Flags & 8) ? "Serialized" : "NotSerialized", Flags >> 4);
      } else {
        EmitFmt (Ctx, Indent, RwEmStrong, "Method (%a, %d, %a)", S.Buf, Flags & 7, (Flags & 8) ? "Serialized" : "NotSerialized");
      }

      RwStrFree (&S);
      Block (Ctx, PkgEnd, Indent, Abs, AbsLen);
      return TRUE;

    case 0xA0:
    case 0xA2:
      if ((Op == 0xA0) && (Ctx->P + 2 < End)) {
        UINT8  *Save3;
        UINT8  *IfEnd2;
        UINT8  *Q;

        Save3 = Ctx->P;
        Ctx->P++;
        if (ReadPkgLength (Ctx, End, &IfEnd2) && (Ctx->P < IfEnd2) && (*Ctx->P == 0x00) && (Ctx->P + 1 < IfEnd2) && (Ctx->P[1] == 0x15)) {
          Ctx->P++;
          Q = Ctx->P;
          while ((Ctx->P < IfEnd2) && (*Ctx->P == 0x15) && ParseStatement (Ctx, IfEnd2, Indent)) {
          }

          if (Ctx->P == IfEnd2) {
            return TRUE;
          }

          (VOID)Q;
        }

        Ctx->P = Save3;
      }

      Ctx->P++;
      if (!ReadPkgLength (Ctx, End, &PkgEnd) || !ParseArg (Ctx, PkgEnd, &S, TRUE)) {
        RwStrFree (&S);
        return FALSE;
      }

      RwStrAppend (&Line, (Op == 0xA2) ? "While (" : (Ctx->ElseIf ? "ElseIf (" : "If ("));
      Ctx->ElseIf = FALSE;
      SbPutIndented (&Line, &S, 0);
      RwStrAppend (&Line, ")");
      Emit (Ctx, Indent, RwEmNormal, Line.Buf);
      RwStrFree (&S);
      RwStrFree (&Line);
      Block (Ctx, PkgEnd, Indent, NULL, 0);
      return TRUE;

    case 0xA1:
      Ctx->P++;
      if (!ReadPkgLength (Ctx, End, &PkgEnd)) {
        return FALSE;
      }

      //
      // Else { If (...) {...} [Else {...}] } reads as ElseIf.
      //
      if ((Ctx->P < PkgEnd) && (*Ctx->P == 0xA0)) {
        UINT8  *Save2;
        UINT8  *IfEnd;
        UINT8  *ElseEnd;

        Save2 = Ctx->P;
        Ctx->P++;
        if (ReadPkgLength (Ctx, PkgEnd, &IfEnd)) {
          ElseEnd = IfEnd;
          if ((IfEnd < PkgEnd) && (*IfEnd == 0xA1)) {
            Ctx->P = IfEnd + 1;
            if (!ReadPkgLength (Ctx, PkgEnd, &ElseEnd)) {
              ElseEnd = NULL;
            }
          }

          if (ElseEnd == PkgEnd) {
            Ctx->P      = Save2;
            Ctx->ElseIf = TRUE;
            ParseTermList (Ctx, PkgEnd, Indent);
            Ctx->ElseIf = FALSE;
            Ctx->P      = PkgEnd;
            return TRUE;
          }
        }

        Ctx->P = Save2;
      }

      Emit (Ctx, Indent, RwEmNormal, "Else");
      Block (Ctx, PkgEnd, Indent, NULL, 0);
      return TRUE;

    case 0x08:
      Ctx->P++;
      if (!ParseNewName (Ctx, End, &S, Abs, &AbsLen)) {
        RwStrFree (&S);
        return FALSE;
      }

      if ((AbsLen >= 4) && (Ctx->P < End) && (*Ctx->P == 0x0C) &&
          ((CompareMem (Abs + AbsLen - 4, "_HID", 4) == 0) || (CompareMem (Abs + AbsLen - 4, "_CID", 4) == 0)))
      {
        Save = Ctx->P;
        if (ReadConst (Ctx, End, &Value) && EisaId ((UINT32)Value, Id)) {
          EmitFmt (Ctx, Indent, RwEmNormal, "Name (%a, EisaId (\"%a\"))  // 0x%08X", S.Buf, Id, (UINT32)Value);
          RwStrFree (&S);
          return TRUE;
        }

        Ctx->P = Save;
      }

      if (!ParseArg (Ctx, End, &V, FALSE)) {
        RwStrFree (&S);
        RwStrFree (&V);
        return FALSE;
      }

      RwStrAppendF (&Line, "Name (%a, ", S.Buf);
      SbPutIndented (&Line, &V, 0);
      RwStrAppend (&Line, ")");
      Emit (Ctx, Indent, RwEmNormal, Line.Buf);
      RwStrFree (&S);
      RwStrFree (&V);
      RwStrFree (&Line);
      return TRUE;

    case 0x15:
      Ctx->P++;
      if (!ParseNewName (Ctx, End, &S, Abs, &AbsLen) || (Ctx->P + 2 > End)) {
        RwStrFree (&S);
        return FALSE;
      }

      if (Ctx->P[0] == 8) {
        AddMethod (Ctx, Abs, AbsLen, Ctx->P[1] & 7);
      }

      if (Ctx->P[0] == 8) {
        EmitFmt (Ctx, Indent, RwEmDim, "External (%a, MethodObj)    // %d Arguments", S.Buf, Ctx->P[1] & 7);
      } else {
        EmitFmt (Ctx, Indent, RwEmDim, "External (%a, %a)", S.Buf, (Ctx->P[0] < 16) ? mObjType[Ctx->P[0]] : "UnknownObj");
      }

      Ctx->P += 2;
      RwStrFree (&S);
      return TRUE;

    default:
      break;
  }

  if (Op == 0x5B) {
    switch (Ext) {
      case 0x80:
        Ctx->P += 2;
        if (!ParseNewName (Ctx, End, &S, Abs, &AbsLen) || (Ctx->P >= End)) {
          RwStrFree (&S);
          return FALSE;
        }

        Flags = *Ctx->P++;
        if (Flags < ARRAY_SIZE (mRegionSpace)) {
          RwStrAppendF (&Line, "OperationRegion (%a, %a, ", S.Buf, mRegionSpace[Flags]);
        } else {
          RwStrAppendF (&Line, "OperationRegion (%a, 0x%02X, ", S.Buf, Flags);
        }

        Ok = ParseArg (Ctx, End, &V, TRUE);
        if (Ok) {
          SbPutIndented (&Line, &V, 0);
          RwStrFree (&V);
          RwStrAppend (&Line, ", ");
          Ok = ParseArg (Ctx, End, &V, TRUE);
          if (Ok) {
            SbPutIndented (&Line, &V, 0);
            RwStrAppend (&Line, ")");
            Emit (Ctx, Indent, RwEmNormal, Line.Buf);
          }
        }

        RwStrFree (&S);
        RwStrFree (&V);
        RwStrFree (&Line);
        return Ok;

      case 0x81:
      case 0x86:
      case 0x87:
        Ctx->P += 2;
        if (!ReadPkgLength (Ctx, End, &PkgEnd) || !ParseName (Ctx, PkgEnd, &S, Abs, &AbsLen, &Single)) {
          RwStrFree (&S);
          return FALSE;
        }

        if (Ext == 0x81) {
          RwStrAppendF (&Line, "Field (%a, ", S.Buf);
        } else {
          if (!ParseName (Ctx, PkgEnd, &V, Abs2, &AbsLen2, &Single)) {
            Ok = FALSE;
          } else {
            RwStrAppendF (&Line, (Ext == 0x86) ? "IndexField (%a, %a, " : "BankField (%a, %a, ", S.Buf, V.Buf);
            RwStrFree (&V);
            if (Ext == 0x87) {
              Ok = ParseArg (Ctx, PkgEnd, &V, TRUE);
              if (Ok) {
                SbPutIndented (&Line, &V, 0);
                RwStrAppend (&Line, ", ");
              }
            }
          }
        }

        if (Ok && (Ctx->P < PkgEnd)) {
          Flags = *Ctx->P++;
          RwStrAppendF (&Line, "%a)", FieldFlags (Flags, Buf, sizeof (Buf)));
          Emit (Ctx, Indent, RwEmNormal, Line.Buf);
          Emit (Ctx, Indent, RwEmNormal, "{");
          if (!ParseFieldList (Ctx, PkgEnd, Indent + 1)) {
            Emit (Ctx, Indent + 1, RwEmAlert, "/* undecodable field list */");
          }

          Emit (Ctx, Indent, RwEmNormal, "}");
        } else {
          Ok = FALSE;
        }

        RwStrFree (&S);
        RwStrFree (&V);
        RwStrFree (&Line);
        Ctx->P = PkgEnd;
        return Ok;

      default:
        break;
    }
  }

  //
  // Anything else is an expression used as a statement.
  //
  if (!ParseArg (Ctx, End, &S, TRUE)) {
    RwStrFree (&S);
    return FALSE;
  }

  Emit (Ctx, Indent, RwEmNormal, S.Buf);
  RwStrFree (&S);
  return TRUE;
}

STATIC
BOOLEAN
ParseTermList (
  IN OUT RW_AML_CTX  *Ctx,
  IN UINT8  *End,
  IN UINTN  Indent
  )
{
  UINT8  *Start;

  if (Ctx->Depth >= AML_MAX_DEPTH) {
    Ctx->P = End;
    return FALSE;
  }

  Ctx->Depth++;
  while (Ctx->P < End) {
    Start = Ctx->P;
    if (!ParseStatement (Ctx, End, Indent) || (Ctx->P <= Start)) {
      EmitFmt (
        Ctx,
        Indent,
        RwEmAlert,
        "/* cannot decode AML at offset 0x%X (opcode 0x%02X); %d byte(s) skipped */",
        (UINTN)(Start - Ctx->Base),
        *Start,
        (UINTN)(End - Start)
        );
      Ctx->P = End;
      Ctx->Depth--;
      return FALSE;
    }
  }

  Ctx->Depth--;
  return TRUE;
}
//
// ---- Public ----------------------------------------------------------------
//

VOID
RwAmlInit (
  OUT RW_AML_CTX  *Ctx
  )
{
  ZeroMem (Ctx, sizeof (*Ctx));
}

VOID
RwAmlFree (
  IN OUT RW_AML_CTX  *Ctx
  )
{
  if (Ctx->Methods != NULL) {
    FreePool (Ctx->Methods);
  }

  ZeroMem (Ctx, sizeof (*Ctx));
}

/**
  Reset the per-call parser state for one table.
**/
STATIC
VOID
ResetParser (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table
  )
{
  Ctx->Base     = Table;
  Ctx->P        = Table + 36;
  Ctx->ScopeLen = 0;
  Ctx->Depth    = 0;
  Ctx->ElseIf   = FALSE;
  Ctx->UuidHint = FALSE;
}

VOID
RwAmlPrepare (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       **Tables,
  IN     UINT32      *Lengths,
  IN     UINTN       Count
  )
{
  UINTN  Index;

  Ctx->MethodCount = 0;
  Ctx->Silent      = TRUE;
  Ctx->Prepass     = TRUE;
  Ctx->Out         = NULL;
  Ctx->Info        = NULL;
  for (Index = 0; Index < Count; Index++) {
    if ((Tables[Index] == NULL) || (Lengths[Index] <= 36)) {
      continue;
    }

    ResetParser (Ctx, Tables[Index]);
    ParseTermList (Ctx, Tables[Index] + Lengths[Index], 0);
  }

  Ctx->Silent   = FALSE;
  Ctx->Prepass  = FALSE;
  Ctx->Prepared = TRUE;
}

VOID
RwAmlPrepareTables (
  IN OUT RW_AML_CTX            *Ctx,
  IN     CONST RW_ACPI_TABLES  *Tables
  )
{
  UINT8   **Ptrs;
  UINT32  *Lens;
  UINTN   Count;
  UINTN   Index;

  Count = 0;
  Ptrs  = AllocatePool ((Tables->Count + 1) * sizeof (UINT8 *));
  Lens  = AllocatePool ((Tables->Count + 1) * sizeof (UINT32));
  if ((Ptrs != NULL) && (Lens != NULL)) {
    for (Index = 0; Index < Tables->Count; Index++) {
      if (RwAcpiIsAml (&Tables->Tables[Index])) {
        Ptrs[Count] = Tables->Tables[Index].Ptr;
        Lens[Count] = Tables->Tables[Index].Length;
        Count++;
      }
    }
  }

  RwAmlPrepare (Ctx, Ptrs, Lens, Count);
  if (Ptrs != NULL) {
    FreePool (Ptrs);
  }

  if (Lens != NULL) {
    FreePool (Lens);
  }
}

/**
  Header field as text: printable ASCII kept, control bytes as ' ', bytes
  above 0x7E as '?'.
**/
STATIC
VOID
HeaderText (
  OUT CHAR8        *Out,
  IN  CONST UINT8  *In,
  IN  UINTN        N
  )
{
  UINTN  Index;

  for (Index = 0; Index < N; Index++) {
    Out[Index] = (In[Index] < 0x20) ? ' ' : ((In[Index] > 0x7E) ? '?' : (CHAR8)In[Index]);
  }

  Out[N] = '\0';
}

/**
  Disassemble one DSDT/SSDT to whatever Ctx->Out / Ctx->Info point at.
**/
STATIC
EFI_STATUS
Disassemble (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table,
  IN     UINT32      Length
  )
{
  CHAR8  Sig[5];
  CHAR8  Oem[7];
  CHAR8  OemTab[9];

  Ctx->Silent  = FALSE;
  Ctx->Prepass = FALSE;
  if ((Table == NULL) || (Length < 36)) {
    EmitLine (Ctx, RwEmAlert, "Table too short.");
    return EFI_INVALID_PARAMETER;
  }

  HeaderText (Sig, Table, 4);
  HeaderText (Oem, Table + 10, 6);
  HeaderText (OemTab, Table + 16, 8);

  ResetParser (Ctx, Table);
  EmitLine (Ctx, RwEmDim, "/*");
  EmitFmt (Ctx, 0, RwEmDim, " * UefiShellGuiRW AML disassembly (ASL 1.0 syntax), %d bytes of AML", Length - 36);
  EmitLine (Ctx, RwEmDim, " * Method argument counts come from every DSDT/SSDT on this system.");
  EmitLine (Ctx, RwEmDim, " */");
  EmitFmt (
    Ctx,
    0,
    RwEmStrong,
    "DefinitionBlock (\"\", \"%a\", %d, \"%a\", \"%a\", 0x%08X)",
    Sig,
    Table[8],
    Oem,
    OemTab,
    ReadUnaligned32 ((UINT32 *)(Table + 24))
    );
  EmitLine (Ctx, RwEmNormal, "{");
  ParseTermList (Ctx, Table + Length, 1);
  EmitLine (Ctx, RwEmNormal, "}");
  return EFI_SUCCESS;
}

EFI_STATUS
RwAmlDisassemble (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table,
  IN     UINT32      Length,
  IN     BOOLEAN     Crlf,
  IN OUT RW_STR      *Out
  )
{
  EFI_STATUS  Status;

  Ctx->Out  = Out;
  Ctx->Info = NULL;
  Ctx->Crlf = Crlf;
  Status    = Disassemble (Ctx, Table, Length);
  Ctx->Out  = NULL;
  Ctx->Crlf = FALSE;
  if (!EFI_ERROR (Status) && Out->Oom) {
    Status = EFI_OUT_OF_RESOURCES;
  }

  return Status;
}

EFI_STATUS
RwAmlDisassembleInfo (
  IN OUT RW_AML_CTX  *Ctx,
  IN     UINT8       *Table,
  IN     UINT32      Length,
  IN OUT RW_INFO     *Info
  )
{
  EFI_STATUS  Status;

  Ctx->Out  = NULL;
  Ctx->Info = Info;
  Ctx->Crlf = FALSE;
  Status    = Disassemble (Ctx, Table, Length);
  Ctx->Info = NULL;
  if (!EFI_ERROR (Status) && Info->Oom) {
    Status = EFI_OUT_OF_RESOURCES;
  }

  return Status;
}
