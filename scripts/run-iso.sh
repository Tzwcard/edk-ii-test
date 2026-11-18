#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-DEBUG}"
case "$TARGET" in DEBUG|RELEASE) ;; *) echo 'Usage: bash scripts/run-iso.sh [DEBUG|RELEASE]' >&2; exit 2 ;; esac
ISO="$ROOT/out/uefi-shell-x64-$TARGET.iso"
[[ -f "$ISO" ]] || { echo 'Build the ISO first with make-iso.cmd.' >&2; exit 1; }
RUNDIR="$ROOT/out/qemu-iso/$TARGET"
mkdir -p "$RUNDIR"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$RUNDIR/OVMF_VARS.fd"
echo 'Interactive UEFI shell. Press Ctrl+A, then X to quit QEMU.'
exec qemu-system-x86_64 -machine q35 -accel tcg -m 256 -net none \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive "if=pflash,format=raw,unit=1,file=$RUNDIR/OVMF_VARS.fd" \
  -cdrom "$ISO" -boot order=d -display none -serial mon:stdio -monitor none -no-reboot
