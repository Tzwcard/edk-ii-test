# NVMeResetTest.efi — 交互式检查 / Reset / 再检查

用于人工验证 **NVMe driver reset 前后 EF Frozen / TCG Locking 状态是否变化**。
不是解冻工具，不保证 reset 会清除 Frozen。

## 使用

从独立 USB/ISO 的 UEFI Shell 启动，先核对型号、序列号和路径：

```text
NVMeResetTest.efi --list
NVMeResetTest.efi 0        # UEFI NVMe 控制器 0
NVMeResetTest.efi 1        # 或其他控制器
NVMeResetTest.efi          # 默认 0，仍有全部交互确认
NVMeResetTest.efi --help
```

控制器编号与 `NVMeDisp` 使用同一种 PassThru 枚举方式，但不是跨启动稳定 ID，也不保证对应 Linux nvme0。
`--list` 只读取 Identify，不 reset；不能与索引混用。

进入测试后按提示输入完整单词再回车：

| 阶段 | 确认输入 | 执行内容 |
| --- | --- | --- |
| 1 | `CHECK` | TCGOpalCheck 同样的只读协议列表、EF 状态、TCG Discovery 查询 |
| 2 | `RESET` | 重新核对身份后，仅调用一次 `BlockIo->Reset(BlockIo, TRUE)` |
| 3 | `CHECK` | 重新枚举并核对原设备，再查询安全状态并汇总差异 |

**每一步都停下来等待输入，无倒计时、不自动前进。** 空回车或其他单词不会确认。
每个等待点都能输入 `q` 回车或按 Esc 取消；取消只阻止后续操作，不能撤销已完成的 reset。
确认词区分大小写。不提供无人值守 `--yes`。

## 前后状态

仅检查所选控制器下的一个完整 NVMe namespace，不扫描其他磁盘的安全状态。
控制器有多个 namespace 时，使用第一个匹配的完整 namespace 作为驱动 Reset/安全查询入口，并打印其路径；
**这并不代表 reset 只影响这个 namespace**。

复用 `../TCGOpalCheck/SecurityCheck.c`，而不是启动外部 `TCGOpalCheck.efi`。
因此没有查找外部 EFI 文件或子程序返回后误选目标的依赖；独立 TCGOpalCheck 仍保留原有全盘只读枚举。

最终汇总示意（不是实机测试结果）：

```text
EF Enabled: 0 -> 0
EF Locked: 0 -> 0
EF Frozen: 1 -> 1
TCG Locking flags: 0x.. -> 0x..
```

EF 与 OPAL 的有效性独立：例如 SSD 只有 `00 EF`、OPAL Discovery 失败时，仍可比较有效的 EF 状态。
若前后任一响应缺失、格式异常、或未声明支持，则显示 Unknown/unsupported，**不会将读取失败当作 Frozen=0**。
驱动 Reset 返回错误时不重试，仍提供第 3 步只读诊断的机会，最终保留 reset 的错误返回。
序列完成标记只说明走完流程，**不表示已解冻，亦不表示所有安全查询成功**。

## 安全与平台限制

- 这不是纯只读应用：**reset 会改变控制器运行状态**，可能中断 I/O、使设备暂时不可访问，或使配置为 reset 时锁定的范围重新上锁。
- 先备份重要数据，关闭目标盘文件和其他应用，不要在正在使用的系统盘/存储工作负载上实验。
- 应从独立 USB/ISO 运行，避免重置存放 Shell/应用的控制器。
- 仅通过驱动提供的 Block I/O Reset，不写 PCI/MMIO 寄存器，不执行 FLR、Subsystem Reset、掉电或驱动断开/重连。
- 当前 EDK II `NvmExpressDxe` 的 `NvmeBlockIoReset()` 会调用 `NvmeControllerInit()`；厂商固件可能不同，API 成功不证明发生了特定底层 reset 类型。
- 不发送 Security Send、Freeze、设置密码、Unlock、Format、Sanitize 或擦除命令，不写磁盘扇区、UEFI 变量或 TPM。
- reset 不是 SSD 断电，也不是 ACPI S3。**Frozen 是否清除取决于设备/平台，不能预先保证。**
- 固件驱动无响应时，应用可能停在 reset 调用中；进入调用后没有本应用可强制打断的超时机制。

每次确认后重新按保存的完整路径定位控制器及 namespace，并比较 Identify 原始 VID/SSVID/Serial/Model/Firmware。
reset 后不继续使用旧协议指针、不按新枚举索引重新选盘；找不到原路径或身份不符就停止，**不回退到另一块盘**。
无完整 namespace Block I/O（例如只暴露 PassThru、RAID/VMD 隐藏接口）时拒绝 reset。

## 构建与测试

已接入 DEBUG/RELEASE 编译、自动签名、ISO 根目录和 FAT 启动镜像：

```text
out/DEBUG/NVMeResetTest.efi
out/RELEASE/NVMeResetTest.efi
```

```powershell
.\make-iso.cmd
.\make-iso.cmd RELEASE
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-security-check.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-nvme-reset.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-nvme-reset.py RELEASE
```

- ASan/UBSan mock transport：EF 冻结/未冻结样本、OPAL 失败仍保留 EF、短响应/异常格式/协议列表错误为 Unknown、缓冲区释放。
- 两个临时 QEMU NVMe：编号选择、三个步骤确认、空/错误输入、每一步取消、实际驱动 reset 成功、reset 后身份核对、两控制器仍可 Identify、独立 TCGOpalCheck 回归。
- QEMU NVMe 不提供真实 OPAL/EF 状态；**模拟器测试不验证解除 Frozen，实际 SSD 的效果需要你手动测试。**
- 日志：`out/qemu-nvme-reset/<目标>/serial.log`。
- 自动测试只连接临时磁盘镜像和 OVMF VARS，不连接宿主物理磁盘、不操作实际主板 NVRAM。
