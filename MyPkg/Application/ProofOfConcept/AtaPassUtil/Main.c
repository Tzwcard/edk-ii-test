/** @file
  SAFE DEFAULT: enumerate and read status only. Put deliberate API calls here.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "AtaPassUtil.h"
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS               Status;
  EFI_HANDLE               *Handles;
  UINTN                    Count;
  UINTN                    Index;
  ATA_PASS_DEVICE          Device;
  ATA_PASSWORD_STATUS      State;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  CHAR16                   *Text;

  Print (L"AtaPassUtil - default entry is READ ONLY; writes disabled.\r\n");
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"Cannot enumerate devices: %r\r\n", Status);
    return Status;
  }

  for (Index = 0; Index < Count; Index++) {
    Print (L"\r\nBlock handle %u:\r\n", (UINT32)Index);
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (!EFI_ERROR (Status)) {
      Text = ConvertDevicePathToText (Path, FALSE, FALSE);
      if (Text != NULL) {
        Print (L"  %s\r\n", Text);
        FreePool (Text);
      }
    }

    Status = AtaPassOpen (Handles[Index], &Device);
    if (EFI_ERROR (Status)) {
      Print (L"  EF device open: %r (partitions/non-EF devices are skipped).\r\n", Status);
      continue;
    }

    Status = AtaPassGetStatus (&Device, &State);
    Print (L"  EF status: %r\r\n", Status);
    if (!EFI_ERROR (Status)) {
      Print (L"  Supported=%u\r\n", (UINT32)State.Supported);
      if (State.Supported) {
        Print (L"  Enabled=%u Locked=%u Frozen=%u AttemptsExceeded=%u\r\n",
          (UINT32)State.Enabled, (UINT32)State.Locked,
          (UINT32)State.Frozen, (UINT32)State.AttemptsExceeded);
        Print (L"  MAXSET=%u EnhancedEraseSupported=%u MasterId=0x%04x\r\n",
          (UINT32)State.MaximumSecurity, (UINT32)State.EnhancedEraseSupported,
          (UINT32)State.MasterPasswordIdentifier);
      }
    }

    // Intentionally NO write calls here. Do not apply writes in an enumeration
    // loop: select and verify ONE device path first. See README.md for the API.
    AtaPassClose (&Device);
  }

  FreePool (Handles);
  Print (L"AtaPassUtil finished: read-only entry; no SendData calls.\r\n");
  return EFI_SUCCESS;
}
