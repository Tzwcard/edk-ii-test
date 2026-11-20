/** @file
  Read-only SAT ATA Device Server Password Security status decoding.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef ATA_SECURITY_STATUS_H_
#define ATA_SECURITY_STATUS_H_

#include <Base.h>

typedef struct {
  UINT16  EraseTime;
  UINT16  EnhancedEraseTime;
  UINT16  MasterPasswordIdentifier;
  BOOLEAN MaximumSecurity;
  BOOLEAN Supported;
  BOOLEAN Enabled;
  BOOLEAN Locked;
  BOOLEAN Frozen;
  BOOLEAN AttemptsExceeded;
  BOOLEAN EnhancedEraseSupported;
} ATA_PASSWORD_STATUS;

BOOLEAN
ParseAtaPasswordStatus (
  IN CONST UINT8           *Buffer,
  IN UINTN                 BufferSize,
  OUT ATA_PASSWORD_STATUS  *Info
  );

#endif
