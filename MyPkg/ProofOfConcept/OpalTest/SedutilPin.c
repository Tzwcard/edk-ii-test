/** @file
  Algorithm/parameters verified against Drive-Trust-Alliance/sedutil commit
  5af6caf6f73050dc689ade3cb63678d1bf81ea39, DtaHashPwd.cpp/.h and
  linux/DtaDevLinuxNvme.cpp. Uses EDK II crypto, not copied GPL code.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "SedutilPin.h"
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>

BOOLEAN
SanitizeDiskSerial(OUT UINT8 Serial[SEDUTIL_SERIAL_SIZE], IN CONST UINT8 RawSerial[SEDUTIL_SERIAL_SIZE])
{
  INTN Index = 0;

  CopyMem(Serial, RawSerial, SEDUTIL_SERIAL_SIZE);
  for (Index = SEDUTIL_SERIAL_SIZE - 1; Index >= 0; Index--) {
    if (Serial[Index] == 0x20) { Serial[Index] = 0x00; }
    else { break; }
  }

  return TRUE;
}

EFI_STATUS
OpalSedutilDerivePin (
  IN CONST UINT8 *Password, IN UINTN PasswordSize,
  IN CONST UINT8 RawSerial[SEDUTIL_SERIAL_SIZE], OUT UINT8 DerivedPin[SEDUTIL_PIN_SIZE]
  )
{
  UINT8 Result[SEDUTIL_PIN_SIZE] = { 0 };
  UINT8 Serial[SEDUTIL_PIN_SIZE] = { 0 };
  volatile UINT8 *Byte = Result;
  UINTN Index;
  BOOLEAN Success;

  if (Password == NULL || RawSerial == NULL || DerivedPin == NULL ||
      PasswordSize == 0 || PasswordSize > SEDUTIL_PASSWORD_MAX) { return EFI_INVALID_PARAMETER; }
  // sedutil's CLI input is a C string; do not silently accept embedded NULs.
  for (Index = 0; Index < PasswordSize; Index++) {
    if (Password[Index] == 0) { return EFI_INVALID_PARAMETER; }
  }
  SanitizeDiskSerial(Serial, RawSerial);
  Success = Pkcs5HashPassword (PasswordSize, (CONST CHAR8 *)Password, SEDUTIL_SERIAL_SIZE,
    Serial, SEDUTIL_ITERATIONS, SHA1_DIGEST_SIZE, sizeof (Result), Result);
  if (Success) { CopyMem (DerivedPin, Result, sizeof (Result)); }
  for (Index = 0; Index < sizeof (Result); Index++) { Byte[Index] = 0; }
  return Success ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
