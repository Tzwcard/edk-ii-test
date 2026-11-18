#!/usr/bin/env bash
# Sign generated PE/COFF outputs. Never copy private keys into the workspace/ISO.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f "$ROOT/signing.local.sh" ]]; then
  source "$ROOT/signing.local.sh"
fi
KEY="${EFI_SIGN_KEY:-}"
CERT="${EFI_SIGN_CERT:-}"

if [[ $# == 1 && "${1:-}" == --cert-path ]]; then
  [[ -r "$CERT" ]] || { echo 'Configure EFI_SIGN_CERT or signing.local.sh first.' >&2; exit 1; }
  printf '%s\n' "$CERT"
  exit 0
fi
if [[ ! ( $# == 2 && "${1:-}" != --* ) && ! ( $# == 1 && "${1:-}" == --check ) ]]; then
  echo 'Usage: bash scripts/sign-efi.sh INPUT.efi OUTPUT.efi | --check | --cert-path' >&2
  exit 2
fi
for tool in openssl sbsign sbverify; do
  command -v "$tool" >/dev/null || { echo "Missing $tool. Install openssl sbsigntool." >&2; exit 1; }
done
[[ -r "$KEY" && -r "$CERT" ]] || {
  echo 'Signing key/certificate unavailable. Configure signing.local.sh or EFI_SIGN_KEY/EFI_SIGN_CERT.' >&2
  exit 1
}

# Compare public keys only. Never print or export the private key.
# The existing MOK.priv is unencrypted. Refuse unattended encrypted-key prompts.
CERT_PUBLIC="$(openssl x509 -in "$CERT" -pubkey -noout |
  openssl pkey -pubin -outform DER | openssl dgst -sha256)"
KEY_PUBLIC="$(openssl pkey -in "$KEY" -passin pass: -pubout -outform DER |
  openssl dgst -sha256)" || {
  echo 'Cannot read signing key (this signer expects the existing unencrypted MOK.priv).' >&2
  exit 1
}
[[ "$CERT_PUBLIC" == "$KEY_PUBLIC" ]] || { echo 'Signing certificate and key do not match.' >&2; exit 1; }
if [[ "${1:-}" == --check ]]; then
  echo 'EFI signing tools and MOK certificate/key pair are ready.'
  exit 0
fi

INPUT="$1"
OUTPUT="$2"
[[ -f "$INPUT" ]] || { echo "Input EFI not found: $INPUT" >&2; exit 1; }
[[ "$(realpath -m -- "$INPUT")" != "$(realpath -m -- "$OUTPUT")" ]] || {
  echo 'Input/output must differ; original build artifacts are kept unsigned.' >&2
  exit 1
}
mkdir -p "$(dirname -- "$OUTPUT")"
TEMP="$(mktemp "$(dirname -- "$OUTPUT")/.efi-signed.XXXXXX")"
trap 'rm -f -- "$TEMP"' EXIT
sbsign --key "$KEY" --cert "$CERT" --output "$TEMP" "$INPUT"
sbverify --cert "$CERT" "$TEMP"
mv -f -- "$TEMP" "$OUTPUT"
echo "Signed and verified: $OUTPUT"
