/** @file
  DXE app for:
    1. 检查 UEFI 变量 (NV | BS) 是否存在，不存在则退出
        - `ApplicationInfo-bd29e016-764e-4a58-9d53-d64fe953edab`
    2. 打开并读取 UEFI 变量，获取数据，失败则退出
        - 包括硬盘型号、序列号以及 TPM 解密块
    3. 检查是否存在目标 NVMe 硬盘，操作如下：
        a. 若存在则读取其 LBA 范围
        b. 若不存在则删除 UEFI 变量，并退出
            - 防止硬盘替换尝试
            - 可能需要增加另外变量来计数→ Threshold 来处理假阳性
    4. 检查 NVMe 安全设置，如果不支持 SSC 或不支持 ATA 透传 → ATA 安全特性则退出
        - 包括检查是否已加密/已锁定
        - 该步骤退出时删除 UEFI 变量，或 FAILED > THRESHOLD 后删除
    5. 通过 TPM2 使用 PCR 0 策略解封解密/解锁密钥 `ps_u`，失败则退出
        - 该步骤退出时删除 UEFI 变量，或 FAILED > THRESHOLD 后删除
    6. 使用对应协议进行解密/解封，不管成功与否清理内存内的密钥
        - 该步骤退出时删除 UEFI 变量，或 FAILED > THRESHOLD 后删除
    7. 完成

  上述程序会在正式 BIOS/UEFI 内启用，不包含密钥生成算法
  错误后删除变量操作待定，但退出后回到正常引导
  因目前后续可能会有多个硬盘规格/品牌等，这里做硬盘白名单不现实，暂时不考虑
  DXE 程序未知点太多，需要更多信息
  暂定执行与 DXE 阶段最后

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/