# ProofOfConcept

调研、验证代码，与 `MyPkg/Application/` 同级；可作为正式程序的候选实现，不直接混入业务模块。

- [AtaPassUtil](AtaPassUtil/README.md)：通过 NVMe Security Recv/Send 进行 ATA Security 协议通讯（未测试）。
- [NVMeDisp](NVMeDisp/README.md)：NVMe 信息获取。
- [NVMeResetTest](NVMeResetTest/README.md)：NVMe 控制器重置；测试 ATA Security Frozen 重置无效。
- [OpalTest](OpalTest/README.md)：TCG OPAL 通讯、显式确认的手动操作测试。
- [OpalPba](OpalPba/README.md)：静态 UEFI Shadow MBR 提示镜像。
- `PCRRead/`：读取当前 PCR 寄存器值。
- [SMARTDisp](SMARTDisp/README.md)：SMART 信息获取。
- [TCGOpalCheck](TCGOpalCheck/README.md)：TCG OPAL / ATA Security 状态获取。
- [TPMOps](TPMOps/README.md)：TPM 封入/解封测试。
- [UEFINVTest](UEFINVTest/README.md)：UEFI NVRAM 读写抹除测试。
- `HelloWorld/`：最小 UEFI 应用。

不新增单独构建入口，`build.cmd [DEBUG|RELEASE]` 统一编译、签名全部现有可构建应用；
`make-iso.cmd [DEBUG|RELEASE]` 将 Application 的 TestMain 与全部 PoC 一起打包到原有 Shell ISO。
SealSecret/Unlocker 草稿仍不参与构建。每个 PoC 的操作风险和测试范围见自己的 README。
