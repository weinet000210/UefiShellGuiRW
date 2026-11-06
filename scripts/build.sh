#!/usr/bin/env bash
# Build UefiShellGuiRW with the EDK2 GCC toolchain (Linux).
#
#   EDK2_DIR=/path/to/edk2 scripts/build.sh [DEBUG|RELEASE|NOOPT ...]
#
# Requirements: edk2-stable202608 with BaseTools built (make -C BaseTools),
# GCC 5+ targeting x86_64, NASM 2.15.05+, python3, and the submodules
# (git clone --recursive, or git submodule update --init --recursive).
# Optional env: GCC_BIN (tool prefix, e.g. x86_64-linux-gnu-),
#               WORKSPACE (build output root, default: <repo>/../Build-rw).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
: "${EDK2_DIR:?set EDK2_DIR to your edk2 checkout}"
EDK2_DIR="$(cd "$EDK2_DIR" && pwd)"
TARGETS=("$@")
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(DEBUG)

if [ ! -f "$REPO/LVGLUefiGuiPkg/Library/LvglLib/lvgl/lvgl.h" ]; then
  echo "LVGLUefiGuiPkg or LVGL is missing: run 'git submodule update --init --recursive'" >&2
  exit 1
fi

export WORKSPACE="${WORKSPACE:-$(dirname "$REPO")/Build-rw}"
mkdir -p "$WORKSPACE"
# The repository root holds both packages: UefiShellGuiRWPkg and the
# LVGLUefiGuiPkg submodule.
export PACKAGES_PATH="$EDK2_DIR:$REPO"
export EDK_TOOLS_PATH="$EDK2_DIR/BaseTools"

edk2_setup() {
  set +u
  . ./edksetup.sh > /dev/null
  local rc=$?
  set -u
  return $rc
}
cd "$EDK2_DIR"
edk2_setup
cd "$WORKSPACE"

for T in "${TARGETS[@]}"; do
  build -p UefiShellGuiRWPkg/UefiShellGuiRWPkg.dsc -a X64 -t GCC -b "$T" -n "$(nproc)"
  echo "Output: $WORKSPACE/Build/UefiShellGuiRWPkg/${T}_GCC/X64/UefiShellGuiRW.efi"
done
