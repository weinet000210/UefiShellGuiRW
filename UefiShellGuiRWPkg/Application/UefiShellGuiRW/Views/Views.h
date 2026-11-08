/** @file
  Module views of Phase 3 (raw spaces) and the pieces they share.

  Each module has one window; its state is a static structure that the self
  test reads through the Rw<Module>ViewGet functions.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_VIEWS_H_
#define RW_VIEWS_H_

#include "../Widgets/RwWidgets.h"

//
// ---- Shared layout helpers -------------------------------------------------
//

/** A flex column; Width 0 takes the rest of the row. **/
lv_obj_t *
RwViewColumn (
  IN lv_obj_t  *Parent,
  IN INT32     Width
  );

/** A full-width box in a column; Grow 0 sizes to content. **/
lv_obj_t *
RwViewPane (
  IN lv_obj_t  *Parent,
  IN INT32     Grow
  );

/** One-line label (Montserrat), for headings above a grid or list. **/
lv_obj_t *
RwViewLabel (
  IN lv_obj_t     *Parent,
  IN CONST CHAR8  *Text
  );

//
// ---- Space pane: heading + hex grid + bit field view -------------------------
//
// The cursor value goes to the bit field view and the status bar; toggling a
// bit writes through the grid (so dangerous writes still ask).
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  lv_obj_t          *Column;
  lv_obj_t          *Heading;
  RW_HEXGRID        *Grid;
  RW_BITFIELD       *Bits;
  RW_DATA_SOURCE    *Source;
} RW_SPACE_PANE;

VOID
RwSpacePaneCreate (
  OUT RW_SPACE_PANE   *Pane,
  IN  lv_obj_t        *Parent,
  IN  RW_MDI_WINDOW   *Window,
  IN  RW_DATA_SOURCE  *Source
  );

/** Show another source (same grid), page 0. **/
VOID
RwSpacePaneSetSource (
  IN RW_SPACE_PANE   *Pane,
  IN RW_DATA_SOURCE  *Source
  );

/** Pixel size of the pane's grid and bit field view, for window fitting. **/
VOID
RwSpacePaneSize (
  IN  RW_SPACE_PANE  *Pane,
  OUT INT32          *Width,
  OUT INT32          *Height
  );

//
// ---- PCI -------------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_TABLE          *Devices;
  RW_SPACE_PANE     Pane;
  RW_PCI_LIST       List;
  RW_DATA_SOURCE    Source;
  UINTN             Current;
} RW_PCI_VIEW;

RW_MDI_WINDOW *
RwViewOpenPci (
  VOID
  );

RW_PCI_VIEW *
RwPciViewGet (
  VOID
  );

/**
  Open (or bring up) the PCI Devices window on the function at
  Segment:Bus:Dev.Func with the grid cursor on config Offset.
  FALSE when the function is not in the list.
**/
BOOLEAN
RwPciViewShow (
  IN UINT32  Segment,
  IN UINT8   Bus,
  IN UINT8   Dev,
  IN UINT8   Func,
  IN UINT32  Offset
  );

//
// ---- PCIe tree + device decode (Phase 4) -----------------------------------
//
typedef struct {
  RW_MDI_WINDOW    *Window;
  RW_TREEVIEW      *Tree;
  RW_PROPTABLE     *Decode;
  lv_obj_t         *Heading;
  RW_PCI_LIST      List;
  INT32            Current;          ///< device index shown in Decode, -1 = none
} RW_PCIE_TREE_VIEW;

RW_MDI_WINDOW *
RwViewOpenPcieTree (
  VOID
  );

RW_PCIE_TREE_VIEW *
RwPcieTreeViewGet (
  VOID
  );

//
// ---- ACPI tables (Phase 4) -------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_TABLE          *Tables;
  lv_obj_t          *Heading;
  lv_obj_t          *DecodePane;
  lv_obj_t          *AslPane;
  RW_PROPTABLE      *Decode;
  RW_TEXTVIEW       *Asl;
  RW_ACPI_TABLES    List;
  RW_AML_CTX        Aml;
  INT32             Current;         ///< table index, -1 = none
  BOOLEAN           ShowAsl;         ///< AML tables: ASL (TRUE) or header + hex (FALSE)
} RW_ACPI_VIEW;

RW_MDI_WINDOW *
RwViewOpenAcpi (
  VOID
  );

RW_ACPI_VIEW *
RwAcpiViewGet (
  VOID
  );

//
// ---- SMBIOS (Phase 4) ------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW      *Window;
  RW_TABLE           *Structs;
  RW_PROPTABLE       *Decode;
  lv_obj_t           *Heading;
  RW_SMBIOS_TABLE    Table;
  BOOLEAN            Loaded;
  INT32              Current;
} RW_SMBIOS_VIEW;

RW_MDI_WINDOW *
RwViewOpenSmbios (
  VOID
  );

RW_SMBIOS_VIEW *
RwSmbiosViewGet (
  VOID
  );

//
// ---- USB (Phase 4) ---------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_TREEVIEW       *Tree;
  RW_PROPTABLE      *Decode;
  lv_obj_t          *Heading;
  RW_USB_SCAN       Scan;
  RW_USB_HOTPLUG    Hotplug;
  INT32             CurrentTag;      ///< tag shown in Decode, -1 = none
} RW_USB_VIEW;

RW_MDI_WINDOW *
RwViewOpenUsb (
  VOID
  );

RW_USB_VIEW *
RwUsbViewGet (
  VOID
  );

/** Rescan (F5): keeps hot-plug marks between scans. **/
VOID
RwUsbViewRescan (
  VOID
  );

//
// ---- Memory ----------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_TABLE          *Map;
  RW_SPACE_PANE     Pane;
  RW_MEM_RANGE      *Ranges;
  UINTN             RangeCount;
  RW_DATA_SOURCE    Source;
} RW_MEM_VIEW;

RW_MDI_WINDOW *
RwViewOpenMemory (
  VOID
  );

RW_MEM_VIEW *
RwMemViewGet (
  VOID
  );

//
// ---- I/O space -------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_SPACE_PANE     Pane;
  RW_DATA_SOURCE    Source;
} RW_IO_VIEW;

RW_MDI_WINDOW *
RwViewOpenIo (
  VOID
  );

RW_IO_VIEW *
RwIoViewGet (
  VOID
  );

//
// ---- Index/Data ------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_TABLE          *Pairs;
  RW_SPACE_PANE     Pane;
  RW_DATA_SOURCE    Source;
  UINT16            IndexPort;
  UINT16            DataPort;
  UINT16            PendingIndex;     ///< custom pair: index port while the data port is asked
} RW_INDEX_DATA_VIEW;

RW_MDI_WINDOW *
RwViewOpenIndexData (
  VOID
  );

RW_INDEX_DATA_VIEW *
RwIndexDataViewGet (
  VOID
  );

/** Switch the window to another port pair (also used by the Custom row). **/
VOID
RwIndexDataViewSetPorts (
  IN UINT16  IndexPort,
  IN UINT16  DataPort
  );

//
// ---- CMOS ------------------------------------------------------------------
//
typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_SPACE_PANE     Pane;
  RW_PROPTABLE      *Rtc;
  RW_DATA_SOURCE    Source;
} RW_CMOS_VIEW;

RW_MDI_WINDOW *
RwViewOpenCmos (
  VOID
  );

RW_CMOS_VIEW *
RwCmosViewGet (
  VOID
  );

//
// ---- MSR -------------------------------------------------------------------
//
#define RW_MSR_VIEW_MAX  64

typedef struct {
  UINT32     Index;
  UINT64     Value;
  BOOLEAN    Valid;
} RW_MSR_ENTRY;

typedef struct {
  RW_MDI_WINDOW    *Window;
  lv_obj_t         *Cpu;
  RW_TABLE         *Table;
  RW_BITFIELD      *Bits;
  RW_MSR_ENTRY     Entries[RW_MSR_VIEW_MAX];
  UINTN            Count;
  BOOLEAN          Guard;
  UINT32           PendingIndex;      ///< MSR being written while the dialogs are open
  UINT64           PendingValue;
} RW_MSR_VIEW;

RW_MDI_WINDOW *
RwViewOpenMsr (
  VOID
  );

RW_MSR_VIEW *
RwMsrViewGet (
  VOID
  );

/** Re-read every listed MSR. **/
VOID
RwMsrViewReadAll (
  VOID
  );

/**
  Write an MSR the way the window does: a warning Yes / No first, then
  WRMSR and a re-read (asynchronous, the dialog decides).
**/
VOID
RwMsrViewWrite (
  IN UINT32  Index,
  IN UINT64  Value
  );

/** Add an MSR to the list and read it; the row index, or MAX_UINTN. **/
UINTN
RwMsrViewAdd (
  IN UINT32  Index
  );

#endif
