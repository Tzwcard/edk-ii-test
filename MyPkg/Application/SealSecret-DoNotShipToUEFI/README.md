# 密码封装

## 目的

- 使用硬盘信息生成密钥封装到 `TPM` 内。
- 可以在启动时通过 [Unlocker](../Unlocker/README.md) 解封该密钥，从而解锁硬盘。

## 执行逻辑

1. 检查是否存在并只存在单个 NVMe 硬盘，不存在则退出
2. 检查 NVMe 安全设置，如果不支持 SSC 或不支持 ATA 透传 → ATA 安全特性则退出
3. 获取型号以及序列号（Model 和 Serial），获取失败则退出
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
    - 此 App 只作用在工厂初始化阶段，所以该变量不需要管它是否为旧数据
    - 读写删除可参照 UEFINVTest
7. 将数据写入到上述 UEFI 变量内，失败则退出
    - 数据包含目标型号、序列号、TPM 解密材料块
    - 保证完整性可能假如哈希值
8. 完成

## Remarks
- 上述只作为封存解密密钥到 TPM 内，密钥生成函数 KDF_User() 必须与加密/锁定时函数一致
- 这里不会变更硬盘加密/锁定状态，它可以是已启用安全特性，也可以是未启用安全特性，但一般
  来说到了这一步需要插入的硬盘已经是启用安全特性了的
- 本程序只允许存在于工厂制造阶段，不能使用也不能出现在这之外

## 约定

### NVRAM 数据结构
```
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
```
- 信息需要进行简单的流式混淆/加密例如rc4等便宜算法使其他人就算能读取到也不能直接用
    - 因为设计上保存使用`(NV | BS)`，因为设计是一个`UEFI Shell`运行的程序。但是`BS`的话其他`BootManager`拉起的`EFI`也能读到，所以加入简单混淆变成高熵数据
    - 但是不要用rc4

### NVRAM PATH
- `ApplicationInfo-bd29e016-764e-4a58-9d53-d64fe953edab`