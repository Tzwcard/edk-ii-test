# AtaPassUtil.efi — 可在 UefiMain 中调用的 EF / ATA Security 接口

**默认入口仅列设备、查询 EF 状态，不修改任何磁盘。**
设置密码、解锁、禁用、冻结及擦除接口均已实现，但默认 `Main.c` 不调用它们。
这不是已经带命令解析的交互式密码工具；按你的要求，由你在 `UefiMain` 中明确选择设备和调用函数。

## 文件与产物

- `Main.c`：`UefiMain()`，从这里改你自己的调用流程。
- `AtaPassUtil.h`：完整公开函数声明、枚举、设备上下文。
- `AtaPassUtil.c`：EF 查询和安全操作实现。
- `../TCGOpalCheck/AtaSecurity.c`：共用只读状态解析器，不复制两份。
- `out/DEBUG/AtaPassUtil.efi`、`out/RELEASE/AtaPassUtil.efi`：编译产物。
- DEBUG ISO 已重新打包，包含该应用。

```powershell
cd D:\Workspace\EDKII
.\build.cmd
.\make-iso.cmd
.\run-iso.cmd
```

在 UEFI Shell 中选择工具所在文件系统后运行 `AtaPassUtil.efi`。

## 接口列表

包含 `AtaPassUtil.h`，使用以下函数：

| 函数 | 用途 | EF 协议特定值 |
| --- | --- | --- |
| `AtaPassOpen(Handle, &Device)` | 绑定一个整盘句柄，核对协议列表声明 EF，写入默认关闭 | `00` 协议列表查询 |
| `AtaPassGetStatus(&Device, &State)` | 只读状态 | `0000` / Receive |
| `AtaPassEnableWrites(&Device, TRUE)` | 调用者明确允许本上下文执行状态修改；不向硬盘发命令 | 无 |
| `AtaPassSetUserPassword(&Device, Password32, Level)` | 设置/更改 User 密码和 High/Maximum 级别 | `0001` / Send |
| `AtaPassSetMasterPassword(&Device, Password32, Identifier)` | 设置/更改 Master 密码及标识符 | `0001` / Send |
| `AtaPassUnlock(&Device, Authority, Password32)` | 使用已知密码解锁，不移除密码 | `0002` / Send |
| `AtaPassDisablePassword(&Device, Authority, Password32)` | 使用已知密码禁用密码安全功能 | `0006` / Send |
| `AtaPassFreeze(&Device)` | 冻结安全配置，不等于锁住数据 | `0005` / Send |
| `AtaPassErasePrepare(&Device, Confirmation)` | **破坏性擦除序列的准备步骤** | `0003` / Send |
| `AtaPassEraseUnit(&Device, Authority, Password32, Enhanced, Timeout100ns, Confirmation)` | **永久擦除数据** | `0004` / Send |
| `AtaPassEnableWrites(&Device, FALSE)` | 关闭本上下文的写操作 | 无 |
| `AtaPassClose(&Device)` | 清零上下文；不禁用硬盘密码、不解除冻结、不撤销已发出的操作 | 无 |
| `AtaPassWipe(Buffer, Size)` | 覆盖调用者持有的密码缓冲区 | 无 |

参数枚举：

```c
AtaPassUser
AtaPassMaster

AtaPassHigh
AtaPassMaximum

AtaPassDoNotErase
AtaPassConfirmPermanentDataLoss
```

所有磁盘操作都通过 `EFI_STORAGE_SECURITY_COMMAND_PROTOCOL`，使用协议 `0xEF`。
协议特定值经 `SwapBytes16()` 传给固件，与 EDK II 存储安全传输实现匹配。
密码参数块按 SAT 格式构造为 36 字节，不是直接发送 ATA 512 字节命令参数块。

## 只读调用示例

`VerifiedWholeDiskHandle` 应是你已通过设备路径核对的整盘句柄，而不是分区或不稳定的枚举编号。

```c
ATA_PASS_DEVICE      Device;
ATA_PASSWORD_STATUS State;
EFI_STATUS          Status;

Status = AtaPassOpen (VerifiedWholeDiskHandle, &Device);
if (EFI_ERROR (Status)) {
  return Status;
}

Status = AtaPassGetStatus (&Device, &State);
if (!EFI_ERROR (Status) && State.Supported) {
  Print (L"Enabled=%u Locked=%u Frozen=%u\r\n",
    (UINT32)State.Enabled, (UINT32)State.Locked, (UINT32)State.Frozen);
}

AtaPassClose (&Device);
return Status;
```

## 修改状态时的调用规则

1. 只选定并核对 **一个** 目标磁盘；不要直接在默认枚举循环里给所有设备执行写操作。
2. 先 `AtaPassOpen()` 和 `AtaPassGetStatus()`，确认状态。
3. 明确调用 `AtaPassEnableWrites(&Device, TRUE)`，否则修改接口返回 `EFI_WRITE_PROTECTED`。
4. 调用所需的单个接口，检查返回值。解锁/禁用接口使用已知密码，不进行猜测或重试。
5. 普通操作后重新读状态，确认实际结果，然后关闭写权限和上下文。
6. 用 `AtaPassWipe()` 清理你自己保存的密码。接口内部会清零传输缓冲区，但不会替你清理调用者的原始密码。

`WritesEnabled` 是防误调用开关，不是硬件认证或安全隔离机制。
操作前的状态检查只能过滤已知的不合法情况；真实设备仍决定操作是否允许。

### 密码必须是 32 个原始字节

`Password32` 必须指向完整的 32 字节缓冲区，不是短 C 字符串，更不是 `CHAR16 *`。
接口不会替你做 UTF-16 转换、截断、补零、字节交换或哈希，也不打印密码。
没有自动读取命令行/键盘的逻辑；你可自己实现无回显输入和设置密码的二次确认。

**BIOS 输入的字符串不一定等于硬盘收到的密码字节。**
不同 BIOS 可能采用不同编码或派生规则；仅使用相同的可见字符串，不能保证能解开 BIOS 设置的密码。
不要为了试验在有重要数据的系统盘上设置密码。
Master Password Identifier 只是标识符，不是密码；此接口保守拒绝设置 `FFFE`（工厂标识）和 `FFFF`（保留值）。

### 擦除接口的额外限制

**会永久破坏数据；默认入口绝不调用。** 不会自动设置临时密码，也不会自动从普通擦除降级/升级为其他擦除方式。

- 两个擦除接口都需要显式传入 `AtaPassConfirmPermanentDataLoss`。
- 必须先启用写操作，并成功执行 `AtaPassErasePrepare()`。
- 随后紧接着执行 `AtaPassEraseUnit()`；中间不要查询状态、修改写开关或进行其他操作，否则本上下文的准备标志失效。
- `AtaPassEraseUnit()` 单独要求非零的 `Timeout100ns`，单位是 **100 ns**；需根据设备擦除时间和固件限制选择，不能套用普通操作默认的 5 秒。
- Enhanced 模式要求准备阶段读取到 `EnhancedEraseSupported`。
- 本地准备标志只可消费一次，成功、失败或超时后均不自动重试。
- 外部固件/其他工具插入的命令仍可能影响设备准备状态，本地标志不保证硬件一定接受。

**失败或超时不等于命令未执行。** 尤其擦除可能仍在进行或设备已改变状态，不要自动重试、重置或断电。
使用独立、无重要数据且允许销毁的测试盘，并核对厂商文档。

## 状态和兼容性

- `Frozen` 会阻止设置/禁用/擦除等操作，本工具不尝试绕过。
- 密码尝试次数已超限时不发送解锁/禁用/擦除准备命令。
- Maximum 级别下，不尝试用 Master 密码解锁或禁用密码。
- 协议列表未声明 EF、分区句柄或状态响应异常时拒绝继续。
- 传输缓冲区按设备 `IoAlign` 分配，密码参数传输完成后用 volatile 写覆盖，再释放。
- 不自动重试密码操作，不自动切换协议、字节序或密码编码。
- 接口主要面向当前 NVMe/EF 使用场景；某些 SATA 桥或固件可能限制短传输或零长度 Send，失败时保留原始 EFI 状态，不做猜测性补发。
- 本工具只能取得固件返回的 EFI 状态，不能直接取得完整 NVMe completion 错误码。
- 解锁成功后，UEFI 的分区/文件系统驱动未必自动重新发现磁盘，本工具不会自动触发重连或复位。
- 密码访问控制不等于证明介质加密。

## 已执行的验证

```powershell
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-ata-pass.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-iso.py
```

- DEBUG、RELEASE 编译通过，DEBUG ISO 已更新。
- ASan/UBSan 下，使用模拟 UEFI/磁盘接口验证所有操作的 EF 值、字节序、密码参数块、写开关、擦除准备/确认限制、异常处理、无自动重试及缓冲区清零。
- QEMU 实际运行默认只读入口，未暴露可用 EF 磁盘时正常返回。
- **所有修改/擦除操作只在模拟接口上验证，没有在真实 SSD 上运行，也没有设置任何真实磁盘密码。**

协议依据：[SAT-2 §12.5.1 草案](https://t10.org/ftp/t10/document.08/08-451r0.pdf)
及 [T10 原始协议提案](https://www.t10.org/ftp/t10/document.08/08-075r0.pdf)。
