# NVMeDisp.efi — NVMe Identify Controller

通过 `EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL` 读取 Identify Controller 数据。
仅发送 Admin Identify（opcode `0x06`、CNS=1、NSID=0），返回 4096 字节；不读取 SMART，不修改设备配置。

```text
NVMeDisp.efi              # 默认 UEFI 控制器 0，打印主要身份及能力
NVMeDisp.efi --list       # 所有控制器的设备路径、型号、序列号、固件版本
NVMeDisp.efi 1            # 选择控制器 1
NVMeDisp.efi 0 --raw      # 正常信息之后打印完整 4096 字节 hex
NVMeDisp.efi --help
```

`--list` 不与编号或 `--raw` 混用。
**UEFI 编号不是 Linux `/dev/nvme0` 的稳定名称。** 请与 `sudo nvme list` 的型号、序列号核对后选择。

## 输出内容

- Model / Serial / Firmware / 设备路径。
- PCI VID/SSVID、Controller ID、NVMe Version、IEEE OUI、Controller Type。
- CMIC、CTRATT、OAES、MDTS、RTD3 resume/entry latency。
- OACS 及 Security Send/Receive、Format NVM、固件更新、Namespace Management 等声明。
- Abort / Async Event Request 限制、FRMW 固件槽与激活能力、LPA 日志能力。
- 错误日志条目数、支持的电源状态数、APSTA、温度阈值、Host Memory Buffer 字段。
- TNVMCAP / UNVMCAP，使用完整 128 位十进制显示。
- SANICAP 的 crypto/block/overwrite 能力标志。
- SQES/CQES、MAXCMD、NN、ONCS、FUSES、FNA、VWC、AWUN/AWUPF、SGLS、SUBNQN。

这是 **Identify Controller**，不是 Identify Namespace。`NN` 是控制器报告的 namespace 数量/ID 范围相关字段，不代表当前实际激活的 namespace 数量；不能把它当作分区数量。
TNVMCAP 为零可能表示未报告，不能因此认为磁盘容量为零。获得具体 namespace 容量和 LBA 格式需要另行执行 Identify Namespace。
旧版本 NVMe 或未实现的可选字段可能为零；输出原始字段不等于所有字段在所有版本上都生效。
`MDTS` 的单位依赖控制器 `CAP.MPSMIN`，本工具不擅自假定最小页为 4 KiB；这里的 HMB 字段单位才是规定的 4 KiB。
ACL/AERL/ELPE/NPSS 按零基计数显示加一后的数量，AWUN/AWUPF 保留原始值。

**OACS 声明 Security Send/Receive 并不代表支持 OPAL 或 EF。**
查看具体安全协议仍使用 `TCGOpalCheck.efi`；SMART 健康信息使用 `SMARTDisp.efi`。
SANICAP/Format 等只显示能力，本应用不会发送任何擦除或格式化请求。

## 后续 adapt

- `NVMeDisp.c`：`ReadIdentify()` 负责命令提交，`ShowController()` 选择控制器，`Capabilities()` 打印字段。
- `IdentifyData.c/.h`：`NvmeIdentifyParse()` 将 4096 字节原始数据转换成 `NVME_IDENTIFY_INFO`。
- 数值使用显式小端读取，字符串处理尾部空格/NUL，并将非可打印字节替换成 `?`，避免控制台注入。
- 128 位容量显示复用 `../SMARTDisp/SmartData.c` 中的 `SmartDecimal()`；不会因此执行 SMART 命令。
- DMA 缓冲区满足控制器 `IoAlign`，提交参数 NamespaceId 和命令 NSID 都为 0。
- 传输失败、短响应或 NVMe completion 状态异常会报错，不把未读取的数据解释成能力；输出 DW3/SCT/SC 供诊断。

固件的 RAID/VMD 驱动如果没有提供 NVMe PassThru，工具会报告无可用接口，不尝试绕过控制器驱动直接访问寄存器。

## 构建与验证

```powershell
.\build.cmd
.\make-iso.cmd
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-nvme-identify.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-nvmedisp.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-nvmedisp.py RELEASE
```

产物：`out/DEBUG/NVMeDisp.efi` / `out/RELEASE/NVMeDisp.efi`，自动签名并加入对应 ISO。
ASan/UBSan 解析测试覆盖字段偏移、字节序、字符串边界、128 位字段与 10,000 组随机记录。
QEMU 使用两个临时空白 NVMe 控制器测试列举、编号选择、完整 hex 输出和错误参数。
日志位于 `out/qemu-nvmedisp/<目标>/serial.log`。
**不连接宿主物理磁盘；尚未在真实 SSD 上验证。**
