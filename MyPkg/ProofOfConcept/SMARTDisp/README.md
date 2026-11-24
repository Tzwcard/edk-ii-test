# SMARTDisp.efi

在 UEFI Shell 中只读打印 NVMe SMART / Health Information（日志页 `0x02`）。

```text
SMARTDisp.efi           # 默认 UEFI 枚举的控制器 0
SMARTDisp.efi --list    # 列出控制器的路径、型号、序列号、固件版本
SMARTDisp.efi 1         # 读取枚举的控制器 1
SMARTDisp.efi --help
```

**UEFI 没有 Linux 的 `/dev/nvme0` 名称。** 默认的控制器 0 不保证与 Linux nvme0 是同一设备。
有多个 NVMe 时，先 `--list`，用型号、序列号和设备路径与 Linux 的 `sudo nvme list` 对应，不能仅依赖编号。

## 输出

- 型号、序列号、固件版本、控制器设备路径。
- Critical Warning 原始位图及已知告警。
- Composite Temperature（Kelvin 和 Celsius）、可用温度传感器。
- Available Spare、Spare Threshold、Percentage Used（允许超过 100%，不是简单的剩余健康度）。
- Data Units Read/Written 及按 `512000 bytes/unit` 换算的计量值；设备计量有向上取整，不能视为精确字节计数。
- Host Read/Write Commands、Controller Busy Time（分钟）。
- Power Cycles、Power On Hours、Unsafe Shutdowns。
- Media and Data Integrity Errors、Error Information Log Entries。
- Warning/Critical Temperature Time（分钟）。
- Thermal Management 1/2 转换次数及累计时间（秒）。

所有 128 位计数器均按完整精度转换成十进制，不截断为 UINT64；字节换算也不会溢出 128 位。
温度值为 0 时显示 Not reported。某些可选字段未实现时会返回 0，不能仅靠 0 区分“未实现”和“真实为零”。

## 实现

`SMARTDisp.c` 使用 `EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL`，仅发送两个 Admin 读取命令：

1. Identify Controller：opcode `0x06`，CNS=1，NSID=0，4096 字节。
2. Get Log Page：opcode `0x02`，LID=`0x02`，NSID=`0xFFFFFFFF`（控制器整体/所有 namespaces），512 字节。

Get Log Page 的 NUMD 为 `512/4-1=127`，设置 RAE=1 保留相关异步事件。
为兼容驱动，PassThru 参数 NamespaceId 与命令 NSID 保持一致；缓冲区满足控制器 `IoAlign`。
命令失败时打印 EFI_STATUS 及 NVMe completion DW3、SCT、SC，不将失败或短响应解释成健康状态。

`SmartData.c/.h` 独立负责固定长度日志解析、128 位十进制转换和字节换算，使用显式小端解码，避免未对齐结构访问。
不发送 Set Features、固件更新、Sanitize、Format、Security Send 或磁盘写入命令。

如果固件的 RAID/VMD 驱动不暴露 NVMe PassThru，会报告没有可用控制器；这不意味着设备没有 SMART。
当前显示标准的控制器级日志，不读取厂家自定义日志，也不逐个查询 namespace 专属 SMART。

## 编译与签名

```powershell
cd D:\Workspace\EDKII
.\build.cmd
.\make-iso.cmd
```

- `out/DEBUG/SMARTDisp.efi`
- `out/RELEASE/SMARTDisp.efi`
- DEBUG/RELEASE ISO 均包含对应的已签名 SMARTDisp。

使用已有签名配置；固件开启 Secure Boot 时仍需信任配置中的证书。

## 测试

```powershell
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-smart-data.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-smartdisp.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-smartdisp.py RELEASE
```

- C 解析/格式测试在 ASan/UBSan 下覆盖完整 128 位最大值、160 位转换边界、短响应、字段偏移及 10,000 组随机日志。
- 10,008 个计数器及其字节换算与 Python 任意精度整数交叉核对。
- QEMU 使用临时空白 NVMe 镜像，实际启动 ISO、列出控制器、读取 Identify/SMART，并测试无效索引。
- 测试不连接宿主物理磁盘。**尚未在真实 NVMe SSD 上验证，请在目标机器复测。**
- QEMU 串口记录：`out/qemu-smart/<目标>/serial.log`。
