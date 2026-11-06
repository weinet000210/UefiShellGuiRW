## @file
#  Build description for UefiShellGuiRW.
#
#  Linux:   scripts/build.sh   (build -p UefiShellGuiRWPkg/UefiShellGuiRWPkg.dsc -a X64 -t GCC)
#  Windows: scripts\build.bat (build -p UefiShellGuiRWPkg\UefiShellGuiRWPkg.dsc -a X64 -t VS2022)
#
#  PACKAGES_PATH must contain edk2 and the repository root (which holds
#  UefiShellGuiRWPkg and the LVGLUefiGuiPkg submodule).
#
#  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]
  PLATFORM_NAME                  = UefiShellGuiRWPkg
  PLATFORM_GUID                  = 9A4D61E2-3B7C-4E58-8F12-C6B05D7A2E93
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010006
  OUTPUT_DIRECTORY               = Build/UefiShellGuiRWPkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses.common]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
!if $(TARGET) == RELEASE
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!else
  #
  # DEBUG() goes straight to the COM1 UART, never to ConOut: a GUI owns the
  # screen, and under the UEFI Shell a ConOut write blocks while the Shell's
  # output is paused (Ctrl+S).
  #
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  SerialPortLib|MdeModulePkg/Library/BaseSerialPortLib16550/BaseSerialPortLib16550.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  PciLib|MdePkg/Library/BasePciLibCf8/BasePciLibCf8.inf
  PciCf8Lib|MdePkg/Library/BasePciCf8Lib/BasePciCf8Lib.inf
!endif

  #
  # LVGLUefiGuiPkg library instances
  #
  UefiGuiRuntimeLib|LVGLUefiGuiPkg/Library/UefiGuiRuntimeLib/UefiGuiRuntimeLib.inf
  UefiGuiDisplayLib|LVGLUefiGuiPkg/Library/UefiGuiDisplayLib/UefiGuiDisplayLib.inf
  UefiGuiInputLib|LVGLUefiGuiPkg/Library/UefiGuiInputLib/UefiGuiInputLib.inf
  UefiGuiLib|LVGLUefiGuiPkg/Library/UefiGuiLib/UefiGuiLib.inf
  LvglLib|LVGLUefiGuiPkg/Library/LvglLib/LvglLib.inf
  CompilerIntrinsicsLib|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000042

[Components]
  UefiShellGuiRWPkg/Application/UefiShellGuiRW/UefiShellGuiRW.inf

  #
  # USB mouse driver that OVMF does not include, for QEMU runs with a mouse
  # (scripts/run-qemu.sh --load). Not part of the tool.
  #
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
