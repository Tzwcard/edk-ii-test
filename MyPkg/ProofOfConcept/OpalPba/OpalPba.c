/** @file
  Static UEFI Shadow MBR notice, NOT an authenticator or state detector.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_INPUT_KEY Key;
  UINTN Index;
  if (gST->ConOut != NULL) { gST->ConOut->ClearScreen (gST->ConOut); }
  Print (L"Disk read error: MBR is missing\r\nContact game provider to resolve this issue.\r\n");
  // Stay on the notice. No disk access, password collection or automatic boot.
  for (;;) {
    if (gST->ConIn != NULL) {
      if (!EFI_ERROR (gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index))) {
        gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      }
    } else { gBS->Stall (1000000); }
  }
}
