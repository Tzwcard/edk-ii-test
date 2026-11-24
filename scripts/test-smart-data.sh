#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/MyPkg/ProofOfConcept/SMARTDisp" \
  "$ROOT/tests/smart-data-test.c" "$ROOT/MyPkg/ProofOfConcept/SMARTDisp/SmartData.c" \
  -o "$ROOT/out/tests/smart-data-test"
"$ROOT/out/tests/smart-data-test"
# Differential checks against Python integers, including 128-bit multiplication.
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -shared -fPIC \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  "$ROOT/MyPkg/ProofOfConcept/SMARTDisp/SmartData.c" -o "$ROOT/out/tests/libsmart-data.so"
python3 "$ROOT/tests/smart-decimal-test.py"
