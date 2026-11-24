# 密码封装

## 目的

- 使用硬盘信息生成密钥封装到 `TPM` 内。
- 可以在启动时通过 [Unlocker](../Unlocker/README.md) 解封该密钥，从而解锁硬盘。

## 执行逻辑

1. 检查是否存在并只存在单个 NVMe 硬盘，不存在则退出
2. 检查 NVMe 安全设置，如果不支持 SSC 或不支持 ATA 透传 → ATA 安全特性则退出
3. 获取型号以及序列号（Model 和 Serial），获取失败则退出
4. 检查 UEFI 变量 (NV | BS) 是否存在，不存在则退出
5. 打开并读取 UEFI 变量，获取数据，失败则退出
6. 检查 UEFI 变量内的内容是否于目标硬盘信息一直，不一致则退出
    - 检查不到硬盘时删除该变量以防换盘尝试？
7. 通过 TPM2.0 使用 PCR 0 策略解封解密/解锁密钥 `ps_u`，失败则退出
    - （TCG OPAL）使用 `user1`、 Global range，
8. 完成

## Remarks
- 上述只作为封存解密密钥到 TPM 内，密钥生成函数 KDF_User() 必须与加密/锁定时函数一致
- 这里不会变更硬盘加密/锁定状态，它可以是已启用安全特性，也可以是未启用安全特性，但一般
  来说到了这一步需要插入的硬盘已经是启用安全特性了的
- 本程序只允许存在于工厂制造阶段，不能使用也不能出现在这之外
- 当多种安全措施都可用时，优先度为 `OPAL SSC → Pyrite SSC → ATA Security`

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