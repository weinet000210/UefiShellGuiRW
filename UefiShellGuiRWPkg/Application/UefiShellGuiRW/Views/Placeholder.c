/** @file
  Placeholder view for modules that later phases implement. It shows what
  the window will contain, so the frame can be tried out end to end.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "../UefiShellGuiRW.h"

STATIC
CONST CHAR8 *
Describe (
  IN RW_COMMAND_ID  Command
  )
{
  switch (Command) {
    case RwCmdPci:       return "PCI device list (Seg/Bus/Dev/Fn, vendor, device, class) and a 256 B / 4 KB config space editor with decoded registers and capabilities.";
    case RwCmdMemory:    return "Hex view and editor of any physical address, byte / word / dword access, go to address, auto refresh.";
    case RwCmdIo:        return "Hex view and editor of x86 I/O ports 0000h-FFFFh in byte / word / dword width.";
    case RwCmdIndexData: return "Index/Data register pairs (for example 70h/71h, 2Eh/2Fh, CF8h/CFCh) shown as a 256-entry table.";
    case RwCmdCmos:      return "CMOS / RTC RAM, 00h-7Fh through 70h/71h and 80h-FFh through 72h/73h.";
    case RwCmdMsr:       return "Model specific registers per CPU, with a 64-bit bit view and safe handling of unsupported MSRs.";
    case RwCmdEc:        return "Embedded Controller RAM 00h-FFh through the ACPI EC interface (62h/66h or any port pair).";
    case RwCmdSuperIo:   return "Super I/O chip detection at 2Eh/4Eh, logical devices with base addresses and IRQs, config register editor.";
    case RwCmdSmbus:     return "SMBus controller scan, device list and DDR SPD decode.";
    case RwCmdStorage:   return "ATA IDENTIFY and NVMe Identify Controller / Namespace data, decoded.";
    case RwCmdUsb:       return "USB tree: host controllers, hubs and devices with all descriptors decoded.";
    case RwCmdAcpi:      return "ACPI tables (RSDP, XSDT and every table) with decoders and an AML to ASL disassembler for DSDT/SSDT.";
    case RwCmdSmbios:    return "SMBIOS structures with decoded fields, strings and raw data.";
    case RwCmdPcieTree:  return "PCIe topology: root bridge, root ports, switches and endpoints with link speed and width.";
    case RwCmdEdid:      return "Monitor EDID read through the GOP EDID protocols, decoded.";
    case RwCmdComPort:   return "16550 UART detection and a terminal for loopback and serial testing.";
    default:             return "";
  }
}

RW_MDI_WINDOW *
RwViewOpenPlaceholder (
  IN RW_COMMAND_ID  Command
  )
{
  CONST RW_COMMAND  *Cmd;
  RW_MDI_WINDOW     *Window;
  lv_obj_t          *Obj;
  lv_obj_t          *Row;
  CHAR8             Text[64];

  Cmd    = RwGetCommand (Command);
  Window = RwMdiCreate (Cmd->Name, Command, 560, 300);
  if (Window == NULL) {
    return NULL;
  }

  lv_obj_set_flex_flow (Window->Content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all (Window->Content, 12, 0);
  lv_obj_set_style_pad_row (Window->Content, 10, 0);
  lv_obj_set_style_bg_color (Window->Content, lv_color_white (), 0);

  Row = lv_obj_create (Window->Content);
  lv_obj_remove_style_all (Row);
  lv_obj_set_size (Row, LV_PCT (100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (Row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column (Row, 10, 0);
  lv_obj_set_event_bubble (Row, TRUE);

  if (Cmd->Icon != NULL) {
    Obj = lv_image_create (Row);
    lv_image_set_src (Obj, Cmd->Icon);
    lv_image_set_scale (Obj, 512);
    lv_obj_set_size (Obj, 40, 40);
    lv_image_set_inner_align (Obj, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_event_bubble (Obj, TRUE);
  }

  Obj = lv_label_create (Row);
  lv_label_set_text (Obj, Cmd->Name);
  lv_obj_set_style_text_font (Obj, &lv_font_montserrat_20, 0);
  lv_obj_set_event_bubble (Obj, TRUE);

  Obj = lv_label_create (Window->Content);
  lv_label_set_text (Obj, Describe (Command));
  lv_label_set_long_mode (Obj, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width (Obj, LV_PCT (100));
  lv_obj_set_event_bubble (Obj, TRUE);

  Obj = lv_label_create (Window->Content);
  AsciiSPrint (Text, sizeof (Text), "Coming in %a.", (Cmd->Planned != NULL) ? Cmd->Planned : "a later phase");
  lv_label_set_text (Obj, Text);
  lv_obj_set_style_text_color (Obj, lv_color_hex (RW_COLOR_TEXT_GRAY), 0);
  lv_obj_set_event_bubble (Obj, TRUE);
  return Window;
}
