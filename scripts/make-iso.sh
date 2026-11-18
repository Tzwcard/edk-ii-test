#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-DEBUG}"
case "$TARGET" in DEBUG|RELEASE) ;; *) echo 'Usage: bash scripts/make-iso.sh [DEBUG|RELEASE]' >&2; exit 2 ;; esac
for tool in xorriso mkfs.vfat mcopy mmd; do
  command -v "$tool" >/dev/null || { echo "Missing $tool; install xorriso dosfstools mtools." >&2; exit 1; }
done
bash "$ROOT/scripts/build.sh" "$TARGET"
source "$ROOT/scripts/env.sh"
if [[ ! -f "$ROOT/edk2/MdeModulePkg/Library/BrotliCustomDecompressLib/brotli/c/include/brotli/decode.h" ]]; then
  git -C "$ROOT/edk2" -c core.autocrlf=false submodule update --init --depth 1 \
    MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
fi
build -p MyPkg/Shell.dsc -a X64 -t GCC -b RELEASE -n "${JOBS:-8}"
STAGE="$ROOT/out/iso/$TARGET"
mkdir -p "$STAGE/EFI/BOOT" "$STAGE/boot"
bash "$ROOT/scripts/sign-efi.sh" \
  "$ROOT/Build/DevShell/RELEASE_GCC/X64/Shell.efi" "$ROOT/out/$TARGET/Shell.efi"
cp "$ROOT/out/$TARGET/Shell.efi" "$STAGE/EFI/BOOT/BOOTX64.EFI"
cp "$ROOT/out/$TARGET/HelloWorld.efi" "$STAGE/HelloWorld.efi"
cp "$ROOT/out/$TARGET/PCRRead.efi" "$STAGE/PCRRead.efi"
cp "$ROOT/out/$TARGET/TCGOpalCheck.efi" "$STAGE/TCGOpalCheck.efi"
cp "$ROOT/out/$TARGET/AtaPassUtil.efi" "$STAGE/AtaPassUtil.efi"
# Shell scripts on the FAT boot image use DOS line endings.
sed 's/\r$//; s/$/\r/' "$ROOT/iso/startup.nsh" > "$STAGE/startup.nsh"
IMAGE="$STAGE/boot/efiboot.img"
# This is a regular file, never a block device. No sudo or loop mounts needed.
truncate -s 0 "$IMAGE"
truncate -s 16M "$IMAGE"
mkfs.vfat -F 16 -n EDKII_SHELL "$IMAGE"
mmd -i "$IMAGE" ::/EFI ::/EFI/BOOT
mcopy -i "$IMAGE" "$STAGE/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMAGE" "$STAGE/HelloWorld.efi" "$STAGE/startup.nsh" ::/
mcopy -i "$IMAGE" "$STAGE/PCRRead.efi" ::/
mcopy -i "$IMAGE" "$STAGE/TCGOpalCheck.efi" ::/
mcopy -i "$IMAGE" "$STAGE/AtaPassUtil.efi" ::/
ISO="$ROOT/out/uefi-shell-x64-$TARGET.iso"
xorriso -as mkisofs -R -J -V EDKII_SHELL \
  -e boot/efiboot.img -no-emul-boot -o "$ISO" "$STAGE"
echo "Bootable UEFI-only ISO: $ISO"
echo 'Shell and all four applications are MOK-signed and signature-verified.'
echo 'MOK trust is not firmware db trust; direct firmware boot may still require db enrollment.'
