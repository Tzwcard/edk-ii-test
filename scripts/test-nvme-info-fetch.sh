#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out/tests"
for OPT in 0 2; do
  # Also compile the production file independently (test includes cannot mask
  # a missing production header or declaration).
  gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar "-O$OPT" \
    -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
    -c "$ROOT/MyPkg/Application/NVMe/InfoFetch.c" \
    -o "$ROOT/out/tests/nvme-info-fetch-O$OPT.o"
  gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar -g "-O$OPT" \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$ROOT/edk2/MdePkg/Include" -I "$ROOT/edk2/MdePkg/Include/X64" \
    "$ROOT/tests/nvme-info-fetch-test.c" "$ROOT/MyPkg/Application/Util/ValueAccess.c" \
    -o "$ROOT/out/tests/nvme-info-fetch-test-O$OPT"
  "$ROOT/out/tests/nvme-info-fetch-test-O$OPT"
done
