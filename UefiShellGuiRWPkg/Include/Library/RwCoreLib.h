/** @file
  RwCoreLib: hardware access and decoders of UefiShellGuiRW.

  The library never draws and never uses LVGL; see RwCore/RwBase.h for the
  conventions every module follows.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RW_CORE_LIB_H_
#define RW_CORE_LIB_H_

#include <Library/RwCore/RwBase.h>
#include <Library/RwCore/RwSpace.h>
#include <Library/RwCore/RwPci.h>
#include <Library/RwCore/RwMsr.h>
#include <Library/RwCore/RwSio.h>
#include <Library/RwCore/RwSmbios.h>
#include <Library/RwCore/RwAcpi.h>
#include <Library/RwCore/RwUsb.h>
#include <Library/RwCore/RwUart.h>
#include <Library/RwCore/RwSmbus.h>
#include <Library/RwCore/RwStorage.h>
#include <Library/RwCore/RwEdid.h>
#include <Library/RwCore/RwTools.h>

#endif
