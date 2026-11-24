/** @file
  One isolated UEFI NV variable for explicit read/write testing.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef NV_VARIABLE_TEST_H_
#define NV_VARIABLE_TEST_H_

#include <Uefi.h>

// | EFI_VARIABLE_RUNTIME_ACCESS)
#define NV_TEST_ATTRIBUTES  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS)
#define NV_TEST_MAX_READ    (64 * 1024)

extern CHAR16    gNvTestVariableName[];
extern EFI_GUID  gNvTestVendorGuid;
extern CONST UINT8 gNvTestPayload[];
extern CONST UINTN gNvTestPayloadSize;

// On success caller owns *Data and must FreePool it. No writes are performed.
EFI_STATUS NvTestRead (OUT UINT32 *Attributes, OUT UINTN *Size, OUT VOID **Data);
// FALSE refuses an existing variable. TRUE only permits replacing NV|BS|RT data.
EFI_STATUS NvTestWrite (IN BOOLEAN AllowOverwrite);
EFI_STATUS NvTestVerify (VOID);
// Deletes only this fixed name/GUID, and only with the expected attributes.
EFI_STATUS NvTestDelete (VOID);

#endif
