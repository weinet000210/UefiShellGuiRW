#!/bin/bash
# Start a software TPM 2.0 (swtpm) for one QEMU run and print the QEMU
# arguments that attach it as a TIS device. swtpm exits when QEMU closes.
#
# usage: TPM_ARGS=$(scripts/swtpm.sh [state dir])
set -e
DIR=${1:-$(mktemp -d /tmp/rwtpm.XXXXXX)}
mkdir -p "$DIR"
swtpm socket --tpm2 --tpmstate dir="$DIR" --ctrl type=unixio,path="$DIR/sock" \
  --flags startup-clear --daemon --terminate
for _ in $(seq 50); do
  [ -S "$DIR/sock" ] && break
  sleep 0.1
done
echo "-chardev socket,id=chrtpm,path=$DIR/sock -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0"
