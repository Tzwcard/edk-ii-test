#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/MyPkg/ProofOfConcept/TCGOpalCheck" \
  "$ROOT/tests/security-check-test.c" \
  "$ROOT/MyPkg/ProofOfConcept/TCGOpalCheck/SecurityCheck.c" \
  "$ROOT/MyPkg/ProofOfConcept/TCGOpalCheck/Discovery.c" \
  "$ROOT/MyPkg/ProofOfConcept/TCGOpalCheck/AtaSecurity.c" \
  -o "$ROOT/out/tests/security-check-test"
"$ROOT/out/tests/security-check-test"
