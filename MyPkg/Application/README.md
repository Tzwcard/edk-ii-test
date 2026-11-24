# 程序代码

## 独立的 ProofOfConcept

- 调研、验证代码现放在同级的 [../ProofOfConcept](../ProofOfConcept/README.md)，不混入 Application 目录。
- 构建入口不拆分：仍统一编译、签名并打包到同一个 ISO。

## SealSecret-DoNotShipToUEFI

- 解锁密钥封入 `TPM` 的程序
- 需要：
    - `NVMe` 信息获取（[NVMe/InfoFetch.c](./NVMe/InfoFetch.c)） - [NVMeDisp](../ProofOfConcept/NVMeDisp/README.md)
    - `NVMe - TCG OPAL` 协议通讯（[NVMe/TCGOpalSecurity.c](./NVMe/TCGOpalSecurity.c)） - [OpalTest](../ProofOfConcept/OpalTest/README.md)
    - `NVMe - ATA PassThru` 协议通讯（[NVMe/ATASecurity.c](./NVMe/ATASecurity.c)） - [AtaPassUtil](../ProofOfConcept/AtaPassUtil/README.md)
    - `TPM` 信息封入（[TPM/Seal.c](./TPM/Seal.c)） - [TPMOps](../ProofOfConcept/TPMOps/README.md)
    - `NVRAM` 数据读写（[NVRAM/ReadWrite.c](./NVRAM/ReadWrite.c)） - [UEFINVTest](../ProofOfConcept/UEFINVTest/README.md)
    - `Key Delivery Function` 密钥生成算法，需要跟初始化端同等（[KDF/Generate.c](./KDF/Generate.c)）


## Unlocker

- 从 `TPM` 解封密钥实现磁盘解锁
- 需要：
    - `NVMe` 信息获取（[NVMe/InfoFetch.c](./NVMe/InfoFetch.c)） - [NVMeDisp](../ProofOfConcept/NVMeDisp/README.md)
    - `NVMe - TCG OPAL` 协议通讯（[NVMe/TCGOpalSecurity.c](./NVMe/TCGOpalSecurity.c)） - [OpalTest](../ProofOfConcept/OpalTest/README.md)
    - `NVMe - ATA PassThru` 协议通讯（[NVMe/ATASecurity.c](./NVMe/ATASecurity.c)） - [AtaPassUtil](../ProofOfConcept/AtaPassUtil/README.md)
    - `TPM` 信息解封（[TPM/Unseal.c](./TPM/Unseal.c)） - [TPMOps](../ProofOfConcept/TPMOps/README.md)
    - `NVRAM` 数据读（[NVRAM/ReadWrite.c](./NVRAM/ReadWrite.c)） - [UEFINVTest](../ProofOfConcept/UEFINVTest/README.md)
