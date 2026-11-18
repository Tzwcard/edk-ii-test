#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-DEBUG}"
case "$TARGET" in DEBUG|RELEASE) ;; *) echo 'Usage: bash scripts/run-qemu.sh [DEBUG|RELEASE]' >&2; exit 2 ;; esac
EFI="$ROOT/out/$TARGET/HelloWorld.efi"
[[ -f "$EFI" ]] || { echo "Build $TARGET first." >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null
CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
[[ -f "$CODE" && -f "$VARS" ]] || { echo 'Install ovmf first.' >&2; exit 1; }
# Keep the test disk and NVRAM separate from the user's deployment directory.
RUNDIR="$ROOT/out/qemu/$TARGET"
mkdir -p "$RUNDIR/esp/EFI/BOOT"
cp "$EFI" "$RUNDIR/esp/EFI/BOOT/BOOTX64.EFI"
cp "$VARS" "$RUNDIR/OVMF_VARS.fd"
echo 'Starting an isolated QEMU VM (TCG, no network, no host disks, 30-second limit).'
set +e
timeout --signal=TERM --kill-after=5s 30s qemu-system-x86_64 \
  -machine q35 -accel tcg -m 256 -net none \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$CODE" \
  -drive "if=pflash,format=raw,unit=1,file=$RUNDIR/OVMF_VARS.fd" \
  -drive "format=raw,file=fat:rw:$RUNDIR/esp" \
  -display none -serial stdio -monitor none -no-reboot \
  2>&1 | tee "$RUNDIR/serial.log"
STATUS=${PIPESTATUS[0]}
set -e
if [[ "$STATUS" != 0 && "$STATUS" != 124 ]]; then
  echo "QEMU failed (exit $STATUS)." >&2
  exit "$STATUS"
fi
if grep -Fq 'UEFI application is running successfully (X64).' "$RUNDIR/serial.log"; then
  echo "PASS: $TARGET EFI application actually executed under OVMF."
  echo "Log: $RUNDIR/serial.log"
else
  echo 'FAIL: expected application output not found in serial.log.' >&2
  exit 1
fi
