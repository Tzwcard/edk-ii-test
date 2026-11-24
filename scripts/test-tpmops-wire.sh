#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/MyPkg/ProofOfConcept/TPMOps" \
  "$ROOT/tests/tpmops-wire-test.c" "$ROOT/MyPkg/ProofOfConcept/TPMOps/TpmSeal.c" \
  -o "$ROOT/out/tests/tpmops-wire-test"
"$ROOT/out/tests/tpmops-wire-test"
