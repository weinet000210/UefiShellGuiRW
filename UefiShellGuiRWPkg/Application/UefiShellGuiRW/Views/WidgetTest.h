/** @file
  Widget test window state, shared with the self test.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_WIDGET_TEST_H_
#define RW_WIDGET_TEST_H_

#include "../Widgets/RwWidgets.h"

#define RW_WIDGET_TEST_HOLE  0x30     // this byte of the pattern cannot be read

typedef struct {
  RW_MDI_WINDOW     *Window;
  RW_HEXGRID        *Grid;
  RW_BITFIELD       *Bits;
  RW_TABLE          *Table;
  RW_TREEVIEW       *Tree;
  RW_PROPTABLE      *Prop;
  RW_TEXTVIEW       *Text;
  RW_DATA_SOURCE    Pattern;          ///< 512 bytes, 10h-13h dangerous
  UINT8             *Bytes;           ///< the pattern itself
  UINTN             Writes;
  INT32             TableActivated;
  UINTN             TreeSelects;
  RW_DATA_SOURCE    HostBridge;       ///< config space of the first PCI device
  BOOLEAN           HaveHostBridge;
} RW_WIDGET_TEST;

RW_WIDGET_TEST *
RwWidgetTestGet (
  VOID
  );

#endif
