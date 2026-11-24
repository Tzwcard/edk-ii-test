#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
for tool in gcc g++ make git python3 nasm iasl openssl sbsign sbverify; do
  command -v "$tool" >/dev/null || { echo "Missing: $tool. See README.md for apt dependencies." >&2; exit 1; }
done
git -C "$ROOT" -c core.autocrlf=false submodule update --init --depth 1 -- edk2
git -C "$ROOT/edk2" -c core.autocrlf=false submodule update --init --depth 1 \
  BaseTools/Source/C/BrotliCompress/brotli MdePkg/Library/MipiSysTLib/mipisyst \
  MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
  SecurityPkg/DeviceSecurity/SpdmLib/libspdm \
  CryptoPkg/Library/OpensslLib/openssl \
  CryptoPkg/Library/MbedTlsLib/mbedtls
make -C "$ROOT/edk2/BaseTools" -j "${JOBS:-8}"
source "$ROOT/scripts/env.sh"
echo 'Setup complete. Run: bash scripts/build.sh'
