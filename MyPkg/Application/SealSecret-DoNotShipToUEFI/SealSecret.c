/** @file

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "../NVMe/InfoFetch.h"

#include "../Util/ValueAccess.h"

#include "SealSecret.h"

STATIC
VOID
PrintDummy ()
{}

#define PrintTest Print

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status = EFI_SUCCESS;

  // 命令行相关
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters = NULL;
  UINTN Index = 0;
  UINTN Count = 0;

  // NVMe 相关
  EFI_HANDLE *NVMePassThruHandles = NULL;
  NVME_DRIVE_INFO DriveInfo = { 0 };

  // 程序输入 argc / argv，目前可以留空？或者用marco #if 0 忽略？
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR(Status)) {
    for (Index = 1; Index < Parameters->Argc; Index++) {
        // TODO: add some codes?
    }
  }

  // 获取所有 NVMe 控制器
  PrintTest (L"STEP - Fetch all NVMe controllers info\r\n");
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &NVMePassThruHandles);
  if (EFI_ERROR (Status)) { Print (L"No NVMe PassThru controllers available: %r\r\n", Status); return Status; }
  if (Count != 1) { Print (L"Should be only 1 controller available.\r\n"); goto Done; }
  
  // 获取 NVMe 信息
  Status = NVMeGetControllerInfo(&DriveInfo, NVMePassThruHandles[0]);


Done:
  FreePool (NVMePassThruHandles);
  return Status;
}
