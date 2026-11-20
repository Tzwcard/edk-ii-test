/** @file
  Callable ATA Device Server Password Security (EFh) interfaces.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef ATA_PASS_UTIL_H_
#define ATA_PASS_UTIL_H_

#include <Uefi.h>
#include <Protocol/StorageSecurityCommand.h>
#include "../TCGOpalCheck/AtaSecurity.h"

#define ATA_PASS_PASSWORD_SIZE  32
#define ATA_PASS_CONTEXT_SIGNATURE  SIGNATURE_32 ('A', 'P', 'E', 'F')

typedef enum {
  AtaPassUser = 0,
  AtaPassMaster = 1
} ATA_PASS_AUTHORITY;

typedef enum {
  AtaPassHigh = 0,
  AtaPassMaximum = 1
} ATA_PASS_SECURITY_LEVEL;

typedef enum {
  AtaPassDoNotErase = 0,
  AtaPassConfirmPermanentDataLoss = 0x45524153
} ATA_PASS_ERASE_CONFIRMATION;

typedef struct {
  UINT32                                 Signature;
  EFI_HANDLE                             Handle;
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL  *Security;
  UINT32                                 MediaId;
  UINTN                                  Alignment;
  UINT64                                 Timeout100ns;
  BOOLEAN                                WritesEnabled;
  BOOLEAN                                ErasePrepared;
  BOOLEAN                                EnhancedEraseAvailable;
} ATA_PASS_DEVICE;

// Open checks the whole-device handle and protocol list. Writes start disabled.
EFI_STATUS AtaPassOpen (IN EFI_HANDLE Handle, OUT ATA_PASS_DEVICE *Device);
VOID AtaPassClose (IN OUT ATA_PASS_DEVICE *Device);
EFI_STATUS AtaPassGetStatus (IN OUT ATA_PASS_DEVICE *Device, OUT ATA_PASSWORD_STATUS *Status);

// An explicit caller opt-in, NOT a hardware authentication/authorization token.
EFI_STATUS AtaPassEnableWrites (IN OUT ATA_PASS_DEVICE *Device, IN BOOLEAN Enable);

// Passwords are exactly 32 raw bytes. No encoding, hashing, logging or retries.
EFI_STATUS AtaPassSetUserPassword (
  IN OUT ATA_PASS_DEVICE *Device, IN CONST UINT8 *Password32, IN ATA_PASS_SECURITY_LEVEL Level
  );
EFI_STATUS AtaPassSetMasterPassword (
  IN OUT ATA_PASS_DEVICE *Device, IN CONST UINT8 *Password32, IN UINT16 Identifier
  );
EFI_STATUS AtaPassUnlock (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority, IN CONST UINT8 *Password32
  );
EFI_STATUS AtaPassDisablePassword (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority, IN CONST UINT8 *Password32
  );
EFI_STATUS AtaPassFreeze (IN OUT ATA_PASS_DEVICE *Device);

// DANGER: erase destroys data. Neither function is called by default UefiMain.
// Call prepare, then unit immediately. Reads/other operations invalidate prepare.
EFI_STATUS AtaPassErasePrepare (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_ERASE_CONFIRMATION Confirmation
  );
EFI_STATUS AtaPassEraseUnit (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority,
  IN CONST UINT8 *Password32, IN BOOLEAN Enhanced, IN UINT64 Timeout100ns,
  IN ATA_PASS_ERASE_CONFIRMATION Confirmation
  );

// Use on caller-owned password buffers after use; the API wipes its own copies.
VOID AtaPassWipe (IN OUT VOID *Buffer, IN UINTN Size);

#endif
