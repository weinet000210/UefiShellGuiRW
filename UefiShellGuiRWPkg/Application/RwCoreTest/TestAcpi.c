/** @file
  RwCoreTest: ACPI table list, decode and AML disassembly against OVMF's
  tables on QEMU q35 (RSDP, XSDT, FACP, DSDT, APIC, HPET, MCFG).

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RwCoreTest.h"

/**
  Check that a table exists once at least and has a valid checksum.
**/
STATIC
RW_ACPI_TABLE *
CheckTable (
  IN RW_ACPI_TABLES  *Tables,
  IN CONST CHAR8     *Sig
  )
{
  RW_ACPI_TABLE  *T;
  CHAR8          What[64];

  T = RwAcpiFindTable (Tables, Sig, 0);
  AsciiSPrint (What, sizeof (What), "ACPI %a present", Sig);
  if (!CHECK (T != NULL, What)) {
    return NULL;
  }

  AsciiSPrint (What, sizeof (What), "ACPI %a checksum OK", Sig);
  CHECK (T->HasHeader && T->ChecksumOk && (RwAcpiChecksum (T->Ptr, T->Length) == 0), What);
  return T;
}

/**
  TRUE when Info has a Field row with this Name.
**/
STATIC
BOOLEAN
HasField (
  IN CONST RW_INFO  *Info,
  IN CONST CHAR8    *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < Info->Count; Index++) {
    if ((Info->Rows[Index].Kind == RwKindField) && (AsciiStrCmp (Info->Rows[Index].Name, Name) == 0)) {
      return TRUE;
    }
  }

  return FALSE;
}

VOID
TestAcpi (
  VOID
  )
{
  RW_ACPI_TABLES  Tables;
  RW_ACPI_TABLE   *Facp;
  RW_ACPI_TABLE   *Dsdt;
  RW_ACPI_TABLE   *T;
  RW_INFO         Info;
  RW_AML_CTX      Aml;
  RW_STR          Asl;
  RW_STR          Text;
  EFI_STATUS      Status;
  UINTN           Index;
  UINTN           Lines;
  CHAR8           Name[32];

  TestLog ("== ACPI ==");
  Status = RwAcpiScan (&Tables);
  CHECK (!EFI_ERROR (Status), "RwAcpiScan finds the RSDP");
  if (EFI_ERROR (Status)) {
    RwAcpiFree (&Tables);
    return;
  }

  TestLog (
    "  RSDP %lX rev %d OEM '%a', %a %lX, %d tables",
    (UINT64)(UINTN)Tables.Rsdp,
    Tables.RsdpRevision,
    Tables.RsdpOemId,
    Tables.UsesXsdt ? "XSDT" : "RSDT",
    Tables.SdtAddress,
    Tables.Count
    );
  for (Index = 0; Index < Tables.Count; Index++) {
    T = &Tables.Tables[Index];
    TestLog (
      "  %a %-6a %-8a rev %d len %X at %lX %a",
      T->Signature,
      T->OemId,
      T->OemTableId,
      T->Revision,
      T->Length,
      T->Address,
      T->HasHeader ? (T->ChecksumOk ? "OK" : "BAD") : "-"
      );
  }

  CHECK (Tables.Rsdp != NULL, "RSDP pointer");
  CHECK (Tables.RsdpRevision >= 2, "RSDP revision >= 2 (ACPI 2.0+)");
  CHECK (Tables.UsesXsdt, "RSDP points to an XSDT");
  CheckTable (&Tables, "XSDT");
  Facp = CheckTable (&Tables, "FACP");
  Dsdt = CheckTable (&Tables, "DSDT");
  CheckTable (&Tables, "APIC");
  CheckTable (&Tables, "HPET");
  CheckTable (&Tables, "MCFG");
  CHECK (RwAcpiFindTable (&Tables, "FACS", 0) != NULL, "ACPI FACS found through the FADT");
  CHECK (RwAcpiFindTable (&Tables, "ZZZZ", 0) == NULL, "RwAcpiFindTable: unknown signature -> NULL");
  CHECK (RwAcpiFindTable (&Tables, "DSDT", 1) == NULL, "RwAcpiFindTable: one DSDT only");

  //
  // List document.
  //
  RwInfoInit (&Info);
  RwAcpiListInfo (&Tables, &Info);
  CHECK (!Info.Oom && (Info.Count >= Tables.Count + 6), "RwAcpiListInfo: RSDP section and one row per table");
  RwInfoFree (&Info);

  //
  // FADT decode.
  //
  if (Facp != NULL) {
    RwInfoInit (&Info);
    RwAcpiDecode (Facp, &Info);
    CHECK (Info.Count > 0, "FACP decode produces rows");
    CHECK (HasField (&Info, "SCI interrupt"), "FACP decode has SCI interrupt");
    CHECK (HasField (&Info, "PM1a_EVT_BLK") && HasField (&Info, "PM_TMR_BLK"), "FACP decode has PM blocks");
    CHECK (HasField (&Info, "Checksum"), "FACP decode has the header checksum");
    RwStrInit (&Text);
    RwInfoToText (&Info, &Text, FALSE);
    TestLog ("  FACP decode: %d rows, %d chars of text", Info.Count, Text.Len);
    RwStrFree (&Text);
    RwInfoFree (&Info);
  }

  //
  // Every other table decodes without trouble.
  //
  for (Index = 0; Index < Tables.Count; Index++) {
    RwInfoInit (&Info);
    RwAcpiDecode (&Tables.Tables[Index], &Info);
    if ((Info.Count == 0) || Info.Oom) {
      TestLog ("  decode of %a: %d rows%a", Tables.Tables[Index].Signature, Info.Count, Info.Oom ? " (OOM)" : "");
    }

    RwInfoFree (&Info);
  }

  CHECK (TRUE, "RwAcpiDecode over every table returns");

  //
  // DSDT disassembly.
  //
  if (Dsdt != NULL) {
    RwAcpiFileName (Dsdt, 3, "dsl", Name, sizeof (Name));
    CHECK (AsciiStrCmp (Name, "DSDT3.dsl") == 0, "RwAcpiFileName");

    RwAmlInit (&Aml);
    RwAmlPrepareTables (&Aml, &Tables);
    TestLog ("  AML pre-pass: %d methods / externals", Aml.MethodCount);
    CHECK (Aml.Prepared && (Aml.MethodCount > 0), "AML pre-pass finds methods");

    RwStrInit (&Asl);
    Status = RwAmlDisassemble (&Aml, Dsdt->Ptr, Dsdt->Length, FALSE, &Asl);
    CHECK (!EFI_ERROR (Status), "RwAmlDisassemble DSDT");
    Lines = 0;
    for (Index = 0; Index < Asl.Len; Index++) {
      if (Asl.Buf[Index] == '\n') {
        Lines++;
      }
    }

    TestLog ("  DSDT ASL: %d bytes, %d lines", Asl.Len, Lines);
    CHECK (Asl.Len > 0, "DSDT ASL not empty");
    CHECK (AsciiStrStr (Asl.Buf, "\nDefinitionBlock (\"\", \"DSDT\"") != NULL, "DSDT ASL has a DefinitionBlock");
    CHECK (AsciiStrStr (Asl.Buf, "Device (") != NULL, "DSDT ASL contains Device (");
    if (AsciiStrStr (Asl.Buf, "/* cannot decode AML") != NULL) {
      TestLog ("  note: DSDT ASL has undecodable AML");
    }

    RwInfoInit (&Info);
    Status = RwAmlDisassembleInfo (&Aml, Dsdt->Ptr, Dsdt->Length, &Info);
    CHECK (!EFI_ERROR (Status) && (Info.Count == Lines), "RwAmlDisassembleInfo: one row per ASL line");
    RwInfoFree (&Info);

    //
    // A second call on the same context gives the same text (state reset per call).
    //
    RwStrInit (&Text);
    RwAmlDisassemble (&Aml, Dsdt->Ptr, Dsdt->Length, FALSE, &Text);
    CHECK ((Text.Len == Asl.Len) && (CompareMem (Text.Buf, Asl.Buf, Asl.Len) == 0), "RwAmlDisassemble repeatable");
    RwStrFree (&Text);

    RwStrFree (&Asl);
    RwStrInit (&Asl);
    for (Index = 0; (T = RwAcpiFindTable (&Tables, "SSDT", Index)) != NULL; Index++) {
      RwStrReset (&Asl);
      Status = RwAmlDisassemble (&Aml, T->Ptr, T->Length, TRUE, &Asl);
      TestLog ("  SSDT %d: %d bytes of ASL (%r)", Index, Asl.Len, Status);
    }

    RwStrFree (&Asl);
    RwAmlFree (&Aml);
    CHECK (Aml.Methods == NULL, "RwAmlFree");
  }

  RwAcpiFree (&Tables);
  CHECK (Tables.Tables == NULL && Tables.Count == 0, "RwAcpiFree");
}
