/** @file
  Test only implemented modules, not the SealSecret plan.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "../NVMe/InfoFetch.h"
#include "TestMain.h"

EFI_STATUS
TestNVMeEnumeration (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;

  Print (L"STEP - Fetch all NVMe controllers info\r\n");
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"No NVMe PassThru controllers available: %r\r\n", Status);
    return Status;
  }

  Print (L"NVMe controller count: %u\r\n", (UINT32)Count);
  FreePool (Handles);
  return EFI_SUCCESS;
}

EFI_STATUS
TestNVMeInfoFetch (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  NVME_DRIVE_INFO DriveInfo = { 0 };

  // Same implemented flow as SealSecret.UefiMain().
  Print (L"STEP - Fetch all NVMe controllers info\r\n");
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"No NVMe PassThru controllers available: %r\r\n", Status);
    return Status;
  }
  if (Count != 1) {
    Print (L"Should be only 1 controller available; found %u.\r\n", (UINT32)Count);
    FreePool (Handles);
    return Count == 0 ? EFI_NOT_FOUND : EFI_UNSUPPORTED;
  }

  Print (L"STEP - NVMeGetControllerInfo\r\n");
  Status = NVMeGetControllerInfo (&DriveInfo, Handles[0]);
  FreePool (Handles);
  Print (L"NVMeGetControllerInfo: %r\r\n", Status);
  if (EFI_ERROR (Status)) {
    Print (L"Query failed; partial security fields are not interpreted.\r\n");
    return Status;
  }

  Print (L"Model: %a\r\nSerial: %a\r\nFirmware: %a\r\n", DriveInfo.Model, DriveInfo.Serial, DriveInfo.Firmware);
  Print (L"Ok: 0x%02x\r\nSecurity Send/Receive supported: %u\r\n",
    (UINT32)DriveInfo.Ok, (UINT32)DriveInfo.SupportSecurityRecvSend);
  Print (L"Ability: 0x%02x\r\nSecurityFeature: 0x%04x\r\n",
    (UINT32)DriveInfo.Ability, (UINT32)DriveInfo.SecurityFeature);
  Print (L"IsEnabled: 0x%02x\r\nIsLocked: 0x%02x\r\nOpalSSCVersionFlag: 0x%02x\r\n",
    (UINT32)DriveInfo.IsEnabled, (UINT32)DriveInfo.IsLocked, (UINT32)DriveInfo.OpalSSCVersionFlag);
  if (DriveInfo.SecurityFeature & NVME_INFO_SEC_FEATURE_ATA_SECURITY) {
    Print (L"AtaFrozen: %u\r\n", (UINT32)DriveInfo.AtaFrozen);
  } else {
    Print (L"AtaFrozen: N/A (ATA Security not reported as supported).\r\n");
  }
  Print (L"State masks: bit0=TCG, bit1=ATA; zero alone is not proof of support.\r\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters = NULL;
  CONST CHAR16 *Command;

  Print (L"TestMain - implemented module tests (read-only)\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (EFI_ERROR (Status)) { return Status; }
  if (Parameters->Argc == 1) {
    Print (L"Usage: TestMain.efi nvme-enum | nvme-info | --help\r\n");
    return EFI_SUCCESS;
  }
  if (Parameters->Argc != 2) {
    Print (L"Choose one test per invocation. Use --help.\r\n");
    return EFI_INVALID_PARAMETER;
  }

  Command = Parameters->Argv[1];
  if (StrCmp (Command, L"--help") == 0 || StrCmp (Command, L"-h") == 0) {
    Print (L"Usage: TestMain.efi nvme-enum | nvme-info | --help\r\n");
    Print (L"  nvme-enum: enumerate NVMe PassThru controllers and print count.\r\n");
    Print (L"  nvme-info: require exactly one controller, call NVMeGetControllerInfo and print results.\r\n");
    return EFI_SUCCESS;
  }

  if (StrCmp (Command, L"nvme-enum") == 0) { return TestNVMeEnumeration (); }
  if (StrCmp (Command, L"nvme-info") == 0) { return TestNVMeInfoFetch (); }

  Print (L"Unknown test: %s. Use --help.\r\n", Command);
  return EFI_INVALID_PARAMETER;
}
