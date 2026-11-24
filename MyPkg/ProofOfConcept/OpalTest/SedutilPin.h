/** @file
  sedutil-compatible NVMe PIN derivation; no OPAL commands or authentication.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef OPAL_SEDUTIL_PIN_H_
#define OPAL_SEDUTIL_PIN_H_
#include <Uefi.h>

#define SEDUTIL_PASSWORD_MAX 256
#define SEDUTIL_SERIAL_SIZE  20
#define SEDUTIL_PIN_SIZE     32
#define SEDUTIL_ITERATIONS   75000

// Nonempty password bytes, excluding a C-string terminator. RawSerial must be
// the 20 original NVMe Identify bytes at offset 4, NOT trimmed/sanitized text.
// DerivedPin may alias Password; the caller owns/wipes its input and output.
EFI_STATUS
OpalSedutilDerivePin (
  IN CONST UINT8 *Password, IN UINTN PasswordSize,
  IN CONST UINT8 RawSerial[SEDUTIL_SERIAL_SIZE], OUT UINT8 DerivedPin[SEDUTIL_PIN_SIZE]
  );
#endif
