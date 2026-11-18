/** @file
  A standalone UEFI application: no OS and no C runtime required.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Print (L"Hello from EDK II!\r\n");
  Print (L"Firmware: %s, revision 0x%08x\r\n",
    SystemTable->FirmwareVendor, SystemTable->FirmwareRevision);
  Print (L"UEFI application is running successfully (X64).\r\n");

  // Return to the calling UEFI shell / boot manager after five seconds.
  gBS->Stall (5 * 1000 * 1000);
  return EFI_SUCCESS;
}
