#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/MyPkg/ProofOfConcept/UEFINVTest" \
  "$ROOT/tests/nv-variable-test.c" "$ROOT/MyPkg/ProofOfConcept/UEFINVTest/NvVariable.c" \
  -o "$ROOT/out/tests/nv-variable-test"
"$ROOT/out/tests/nv-variable-test"
