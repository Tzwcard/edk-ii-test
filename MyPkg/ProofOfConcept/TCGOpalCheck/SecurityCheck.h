/** @file
  Shared read-only check used by TCGOpalCheck and NVMeResetTest.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef STORAGE_SECURITY_CHECK_H_
#define STORAGE_SECURITY_CHECK_H_
#include <Uefi.h>
#include "AtaSecurity.h"
#include "Discovery.h"

typedef struct {
  BOOLEAN AtaValid;
  ATA_PASSWORD_STATUS Ata;
  BOOLEAN DiscoveryValid;
  OPAL_DISCOVERY_INFO Discovery;
} SECURITY_CHECK_RESULT;

// Valid flags are independent: EF may be valid even if OPAL discovery fails.
EFI_STATUS CheckStorageSecurity (IN EFI_HANDLE Handle, OUT SECURITY_CHECK_RESULT *Result);
#endif
