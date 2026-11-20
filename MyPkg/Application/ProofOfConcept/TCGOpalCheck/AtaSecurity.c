/** @file
  SAT-2 section 12.5.1: EFh / protocol-specific 0000h status response.
  All multi-byte response fields are big-endian, not ATA IDENTIFY words.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "AtaSecurity.h"

STATIC
UINT16
ReadBe16 (
  IN CONST UINT8  *Buffer
  )
{
  return (UINT16)(((UINT16)Buffer[0] << 8) | Buffer[1]);
}

BOOLEAN
ParseAtaPasswordStatus (
  IN CONST UINT8           *Buffer,
  IN UINTN                 BufferSize,
  OUT ATA_PASSWORD_STATUS  *Info
  )
{
  ATA_PASSWORD_STATUS  Parsed = { 0 };

  // The defined response is 16 bytes: a 2-byte length followed by 14 bytes.
  // Reject other layouts rather than interpreting an unknown extension.
  if ((Buffer == NULL) || (Info == NULL) || (BufferSize < 16) ||
      (ReadBe16 (Buffer) != 14))
  {
    return FALSE;
  }

  Parsed.EraseTime                 = ReadBe16 (Buffer + 2);
  Parsed.EnhancedEraseTime         = ReadBe16 (Buffer + 4);
  Parsed.MasterPasswordIdentifier  = ReadBe16 (Buffer + 6);
  Parsed.MaximumSecurity          = (Buffer[8] & BIT0) != 0;
  Parsed.Supported                = (Buffer[9] & BIT0) != 0;
  Parsed.Enabled                  = (Buffer[9] & BIT1) != 0;
  Parsed.Locked                   = (Buffer[9] & BIT2) != 0;
  Parsed.Frozen                   = (Buffer[9] & BIT3) != 0;
  Parsed.AttemptsExceeded         = (Buffer[9] & BIT4) != 0;
  Parsed.EnhancedEraseSupported   = (Buffer[9] & BIT5) != 0;
  *Info = Parsed;
  return TRUE;
}
