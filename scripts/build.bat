@echo off
rem Build UefiShellGuiRW with Visual Studio 2022 (Windows).
rem
rem   set EDK2_DIR=C:\src\edk2
rem   scripts\build.bat [DEBUG|RELEASE|NOOPT]
rem
rem Run after the edk2 BaseTools are built; edksetup.bat locates VS2022.
rem NASM must be installed and NASM_PREFIX set (for example
rem C:\Program Files\NASM\). Clone with --recursive so the LVGLUefiGuiPkg
rem and LVGL submodules are present.
setlocal
if "%EDK2_DIR%"=="" (
  echo Set EDK2_DIR to your edk2 checkout.
  exit /b 1
)
set TARGET=%1
if "%TARGET%"=="" set TARGET=DEBUG

for %%I in ("%~dp0..") do set REPO=%%~fI
for %%I in ("%REPO%\..") do set PARENT=%%~fI
if not exist "%REPO%\LVGLUefiGuiPkg\Library\LvglLib\lvgl\lvgl.h" (
  echo LVGLUefiGuiPkg or LVGL is missing: run git submodule update --init --recursive
  exit /b 1
)

if "%WORKSPACE%"=="" set WORKSPACE=%PARENT%\Build-rw
if not exist "%WORKSPACE%" mkdir "%WORKSPACE%"
set PACKAGES_PATH=%EDK2_DIR%;%REPO%
set EDK_TOOLS_PATH=%EDK2_DIR%\BaseTools

pushd "%EDK2_DIR%"
call edksetup.bat VS2022
if errorlevel 1 exit /b 1
popd

pushd "%WORKSPACE%"
call build -p UefiShellGuiRWPkg\UefiShellGuiRWPkg.dsc -a X64 -t VS2022 -b %TARGET%
set RC=%ERRORLEVEL%
popd
if %RC% neq 0 exit /b %RC%
echo Output: %WORKSPACE%\Build\UefiShellGuiRWPkg\%TARGET%_VS2022\X64\UefiShellGuiRW.efi
