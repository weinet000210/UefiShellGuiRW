# Module windows

Each module has one window (running its command again brings it to the
front). Windows open at the size their content needs; Window > Cascade
keeps that size. `F5` re-reads the active window. Writes follow the hex
grid rules in [Widgets.md](Widgets.md): typing writes, dangerous writes ask
first with No as the default.

Raw-space windows share `Views/SpaceView.c`: a heading with the space name
and range, the hex grid, and the bit field view of the value under the
cursor (click a bit to toggle it, written through the grid). The status
bar shows the cursor address and value.

## Phase 3

| Window | File | Left / right | Notes |
|---|---|---|---|
| PCI Devices | `Views/PciView.c` | Every function (bridges in blue) / config space | 256 bytes, or 4 KB when extended config space is reachable (`PgDn` for the next pages). BARs, the ROM BAR, bridge bus numbers and windows are dangerous. `Enter` in the list moves to the grid. |
| Memory | `Views/MemoryView.c` | UEFI memory map, adjacent ranges merged / physical memory and MMIO | Opens at 0. `Enter` on a map row jumps there, `Ctrl+G` goes anywhere. ACPI reclaim / NVS (red in the map) and FF000000h-FFFFFFFFh are dangerous. Auto refresh is on. |
| I/O Space | `Views/IoView.c` | Ports 0000h-FFFFh | Reads can have side effects, so no auto refresh. CF9h, 92h and 64h are dangerous. |
| Index/Data | `Views/IndexDataView.c` | Port pairs / registers 00h-FFh | 70h/71h, 72h/73h, 2Eh/2Fh, 4Eh/4Fh, C00h/C01h, CD6h/CD7h and Custom (asks for both ports). Super I/O pairs read FFh until the chip is in config mode (Phase 5 does that). |
| CMOS | `Views/CmosView.c` | CMOS RAM 00h-FFh / RTC decode | 00h-7Fh through 70h/71h, 80h-FFh through 72h/73h, byte access. The decode shows time, date, day, alarm, registers A, B, D and the century byte; register C is not read (reading clears it). |
| CPU MSR | `Views/MsrView.c` | Known MSRs of the boot processor | `Enter` writes a new value, `Ins` adds any MSR by index, the 64-bit bit view toggles bits. Every MSR write asks first. A #GP guard shows unsupported MSRs as `#GP` instead of hanging. |

## Tests

`SelfTestViews.c` opens all six windows through their commands and drives
them with keys: device selection, BAR write cancelled, memory map jump,
`Ctrl+G` and typed writes into a page the test allocated, CF9h write
cancelled, custom Index/Data pair, CMOS writes to spare bytes 38h/39h,
adding MSRs and writing IA32_PAT back with its own value.

![Phase 3 windows in QEMU at 1024x768](Images/phase3-views.png)
