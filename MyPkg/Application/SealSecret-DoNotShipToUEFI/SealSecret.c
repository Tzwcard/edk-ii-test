/** @file
  UEFI app for:
    1. 检查是否存在并只存在单个 NVMe 硬盘，不存在则退出
    2. 检查 NVMe 安全设置，如果不支持 SSC 或不支持 ATA 透传 → ATA 安全特性则退出
        - 可参考 TCGOpalCheck
    3. 获取型号以及序列号（Model 和 Serial），获取失败则退出
        - 使用 NVMe Identity Controller?
        - SMART 可参考 SMARTDisp
    4. 使用 KDF_User(Model, Serial, secret) 生成用户密码 `ps_u`
        - 在 TCG OPAL Security 下为 User1 密码，128/256 位
        - 在 ATA Security 下为 user 密码，32 字节
        - 这里统一生成一个 32 字节串作为密码
        - 算法待定，但大概率是 SHA-256/SHA-256-HMAC
        - 该算法只存在于此 App 以及硬盘加密/锁定程序内
    5. 将 `ps_u` 封入 TPM2，使用 PCR 0 策略，获取解封装材料，失败则退出
        - 无论是否失败，封入操作后马上清理内存内的密码
        - 封入后加入 Unseal 流程验证确定是否成功？
        - 需要验证封装流程
    6. 检查 UEFI 变量 (NV | BS) 是否存在，存在则移除，移除失败则退出
        - `ApplicationInfo-bd29e016-764e-4a58-9d53-d64fe953edab`
        - 此 App 只作用在工厂初始化阶段，所以该变量不需要管它是否为旧数据
        - 读写删除可参照 UEFINVTest
    7. 将数据写入到上述 UEFI 变量内，失败则退出
        - 数据包含目标型号、序列号、TPM 解密材料块
        - 保证完整性可能假如哈希值
    8. 完成

  上述只作为封存解密密钥到 TPM 内，密钥生成函数 KDF_User() 必须与加密/锁定时函数一致
  这里不会变更硬盘加密/锁定状态，它可以是已启用安全特性，也可以是未启用安全特性，但一般
  来说到了这一步需要插入的硬盘已经是启用安全特性了的
  本程序只允许存在于工厂制造阶段，不能使用在这之外，也不能出现在工厂外

  NVRAM 信息结构：
  {
    MAGIC
    VERSION

    MODEL[]
    SERIAL[]

    SIZE_PRI
    SIZE_PUB
    SIZE_X
    SIZE_Y
    ...

    TPM_DATA (PRI+PUB+X+Y+...?)

    SHA256 (ABOVE ALL)
  }

  信息最好再进行个简单的流式混淆/加密例如rc4等便宜算法使其他人就算能读取到也不能直接用（但是不要用rc4）

  Misc:
  It should be better to have only NV to survive between boots but since this appli
  will likely run with USB drive it needs BS for this to write / read / delete it.

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
  if (Count != 1) { Print (L"Should be only 1 controller available.\r\n"); FreePool (NVMePassThruHandles); return Status; }
  
  // 获取 NVMe 信息
  Status = NVMeGetControllerInfo(&DriveInfo, NVMePassThruHandles[0]);


  FreePool (NVMePassThruHandles);

  return Status;
}
