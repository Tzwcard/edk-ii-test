/** @file
  GetVariable / SetVariable helpers, intentionally scoped to one test variable.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "NvVariable.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

CHAR16 gNvTestVariableName[] = L"TestVariable";
EFI_GUID gNvTestVendorGuid = {
  0xbd29e016, 0x764e, 0x4a58, { 0x9d, 0x53, 0xd6, 0x4f, 0xe9, 0x53, 0xed, 0xab }
};
CONST UINT8 gNvTestPayload[] = "UEFINVTest: Hello NVRAM! v1";
CONST UINTN gNvTestPayloadSize = sizeof (gNvTestPayload); // Includes ASCII NUL.

EFI_STATUS
NvTestRead (OUT UINT32 *Attributes, OUT UINTN *Size, OUT VOID **Data)
{
  EFI_STATUS Status;
  UINTN Attempt;
  UINTN Needed;
  UINTN Allocated;
  UINT32 Flags;
  VOID *Buffer;

  if ((Attributes == NULL) || (Size == NULL) || (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Attributes = 0;
  *Size = 0;
  *Data = NULL;
  // Bounded retries if another component grows the variable between calls.
  for (Attempt = 0; Attempt < 3; Attempt++) {
    Needed = 0;
    Flags = 0;
    Status = gRT->GetVariable (gNvTestVariableName, &gNvTestVendorGuid, &Flags, &Needed, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
    }

    if ((Needed == 0) || (Needed > NV_TEST_MAX_READ)) {
      return EFI_BAD_BUFFER_SIZE;
    }

    Allocated = Needed;
    Buffer = AllocatePool (Allocated);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gRT->GetVariable (gNvTestVariableName, &gNvTestVendorGuid, &Flags, &Needed, Buffer);
    if ((Status == EFI_SUCCESS) && (Needed > 0) && (Needed <= Allocated)) {
      *Attributes = Flags;
      *Size = Needed;
      *Data = Buffer;
      return EFI_SUCCESS;
    }

    FreePool (Buffer);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
    }
  }

  return EFI_ABORTED;
}

EFI_STATUS
NvTestWrite (IN BOOLEAN AllowOverwrite)
{
  EFI_STATUS Status;
  UINT32 Attributes;
  UINTN Size;
  VOID *Data;

  Status = NvTestRead (&Attributes, &Size, &Data);
  if (Status == EFI_SUCCESS) {
    FreePool (Data);
    if (!AllowOverwrite) {
      return EFI_ALREADY_STARTED;
    }

    if (Attributes != NV_TEST_ATTRIBUTES) {
      return EFI_ACCESS_DENIED;
    }
  } else if (Status != EFI_NOT_FOUND) {
    return Status;
  }

  return gRT->SetVariable (
                gNvTestVariableName, &gNvTestVendorGuid, NV_TEST_ATTRIBUTES,
                gNvTestPayloadSize, (VOID *)gNvTestPayload
                );
}

EFI_STATUS
NvTestVerify (VOID)
{
  EFI_STATUS Status;
  UINT32 Attributes;
  UINTN Size;
  VOID *Data;

  Status = NvTestRead (&Attributes, &Size, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Attributes != NV_TEST_ATTRIBUTES) || (Size != gNvTestPayloadSize) ||
      (CompareMem (Data, gNvTestPayload, Size) != 0))
  {
    Status = EFI_CRC_ERROR;
  }

  FreePool (Data);
  return Status;
}

EFI_STATUS
NvTestDelete (VOID)
{
  EFI_STATUS Status;
  UINT32 Attributes;
  UINTN Size;
  VOID *Data;

  Status = NvTestRead (&Attributes, &Size, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FreePool (Data);
  if (Attributes != NV_TEST_ATTRIBUTES) {
    return EFI_ACCESS_DENIED;
  }

  // For this non-authenticated variable, zero DataSize requests deletion.
  return gRT->SetVariable (gNvTestVariableName, &gNvTestVendorGuid, 0, 0, NULL);
}
