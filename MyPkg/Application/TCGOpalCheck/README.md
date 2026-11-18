# TCGOpalCheck.efi

只读检测固件可见的整盘 Block I/O 设备的 TCG OPAL 能力，以及 `0xEF` ATA 密码安全状态。
不依赖 TPM，不发送 `SendData`，不设置密码、不激活锁定 SP、不解锁、不执行 Revert。

## 编译与运行

Windows 工作目录 `D:\Workspace\EDKII`：

```powershell
.\build.cmd
.\make-iso.cmd
.\run-iso.cmd
```

UEFI Shell：

```text
map -r
fs0:
TCGOpalCheck.efi
```

如 `fs0:` 不是工具所在文件系统，按 `map -r` 结果选择其他 `fsN:`。
独立程序位于 `out/DEBUG/TCGOpalCheck.efi`，可以复制到已有的 FAT32 测试 U 盘，在 UEFI Shell 中运行。
ISO 内的根目录和 FAT EFI 启动镜像均包含它。RELEASE 构建同样支持；当前已重建的 ISO 是 DEBUG。
已有系统盘上不要修改 OPAL 或 Secure Boot 配置；若需要调整启动设置，先准备好 BitLocker 恢复密钥。
程序未签名，受 Secure Boot 策略限制。

## 代码中的关键函数

| 函数 | 用途 |
| --- | --- |
| `gBS->LocateHandleBuffer()` | 按 Block I/O 协议枚举设备 |
| `gBS->HandleProtocol()` | 获取同一设备上的 Block I/O、设备路径、Storage Security 协议 |
| `ConvertDevicePathToText()` | 打印设备路径，帮助辨认磁盘 |
| `AllocateAlignedPages()` | 按设备的 `IoAlign` 分配传输缓冲区 |
| `Security->ReceiveData()` | 只发送 Level 0 Discovery 读取请求 |
| `ParseDiscovery()` | 完整检查长度后解析功能描述符 |

`ReadDiscovery()` 使用安全协议 ID `0x01`、Level 0 Discovery 协议特定值 `0x0001`。
协议特定值传入 `ReceiveData()` 前用 `SwapBytes16()` 转为线序，与 EDK II 自带 OPAL 传输代码一致。
每次请求设定 5 秒超时，初始缓冲区 4096 字节；如声明的完整响应更大，则扩大到 512 字节倍数，最多 64 KiB。
设备返回失败、截断、异常长度或格式损坏时不会输出支持/锁定结论，而是报告未知。

`Discovery.c` 是与设备传输分离的解析器，`Discovery.h` 定义解析结果。
本应用只依赖 `MdePkg`，不需要额外链接 `TcgStorageOpalLib`，也不会引入该库中的修改设备状态函数。

## 如何理解结果

- `OPAL 1.x/2.x SSC advertised: Yes`：设备完整发现响应声明了对应 OPAL SSC，不能据此证明拥有管理权限。
- `Opalite`、`Pyrite`：分别列出，不将它们误报为完整 OPAL 1.x/2.x；Pyrite 也不能据此视为支持介质加密。
- `LockingSupported`：设备声明支持锁定。
- `LockingEnabled`：锁定 SP 已启用，并不表示当前磁盘已锁定。
- `Locked`：设备级锁定标志，不是逐一锁定范围的权限报告。
- `MediaEncryption`：设备声明支持介质加密，不代表当前 OS 正在使用 OPAL，也不是加密的独立验证。
- `MBREnabled` / `MBRDone`：MBR shadowing 状态标志。
- `OPAL support: Unknown`：固件未暴露接口，或查询失败；不能推导出硬盘不支持 OPAL。
- 完整发现响应中没有 OPAL 描述符时，仅报告“未声明”，不把错误/截断响应当作“不支持”。

RAID/VMD、USB 转接、固件驱动和控制器策略可能阻断查询接口。本工具不做底层 ATA/NVMe PassThru 绕过。
当前只枚举具有整盘 Block I/O 的句柄，跳过分区句柄；固件完全隐藏的设备不会被发现。
请用设备路径与实际 SSD 对应，不要把光驱、虚拟盘的结果当作目标 SSD 的结果。

## 验证范围

```powershell
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-opal-parser.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-iso.py
```

- X64 DEBUG 和 RELEASE 编译通过。
- 解析器合成用例涵盖 OPAL 1/2、Opalite、Pyrite、锁定标志、未知功能、截断、越界和重复 Locking 描述符。
- 10,000 组确定性异常输入在 AddressSanitizer / UndefinedBehaviorSanitizer 下通过。
- DEBUG ISO 在 QEMU / OVMF 中实际启动并执行应用，验证无可用 OPAL 接口时正确报告 Unknown。
- **没有真实 OPAL SSD 成功查询的测试结果**；需要在目标硬件上运行。普通 QEMU 磁盘不提供 OPAL 功能。

## 0xEF：ATA Device Server Password Security 状态

新增流程保留原有 `0x01` Discovery 请求，即使协议列表没有 `01` 仍会尝试。
同时在协议列表成功且声明 `EF` 时，独立读取 EF 状态；两者不是相互替代或互为前置条件。
实现上先输出 EF 状态，再执行 OPAL Discovery，避免原先 Discovery 失败后的 `continue` 跳过 EF。
协议列表失败或格式不完整时，EF 保持 Unknown，且不盲目发送 EF 请求。

EF 只使用 `ReceiveData()`：

```text
SecurityProtocolId           = 0xEF
SecurityProtocolSpecificData = 0x0000
PayloadBufferSize            = 16
```

这不是 ATA IDENTIFY 数据，也不是 OPAL 功能描述符，不能复用它们的偏移或字节序。
`AtaSecurity.c` 按 SAT 格式解析 16 字节响应，前两字节为大端长度 `00 0E`。
传输失败、返回字节数不符、长度不符时报告 Unknown，不输出猜测出的状态；同时打印返回状态和成功响应的原始 16 字节，便于硬件排查。

输出字段：

- `ATA Security supported`：`S_SUPRT`。
- `Password security enabled`：`S_ENABLD`，不是“当前锁定”。
- `Locked`：`LOCKED`。
- `Frozen`：`FROZEN`，禁止某些安全配置操作，不等于用户数据已锁定。
- `Password attempts exceeded`：`PWCNTEX`，不执行任何密码尝试。
- `Enhanced erase supported`：只显示能力，不执行擦除。
- `Master password identifier`：标识符，**不是密码**。
- `MAXSET` 与 High/Maximum 安全级别：只有安全功能启用时才将其解释为当前生效的安全级别。

`S_SUPRT=0` 时不解释剩余密码状态字段。EF 支持或启用均不证明介质已加密。
当前设备未声明 `01` 且 Discovery 失败时，会明确报告 OPAL 未通过当前接口提供，而不是仅给出泛化的 Unknown。

参考格式：
- [T10 ATA 密码安全协议提案：请求及 16 字节响应布局](https://www.t10.org/ftp/t10/document.08/08-075r0.pdf)
- [SAT-2 草案，§12.5.1](https://t10.org/ftp/t10/document.08/08-451r0.pdf)

新增 EF 解析测试涵盖大端字段、各状态位、短响应、零填充、错误字节序及未知长度；与 OPAL 解析器一起在 ASan/UBSan 下各运行 10,000 组异常输入。
DEBUG/RELEASE 构建和无存储安全接口的 QEMU 启动测试通过；**尚未验证真实 SSD 的 EF 成功响应**，请在目标硬件上复测。
