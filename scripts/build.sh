#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-DEBUG}"
case "$TARGET" in DEBUG|RELEASE) ;; *) echo 'Usage: bash scripts/build.sh [DEBUG|RELEASE]' >&2; exit 2 ;; esac
source "$ROOT/scripts/env.sh"
bash "$ROOT/scripts/sign-efi.sh" --check
if [[ ! -x "$EDK_TOOLS_PATH/Source/C/bin/GenFw" ]]; then
  echo 'BaseTools is not built. Run: bash scripts/setup.sh' >&2
  exit 1
fi
build -p MyPkg/MyPkg.dsc -a X64 -t GCC -b "$TARGET" -n "${JOBS:-8}"
mkdir -p "$ROOT/out/$TARGET" "$ROOT/out/esp/EFI/BOOT"

for APP in HelloWorld PCRRead TCGOpalCheck AtaPassUtil SMARTDisp UEFINVTest TPMOps NVMeDisp NVMeResetTest TestMain OpalTest OpalPba; do
  bash "$ROOT/scripts/sign-efi.sh" \
    "$ROOT/Build/MyPkg/${TARGET}_GCC/X64/$APP.efi" "$ROOT/out/$TARGET/$APP.efi"
  echo "EFI executable: $ROOT/out/$TARGET/$APP.efi"
done
python3 "$ROOT/scripts/make-pba.py" "$TARGET"

cp "$ROOT/out/$TARGET/HelloWorld.efi" "$ROOT/out/esp/EFI/BOOT/BOOTX64.EFI"
echo "Bootable directory: $ROOT/out/esp (contains the most recently built target)"
