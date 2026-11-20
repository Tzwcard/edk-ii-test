/* Mock runtime services only; never accesses host firmware variables. */
#include "NvVariable.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EFI_RUNTIME_SERVICES Runtime;
EFI_RUNTIME_SERVICES *gRT = &Runtime;
static UINT8 Stored[128];
static UINTN StoredSize;
static UINT32 StoredAttributes;
static BOOLEAN Exists;
static BOOLEAN AllocationFails;
static BOOLEAN Growing;
static BOOLEAN Oversized;
static BOOLEAN ZeroSuccess;
static EFI_STATUS GetResult;
static EFI_STATUS SetResult;
static unsigned Writes;
static unsigned Allocations;
static unsigned Frees;

VOID *EFIAPI AllocatePool (UINTN Size)
{
  if (AllocationFails) { return NULL; }
  Allocations++;
  return malloc (Size);
}
VOID EFIAPI FreePool (VOID *Data) { assert (Data != NULL); Frees++; free (Data); }
INTN EFIAPI CompareMem (CONST VOID *Left, CONST VOID *Right, UINTN Size) { return memcmp (Left, Right, Size); }

static EFI_STATUS EFIAPI
GetVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attributes, UINTN *Size, VOID *Data)
{
  assert (Name == gNvTestVariableName && Guid == &gNvTestVendorGuid);
  if (GetResult != EFI_SUCCESS) { return GetResult; }
  if (!Exists) { return EFI_NOT_FOUND; }
  *Attributes = StoredAttributes;
  if (Oversized) { *Size = NV_TEST_MAX_READ + 1; return EFI_BUFFER_TOO_SMALL; }
  if (ZeroSuccess) { *Size = 0; return EFI_SUCCESS; }
  if (Growing) {
    *Size = Data == NULL ? 4 : 8;
    return EFI_BUFFER_TOO_SMALL;
  }
  if (*Size < StoredSize || Data == NULL) {
    *Size = StoredSize;
    return EFI_BUFFER_TOO_SMALL;
  }
  memcpy (Data, Stored, StoredSize);
  *Size = StoredSize;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
SetVariable (CHAR16 *Name, EFI_GUID *Guid, UINT32 Attributes, UINTN Size, VOID *Data)
{
  assert (Name == gNvTestVariableName && Guid == &gNvTestVendorGuid);
  Writes++;
  if (SetResult != EFI_SUCCESS) { return SetResult; }
  if (Size == 0) {
    assert (Attributes == 0 && Data == NULL);
    Exists = FALSE;
    return EFI_SUCCESS;
  }
  assert (Attributes == NV_TEST_ATTRIBUTES && Size == gNvTestPayloadSize);
  assert (memcmp (Data, gNvTestPayload, Size) == 0);
  StoredSize = Size;
  StoredAttributes = Attributes;
  memcpy (Stored, Data, Size);
  Exists = TRUE;
  return EFI_SUCCESS;
}

int main (void)
{
  EFI_STATUS Status;
  UINT32 Attributes;
  UINTN Size;
  VOID *Data;
  unsigned Before;

  Runtime.GetVariable = GetVariable;
  Runtime.SetVariable = SetVariable;
  assert (NV_TEST_ATTRIBUTES == 7);
  assert (gNvTestVendorGuid.Data1 == 0xbd29e016 && gNvTestVendorGuid.Data4[7] == 0xab);
  assert (NvTestRead (NULL, &Size, &Data) == EFI_INVALID_PARAMETER);
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_NOT_FOUND && Data == NULL && Size == 0);
  assert (Writes == 0);
  assert (NvTestWrite (FALSE) == EFI_SUCCESS && Writes == 1);
  assert (Exists && StoredAttributes == 7 && StoredSize == gNvTestPayloadSize);
  assert (NvTestVerify () == EFI_SUCCESS && Writes == 1);
  assert (NvTestWrite (FALSE) == EFI_ALREADY_STARTED && Writes == 1);
  assert (NvTestWrite (TRUE) == EFI_SUCCESS && Writes == 2);

  Stored[0] ^= 1;
  assert (NvTestVerify () == EFI_CRC_ERROR);
  assert (NvTestWrite (TRUE) == EFI_SUCCESS);
  Before = Writes;
  StoredAttributes = EFI_VARIABLE_BOOTSERVICE_ACCESS;
  assert (NvTestWrite (TRUE) == EFI_ACCESS_DENIED && Writes == Before);
  assert (NvTestDelete () == EFI_ACCESS_DENIED && Writes == Before);
  StoredAttributes = 7;

  AllocationFails = TRUE;
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_OUT_OF_RESOURCES && Data == NULL);
  AllocationFails = FALSE;
  Oversized = TRUE;
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_BAD_BUFFER_SIZE && Data == NULL);
  Oversized = FALSE;
  ZeroSuccess = TRUE;
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_COMPROMISED_DATA && Data == NULL);
  ZeroSuccess = FALSE;
  Growing = TRUE;
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_ABORTED && Data == NULL);
  Growing = FALSE;
  GetResult = EFI_SECURITY_VIOLATION;
  assert (NvTestWrite (TRUE) == EFI_SECURITY_VIOLATION && Writes == Before);
  GetResult = EFI_WARN_UNKNOWN_GLYPH;
  assert (NvTestRead (&Attributes, &Size, &Data) == EFI_COMPROMISED_DATA && Data == NULL);
  GetResult = EFI_SUCCESS;
  SetResult = EFI_WRITE_PROTECTED;
  assert (NvTestWrite (TRUE) == EFI_WRITE_PROTECTED && Exists);
  SetResult = EFI_SUCCESS;

  Status = NvTestRead (&Attributes, &Size, &Data);
  assert (Status == EFI_SUCCESS && Attributes == 7 && Size == gNvTestPayloadSize);
  FreePool (Data);
  assert (NvTestDelete () == EFI_SUCCESS && !Exists);
  Before = Writes;
  assert (NvTestDelete () == EFI_NOT_FOUND && Writes == Before);
  assert (Allocations == Frees);
  puts ("PASS: NV create/read/verify/overwrite/delete, attribute guards, bounded retries and error handling.");
  puts ("Only mock runtime services were used; no host NVRAM writes.");
  return 0;
}
