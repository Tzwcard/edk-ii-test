/** @file
  Explicit UEFI NVRAM test commands. Default invocation is read-only.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "NvVariable.h"
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC
EFI_STATUS
DisplayRead (VOID)
{
  EFI_STATUS Status;
  UINT32 Attributes;
  UINTN Size;
  UINTN Index;
  VOID *Data;
  BOOLEAN Matches;

  Status = NvTestRead (&Attributes, &Size, &Data);
  Print (L"GetVariable: %r\r\n", Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Print (L"Attributes: 0x%08x (NV=%u BS=%u RT=%u)\r\n", Attributes,
    (UINT32)((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0),
    (UINT32)((Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) != 0),
    (UINT32)((Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0));
  Print (L"Data size: %u bytes\r\n", (UINT32)Size);
  Print (L"Data hex (at most 256 bytes):\r\n");
  for (Index = 0; Index < MIN (Size, 256); Index++) {
    if ((Index % 16) == 0) {
      Print (L"  %04x:", (UINT32)Index);
    }

    Print (L" %02x", (UINT32)((UINT8 *)Data)[Index]);
    if (((Index % 16) == 15) || (Index + 1 == MIN (Size, 256))) {
      Print (L"\r\n");
    }
  }

  Matches = (Attributes == NV_TEST_ATTRIBUTES) && (Size == gNvTestPayloadSize) &&
            (CompareMem (Data, gNvTestPayload, Size) == 0);
  Print (L"Payload matches built-in test value: %s\r\n", Matches ? L"Yes" : L"No");
  FreePool (Data);
  return EFI_SUCCESS;
}

STATIC
VOID
StorageInfo (VOID)
{
  EFI_STATUS Status;
  UINT64 MaximumStorage;
  UINT64 RemainingStorage;
  UINT64 MaximumVariable;

  if (gRT->QueryVariableInfo == NULL) {
    Print (L"QueryVariableInfo: Not available\r\n");
    return;
  }

  Status = gRT->QueryVariableInfo (NV_TEST_ATTRIBUTES, &MaximumStorage, &RemainingStorage, &MaximumVariable);
  Print (L"QueryVariableInfo: %r\r\n", Status);
  if (!EFI_ERROR (Status)) {
    Print (L"NV storage: max=%lu, remaining=%lu, max-variable=%lu bytes\r\n",
      MaximumStorage, RemainingStorage, MaximumVariable);
  }
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  CONST CHAR16 *Command = L"read";
  UINT32 Attributes;
  UINTN Size;
  VOID *Data;

  Print (L"UEFINVTest - isolated NV variable read/write test\r\n");
  Print (L"Name: %s\r\nVendor GUID: %g\r\n", gNvTestVariableName, &gNvTestVendorGuid);
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR (Status)) {
    if (Parameters->Argc > 2) {
      Print (L"Usage: UEFINVTest.efi [read|write|test|delete|info]\r\n");
      return EFI_INVALID_PARAMETER;
    }

    if (Parameters->Argc == 2) {
      Command = Parameters->Argv[1];
    }
  }

  if ((StrCmp (Command, L"--help") == 0) || (StrCmp (Command, L"-h") == 0)) {
    Print (L"Usage: UEFINVTest.efi [read|write|test|delete|info]\r\n");
    Print (L"Default read is non-mutating. write replaces this variable; test refuses an existing one.\r\n");
    Print (L"NV writes persist. delete removes ONLY this test name/GUID.\r\n");
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"read") == 0) {
    return DisplayRead ();
  }

  if (StrCmp (Command, L"info") == 0) {
    StorageInfo ();
    return EFI_SUCCESS;
  }

  if ((StrCmp (Command, L"write") == 0) || (StrCmp (Command, L"test") == 0)) {
    StorageInfo ();
    Status = NvTestWrite (StrCmp (Command, L"write") == 0);
    Print (L"SetVariable (NV|BS|RT): %r\r\n", Status);
    if (Status == EFI_ALREADY_STARTED) {
      Print (L"Test refused: variable already exists. Read it first; use explicit write/delete if intended.\r\n");
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = NvTestVerify ();
    Print (L"Read-back verification: %r\r\n", Status);
    if (!EFI_ERROR (Status)) {
      Print (L"UEFINVTest PASS: NV|BS|RT payload verified; variable left for reboot/read testing.\r\n");
      Status = DisplayRead ();
    }

    return Status;
  }

  if (StrCmp (Command, L"delete") == 0) {
    Status = NvTestDelete ();
    Print (L"Delete test variable: %r\r\n", Status);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = NvTestRead (&Attributes, &Size, &Data);
    if (Status == EFI_SUCCESS) {
      FreePool (Data);
      Status = EFI_DEVICE_ERROR;
    } else if (Status == EFI_NOT_FOUND) {
      Print (L"UEFINVTest DELETE PASS: variable is absent.\r\n");
      return EFI_SUCCESS;
    }

    return Status;
  }

  Print (L"Unknown command. Use UEFINVTest.efi --help\r\n");
  return EFI_INVALID_PARAMETER;
}
