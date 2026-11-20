/** @file
  Enumerate whole block devices and perform shared read-only security checks.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "SecurityCheck.h"

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN Count;
  UINTN Index;
  UINTN Devices = 0;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  SECURITY_CHECK_RESULT Result;

  Print (L"TCGOpalCheck - read-only TCG Level 0 Discovery\r\n");
  Print (L"Includes ATA Device Server Password Security (0xEF) status.\r\n");
  Print (L"No configuration, passwords, or disk data will be changed.\r\n");
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"Cannot enumerate block devices: %r. OPAL support: Unknown.\r\n", Status);
    return Status;
  }
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
    if (EFI_ERROR (Status) || BlockIo->Media == NULL || BlockIo->Media->LogicalPartition) { continue; }
    Print (L"\r\nDevice %u:\r\n", (UINT32)Devices++);
    CheckStorageSecurity (Handles[Index], &Result);
  }
  FreePool (Handles);
  Print (L"\r\nChecked %u whole-device handle(s); no write commands issued.\r\n", (UINT32)Devices);
  Print (L"Discovery does not prove ownership, credentials, or per-range access.\r\n");
  return EFI_SUCCESS;
}
