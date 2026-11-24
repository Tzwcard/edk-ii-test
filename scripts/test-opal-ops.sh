#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
gcc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fshort-wchar -DMDEPKG_NDEBUG -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
  -I "$ROOT/edk2/SecurityPkg/Include" -I "$ROOT/MyPkg/ProofOfConcept/OpalTest" \
  "$ROOT/tests/opal-ops-test.c" "$ROOT/MyPkg/ProofOfConcept/OpalTest/OpalOps.c" \
  "$ROOT/edk2/SecurityPkg/Library/TcgStorageCoreLib/TcgStorageCore.c" \
  "$ROOT/edk2/SecurityPkg/Library/TcgStorageCoreLib/TcgStorageUtil.c" \
  -o "$ROOT/out/tests/opal-ops-test"
"$ROOT/out/tests/opal-ops-test"
