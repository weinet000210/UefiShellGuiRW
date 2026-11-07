# RwCoreLib

`RwCoreLib` holds every hardware access and every decoder of UefiShellGuiRW.
The code comes from [uefirrw](https://github.com/weinet000210/uefirrw) v0.8,
where it was mixed into the text-mode screens; here it is UI free so the
LVGL views (P3 onwards), the dump commands (P6) and the `RwCoreTest`
application share it.

## Rules

- **No UI.** No LVGL, no ConOut / ConIn, no `Print`, no key handling. A
  function that needs a value from the user takes it as a parameter.
- **CHAR8 text.** Every string the library produces is ASCII (valid UTF-8, what
  LVGL draws). EDK2 print formats: `%a` is a CHAR8 string, `%s` a CHAR16 one;
  inside RwCoreLib use `%a`.
- **Structured output.** Decoders fill an `RW_INFO` (sections, `Name = Value`
  fields, free text rows) instead of text lines. A view shows it as a property
  table or a tree; `RwInfoToText` prints it for dumps. Rows record the
  register offset and size they decode when known (`RwInfoSetReg`), so a view
  can jump from a decoded field to the hex grid.
- **Emphasis instead of colors.** `RwEmStrong` (the value that matters: link
  status, enabled state), `RwEmAlert` (error bits, something unusual), `RwEmDim`
  (unused, not present, raw data).
- **DataSource.** Every editable space (PCI config, memory, I/O, CMOS, EC,
  Super I/O logical device) is an `RW_DATA_SOURCE`; the hex grid only talks to
  that interface. `IsDangerous` flags writes that may reset, hang or
  reconfigure the machine, and the GUI asks before those.
- **No hidden state.** Scans return results in caller-owned structures with a
  matching `Free`. Module globals only for caches that have an explicit
  release function.
- **Same behaviour as uefirrw.** The safety measures stay: MSR accesses only
  with the #GP guard installed, EC accesses time out, a Super I/O chip is
  locked again after every access.
- **Names.** Public functions `Rw<Module><Verb>`, types `RW_<MODULE>_<NAME>`,
  headers `Include/Library/RwCore/Rw<Module>.h` included by
  `Include/Library/RwCoreLib.h`.

## Modules

| Header | Source | Content |
|---|---|---|
| `RwBase.h` | `RwStr.c`, `RwInfo.c`, `RwDataSource.c` | strings, info documents, DataSource |
| `RwPci.h` | `RwPci.c`, `RwPciDecode.c` | config access, enumeration, tree, header/capability decode |
| `RwSpace.h` | `RwSpace.c` | memory, I/O, CMOS and EC spaces, danger rules |
| `RwMsr.h` | `RwMsr.c` | #GP-guarded MSR access, known MSR list, CPU brand |
| `RwSio.h` | `RwSio.c` | Super I/O probe, logical devices, config space source |
| `RwSmbios.h` | `RwSmbios.c` | SMBIOS table walk and structure decode |
| `RwAcpi.h` | `RwAcpi.c`, `RwAml.c` | ACPI table list and decode, AML to ASL disassembler |
| `RwUsb.h` | `RwUsb.c` | USB controllers, device tree, descriptors |
| `RwUart.h` | `RwUart.c` | 16550 detection, setup, send / receive, loopback test |

## Tests

`UefiShellGuiRWPkg/Application/RwCoreTest` runs in QEMU (q35 + OVMF) and checks
each module against hardware the machine is known to have: the Q35 host bridge
8086:29C0, the ICH9 LPC bridge 8086:2918, OVMF's SMBIOS and ACPI tables, the
RTC in CMOS, IA32_APIC_BASE, COM1. CI runs it on every push.
