#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/MyPkg/Application/ProofOfConcept/AtaPassUtil" \
  "$ROOT/tests/ata-pass-api-test.c" \
  "$ROOT/MyPkg/Application/ProofOfConcept/AtaPassUtil/AtaPassUtil.c" \
  "$ROOT/MyPkg/Application/ProofOfConcept/TCGOpalCheck/AtaSecurity.c" \
  -o "$ROOT/out/tests/ata-pass-api-test"
"$ROOT/out/tests/ata-pass-api-test"
