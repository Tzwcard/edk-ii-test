# OpalTest.efi — 手动 OPAL 测试

**仅用于已备份、可丢弃的 OPAL NVMe 测试盘。所有写操作逐条确认，不自动串联。**
不是 MainApplication 的生产实现，不接触 TPM/UEFI 变量，不修改旁边的业务模块。

## 先确认设备

```text
OpalTest.efi
OpalTest.efi --list
OpalTest.efi status 0
```

索引来自可见的 NVMe 完整 namespace Block I/O 句柄，**不保证与 NVMeDisp 或 Linux nvme0 的编号相同**。
先核对路径、型号、序列号。多个 namespace 可属于同一物理 SSD；OPAL 安全配置可能影响整个设备。
只接受实际 Discovery 中的 OPAL 1/2 SSC、LockingSupported、MediaEncryption 及有效 ComID，
不会把 EF-only / Pyrite / 未暴露 StorageSecurity 的设备当作 OPAL，也不会回退执行 ATA 操作。
Discovery 固定读取 4096 字节，超出/截断就拒绝操作，不猜测后续数据。

## 设置、改密与验证

```text
OpalTest.efi sid-set 0                 # 当前 SID PIN -> 新 SID PIN
OpalTest.efi sid-set 0 --msid          # 仅在声明初始 SID=MSID 时读取 MSID，单次认证
OpalTest.efi activate 0               # SID 授权激活 Locking SP；当前 SID PIN 复制为 Admin1 PIN
OpalTest.efi admin1-set 0             # Locking SP Admin1 改密，与 SID 独立
OpalTest.efi user1-set 0              # Admin1 设置 User1 PIN、授权两个锁定位、启用 User1
OpalTest.efi user1-change 0           # User1 使用自己的当前 PIN 改密
OpalTest.efi verify-sid 0
OpalTest.efi verify-admin1 0
OpalTest.efi verify-user1 0
```

- 原 PIN、新 PIN、确认新 PIN 均交互输入且不回显，不接受命令行密码。
- 默认 UI 支持 1–32 个可打印 ASCII 字节；`--sedutil` 支持 1–256 个可打印 ASCII 字节。底层 OPAL 函数接收最多 32 字节原始 PIN。不要把真实密码硬编码进源码。
- 不接受空 PIN，不通过反复尝试 SID/Admin1/User1 来猜测身份或口令；一次错误认证也可能计入 TryLimit。
- `--msid` 不是通用默认密码破解。OPAL 2 的初始 SID 可以是厂商自定义值；即使初始 SID=MSID，当前 SID 也可能已经改过。
- 固件 Block SID 生效时，拒绝 SID 操作，不尝试 reset、绕过或清除 Block SID。
- 各 setter 在成功关闭会话后，用新 PIN 开一个会话只做一次认证验证；失败不回滚、不尝试旧密码补救。
- 所有 StartSession 使用 `Write=TRUE`：Opal §4.1.1.2 强制支持 TRUE，但 FALSE（只读会话）可不支持。
  这不等于自动修改硬盘：`verify-*` 只认证并关闭，`global-status` 只 Get；修改方法仍受独立的 `AllowWrites` 和交互确认保护。
- `user1-set` 先设置 PIN，覆盖 Global Range 的 ReadLocked/WriteLocked 两个 ACE 为 `Admins OR User1`，最后启用 User1。
  **会覆盖这两个 ACE 的已有自定义表达式**，只适合测试盘；不修改 Get_All / GenKey / Erase 权限。
  这是多条方法调用，失败可能只完成了一部分，不能宣称原子事务。

## Linux sedutil 兼容口令模式

如果用标准 sedutil 默认模式（未带 `-n`）配置了密码，例如 `sedutil-cli --initialSetup <密码> /dev/nvme0n1`，
EFI 命令必须显式带 `--sedutil`，并在隐藏输入处输入原来的文本密码：

```text
OpalTest.efi verify-admin1 0 --sedutil
OpalTest.efi admin1-set 0 --sedutil
OpalTest.efi user1-set 0 --sedutil
OpalTest.efi global-unlock 0 --sedutil
```

算法为 PBKDF2-HMAC-SHA1，75,000 次迭代，输出 32 字节；salt 是 NVMe Identify offset 4 的
**全部原始 20 字节序列号**，保留尾部空格和 NUL，不使用屏幕显示的裁剪序列号。
派生使用 EDK II `BaseCryptLib.Pkcs5HashPassword()`，不包含 OPAL token 前缀。

在 EFI 内核对实际派生结果：
```text
OpalTest.efi derive-pin 0
```
按同一套设备索引读取 NVMe Identify 原始数据，显示 20 字节 salt hex，隐藏输入密码，
使用与认证路径相同的 `OpalSedutilDerivePin()` 输出 32 字节 PIN hex 和 `d0 20` 开头的 TCG 字节串。
该命令固定使用 sedutil 派生，不需要另加 `--sedutil`；不要求 OPAL/SSCP 支持，不发送任何 OPAL 会话或认证。
**输出 PIN 本身就是可用于认证的凭据。只供本机调试，不要分享输出或截图；结束后清屏。**
输入缓冲区会清零，但控制台输出无法由程序可靠擦除。

`--sedutil` 同时用于当前密码和新密码；不是只给旧密码兼容，也不支持在同一改密命令中从 raw 迁移到派生模式。
不带参数仍直接发送原始 ASCII PIN，适合本工具原始模式或 sedutil `-n` 配置的凭据。
不存在自动识别、重试或失败后回退。MSID 始终是原始字节：`sid-set 0 --msid --sedutil` 只派生新口令。
可组合 `revert 0 --destroy-data --sedutil`，但仍是 **SID 认证的破坏性 Revert，不是 PSID**，原有确认要求不变。
可选参数放在命令和索引之后，两个参数顺序不限；重复或无关参数被拒绝。

**这只修复密码编码兼容性，不会清除已有的 `0x12 AUTHORITY_LOCKED_OUT`。**
不要对已锁定身份循环尝试，也不要把 SID Revert 当作 PSID 恢复入口。
参数依据为 [sedutil commit 5af6caf](https://github.com/Drive-Trust-Alliance/sedutil/tree/5af6caf6f73050dc689ade3cb63678d1bf81ea39)
的 `DtaHashPwd.cpp/.h`、`DtaStructures.h` 和 `linux/DtaDevLinuxNvme.cpp`。

## “全盘加密/解密”对应的 Global Range 测试

OPAL 的介质加密不是主机逐扇区“加密/解密”开关。本例明确测试 **Global Range 的访问锁定**：

```text
OpalTest.efi global-status 0          # Admin1 认证，读取四个 Global Range 状态
OpalTest.efi global-lock 0            # Admin1: Read/WriteLockEnabled=1，Read/WriteLocked=1
OpalTest.efi global-unlock 0          # Admin1: Read/WriteLockEnabled=1，Read/WriteLocked=0
OpalTest.efi user1-lock 0             # User1: 只更新 Read/WriteLocked=1
OpalTest.efi user1-unlock 0           # User1: 只更新 Read/WriteLocked=0
```

Admin1 的 Global Range 修改后会在同一会话 Get 读回四个状态并比较。
User1 的操作只核对方法返回状态，不额外授予查询所有列的权限；之后用 Admin1 `global-status` 检查。
本例不会写入测试扇区、覆盖分区或生成新媒体密钥。

**Global Range 测试不等于证明每个 LBA 都锁定/解锁**：设备可能存在独立 Locking Range、多个 namespace 或其他配置。
程序不枚举或修改所有非 Global Range，不宣称验证了所有 namespace 的覆盖范围、控制器缓存或实际 NAND 密文。
锁定测试可能让系统/文件系统无法读写；从独立 USB/ISO Shell 启动，关闭目标盘文件，先保证你知道 Admin1 PIN。
解锁不会把存储介质变成明文，也不是 Revert。

## 保留数据的“取消”

### MBR Shadow 与范围解锁是两个独立状态

`status` 会显示 `MBREnabled`、`MBRDone` 和 `ShadowActive`。当 Enable=1、Done=0 时，
即使 Global Range 已解锁，盘头读请求仍会映射到 Shadow MBR，可能看不到真实分区和系统。

```text
OpalTest.efi mbr-done 0 --sedutil
OpalTest.efi status 0
```

`mbr-done` 使用 Admin1 认证，要求 `WRITE` 和序列号确认，只设置 `MBRControl.Done=TRUE`，
不修改 Enable、DoneOnReset、Range 锁位、ACE 或媒体密钥，不写入 Shadow MBR 镜像或分区表。
关闭会话后通过 Discovery 检查 Shadow 不再生效。它不等于解锁、不保证系统/分区仍完整，
也不修复先前安装/格式化造成的修改。成功后在同一次启动内引导系统；相应 reset 可能清除 Done。
规范依据：TCG Core §5.7.2.5、Opal §4.3.5.3。现有 `global-unlock/user1-unlock` 不自动设置 Done。

### 其他非擦除操作

`load-pba <index> [--sedutil]` 用 Admin1 将应用文件系统根目录的 `\OpalPba.img` 写入 Shadow MBR 字节表。
它覆盖已有 PBA，但不写真实用户 LBA，不自动启用 Shadow 或清除 Done；需 `WRITE` 和序列号确认。
镜像生成、静态提示语义和启动限制见 [OpalPba/README.md](../OpalPba/README.md)。

```text
OpalTest.efi locking-disable 0        # Admin1: 解锁 Global Range，并将两个 LockEnabled 位清零
OpalTest.efi user1-disable 0          # Admin1: User1.Enabled=FALSE，不擦除 PIN、不删除 ACE
OpalTest.efi sid-set 0                # 将“新 PIN”输入为你知道的原 SID PIN，即手动恢复原口令
OpalTest.efi admin1-set 0             # 同理恢复你知道的原 Admin1 PIN
```

没有虚构“disable SID/Admin1”或“删除管理员密码”的通用命令。
保留数据的上述操作**不等于恢复出厂**；不会关闭 Locking SP 生命周期，不恢复原 ACE，不擦除口令值。
也不会自动读取/保存原密码（OPAL PIN 不能当作可读取的备份数据）。

## 恢复出厂：独立破坏性命令

```text
OpalTest.efi revert 0 --destroy-data
```

这是 **SID 认证的 Admin SP Revert**，不是错误处理，不是解锁，不是普通取消口令：

- 必须有 `--destroy-data`，交互输入 `ERASE`，再输入目标序列号，最后输入当前 SID PIN。
- SID/Admin1/User1 等个性化设置可能被清除；活动 Locking SP 下的数据及媒体密钥可能被清除，导致全部用户数据丢失。
- Revert 后 SID 是否恢复成 MSID 取决于设备声明的 C_PIN_SID revert 行为，不能保证所有设备都相同。
- 不实现 PSID 回退，不调用 GenKey、Sanitize 或 Format，不改变 ActiveDataRemovalMechanism。
- 不自动执行 `KeepGlobalRangeKey` 的 RevertSP：规范只承诺保留 Global Range 密钥，其他 range 仍可能被擦除，不能包装成“无损取消所有密码”。
- 成功 Revert 会自行终止会话；失败/超时可能结果未知，**不自动重发 Revert，也不自动 reset 或断电**。
  必须人工检查；返回成功也不保证固件所有后台数据清理已完成。

所有其他写命令要求输入 `WRITE` 和目标序列号。输入不匹配或 Esc 就结束，未提供 `--yes` 或一键跑全部测试。
即使取消，之前已完成的独立命令也不会撤销。

## 实现和复用

- `OpalTest.c`：参数分派、NVMe 身份核对、只读 Discovery、对齐的 SSCP 转接、口令交互和写入确认。
- `OpalOps.h/.c`：可独立调用的改密、激活、User1 启停、Global Range、Revert 测试函数。
- 所有修改函数要求 `Device.AllowWrites=TRUE`；Revert 另要求 `AllowErase=TRUE`。默认均为 FALSE。
- 使用 EDK II `OpalStartSession/OpalSetPassword/OpalActivateLockingSp/OpalSetLockingRange/OpalDisableUser`。
  User1 Enable/锁定位 ACE 与 Global Get 用公开的 `TcgStorageCoreLib` 编码，检查传输、ComID、会话 ID 和 MethodStatus。
- 没有使用 EDK `OpalUtilSetUserPassword` 的认证回退，也没有调用同时授予 User1 GenKey 权限的便捷接口。
- 输入数组与传输对齐缓冲会清零；**不保证上游 OPAL 库的内部栈副本/寄存器已擦除**。
  本例无加密认证通道，不适用于抵御恶意固件或物理总线窃听，不能当成生产级凭据保护方案。

## 已核对的依据

[TCG Opal SSC v2.30](https://medias.cdn.trustedcomputinggroup.org/production/TCG_Storage_Opal_SSC_v2_30_pub_f4e29a2c4b.pdf)：

- §2.4：介质加密；§3.1.1：Discovery 及实际 SSC 标识。
- §4.2.1.6 / §4.3.1.6：Admin/Locking SP ACE 预配置及 PIN/锁定位权限。
- §4.3.5.2：Locking 表和 Global Range。
- §5.1.1.2：Activate 将 SID PIN 复制到激活 SP 的 Admin1。
- §5.1.2.2：Revert 的擦除和恢复出厂效果；§5.1.2.2.1：SID PIN 恢复行为。
- §5.1.3.2–3：KeepGlobalRangeKey 并不是保留所有 range 数据。

同时核对当前固定版本 EDK II 的 `SecurityPkg/Include/Library/TcgStorageOpalLib.h` 和
`TcgStorageOpalCore.c`。本例不修改 submodule。

## 验证范围

```powershell
.\make-iso.cmd
.\make-iso.cmd RELEASE
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-opal-ops.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-sedutil-pin.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-opaltest.py
```

Host 测试使用真实 EDK Core 编解码和模拟 OPAL 方法，验证 SID/Admin1/User1 隔离、PIN/ACE 顺序、
锁定读回、错误处理、收包轮询、写入/擦除开关和清零。QEMU 只验证帮助、参数、无 OPAL 时拒绝写入。
派生测试编译实际 helper 和 EDK PKCS5 wrapper、链接 host OpenSSL，与 Python hashlib 比较；不代替真实 SSD 互操作验证。
此 host 派生测试额外需要 Ubuntu `libssl-dev`；EFI 构建使用固定的 EDK OpenSSL 子模块，不链接 host OpenSSL。
**没有在真实 SSD 上执行改密码、锁定或擦除；QEMU 不模拟真实 OPAL 功能。**
已签名产物 `out/<目标>/OpalTest.efi` 加入对应 Shell ISO；不设置自动启动脚本。
