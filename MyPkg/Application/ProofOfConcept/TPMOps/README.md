# TPMOps.efi — TPM 2.0 wrap / unwrap 示例

这里的 wrap / unwrap 是 **TPM sealed-data object 的密封/解封**，不是通用的大文件加密。
最多密封 128 字节，适合演示保护 32 字节密钥。当前示例数据是公开的内置字符串：

```text
TPMOps example sealed payload v1
```

包含结尾 ASCII NUL。这不是一个真实秘密；正式使用应从运行时获取数据，而不是把密码硬编码进 EFI。
明文和对象授权口令都不写入 UEFI 变量；保存的是 TPM 生成的 public/private blob。

## 命令

```text
TPMOps.efi                 # 帮助，不写变量，不创建 TPM 对象
TPMOps.efi wrap            # 新建 NV|BS 变量；交互设置并确认对象口令
TPMOps.efi wrap --RT       # 新建 NV|BS|RT 变量，供 OS 读取 blob
TPMOps.efi --RT wrap       # 等效写法
TPMOps.efi unwrap          # 输入对象口令，解封并显示明文 hex（仅示例调试用途）
TPMOps.efi info            # 查看记录版本、属性和 blob 长度，不访问 TPM
TPMOps.efi dump            # 导出记录 hex 到控制台，不解封
TPMOps.efi delete          # 只删除此 UEFI 变量，不清 TPM 或改变 hierarchy
```

对象口令为 **8–32 个可打印 ASCII 字节**，不回显、不接受命令行口令、不保存到变量。
Esc 取消；wrap 要求输入两次。错误口令不自动重试，可能增加 TPM 全局防字典攻击计数。
不要在实机上反复猜密码，也不要通过 TPM Clear 或修改 lockout 配置来绕过错误。

`wrap` 不覆盖已有变量，即使这次指定的 `--RT` 不同也会拒绝。
如需重新测试，先读取/导出原记录，再明确 `delete`，然后重新 wrap。
`--RT` 仅用于新建变量时选择属性；unwrap/info/dump/delete 使用已存属性，无需该参数。

## 变量名称与属性

```text
VariableName = TPMTest-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}
VendorGuid   = a4e9a95e-5fde-4bcb-9f44-2b487f03395d
Default      = 0x00000003 = NON_VOLATILE | BOOTSERVICE_ACCESS
With --RT    = 0x00000007 = NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS
```

GUID 为本示例固定选择的随机 GUID，**不是每次执行都生成新名字**，因此可跨启动找到同一记录。
名称中带 GUID 是命名要求；真正的 UEFI 变量身份仍是 Name + VendorGuid。
BS-only 变量在 ExitBootServices 后不可按正常运行时接口读取，所以 Linux 调试需要 wrap 时添加 `--RT`。
不使用系统 GUID，不修改 PK/KEK/db/dbx/Boot####，也不创建 TPM NV index。

## TPM 流程

通过 `EFI_TCG2_PROTOCOL.GetCapability()` 检查 TPM，再使用 `SubmitCommand()`：

```text
wrap:
  CreatePrimary(owner, 固定 RSA 存储主密钥模板)
  Create(keyedHash sealed object, 数据 + 对象授权)
  FlushContext(主密钥)
  重新 CreatePrimary / Load / Unseal 验证
  全部成功后才 SetVariable 保存 blob，并读回比较

unwrap:
  GetVariable + 验证记录长度/版本
  CreatePrimary(owner, 同一模板)
  比较 Parent Name，确认主密钥一致
  Load(public/private blob)
  Unseal(对象授权)
  FlushContext(子对象、主密钥)
```

- 固定模板在同一 TPM owner/storage seed 下可重建同一主密钥，**不用持久句柄**。
- TPM 清除、更换或 owner seed 改变后，原 blob 通常不可恢复；删除变量也不等于清除 TPM。
- 当前仅支持 **空 owner hierarchy authorization**。如 owner auth 非空会报错，不修改或重置 owner auth。
- Parent：RSA-2048、SHA256 Name、AES-128-CFB，attributes=`0x00030472`。
- Sealed object：keyedHash、SHA256 Name、fixedTPM/fixedParent/userWithAuth，attributes=`0x00000052`。
- 对象的 `noDA` 未设置，保留防字典攻击机制；parent 的 `noDA` 不会替代子对象授权。
- **没有 PCR policy**，不依赖 PCR0/7，不用于替代现有 LUKS TPM 策略。后续如需 PCR policy 要实现 policy session，而不是仅修改 NVRAM 属性。
- 只对 TPM 明确返回的 RETRY / YIELDED / TESTING 警告做有限重试，最多 50 次、间隔 100ms；不重试授权失败、锁定或传输错误。
- 仅清理本次创建的 transient handles，不枚举/驱逐系统已有对象。

### 示例的安全边界

本例使用 `TPM_RS_PW` 密码会话，**没有启用 salted HMAC session 或 TPM 参数加密**。
所以它演示的是 TPM blob 的静态保护与授权流程，不能防御 TPM 总线窃听或恶意固件。
unwrap 明确将明文输出到控制台，仅供调试；用于真实密钥时必须移除此输出。
内部命令缓冲区和口令/解封数据会覆盖清零，但不宣称能消除所有编译器副本、寄存器或硬件残留。
普通 NVRAM 变量不是访问控制边界；RT 只是可见性，不是加密或授权策略。
Frame header 是格式元数据，不是应用来源签名；TPM 会在 Load/Unseal 时验证 private blob 的完整性及授权。

## 后续 adapt 的位置

- `TPMOps.c`：变量 Name/GUID、`mExampleSecret`、命令入口、口令交互、NV/RT 存储。
- `TpmSeal.h/.c`：可复用的 wrap/unwrap 和二进制记录接口。

```c
TpmOpsWrap (Tcg2, Secret, SecretSize, Auth, AuthSize, &Blob, &TpmRc);
TpmOpsUnwrap (Tcg2, &Blob, Auth, AuthSize, Plaintext, &PlaintextSize, &TpmRc);
TpmOpsEncode (&Blob, Record, &RecordSize);
TpmOpsDecode (Record, RecordSize, &Blob);
TpmOpsWipe (SensitiveBuffer, Size);
```

底层 API 接受 1–32 个原始授权字节；UI 额外要求至少 8 字节。
底层不会主动清理调用者的输入 secret/auth，请在使用后自行清零。
TPM 字段采用大端线序，变量记录头采用显式小端，不直接持久化 C struct。

## 记录格式（v1）

| 偏移 | 内容 |
| --- | --- |
| 0 | 8 字节 ASCII `TPMWRAP1` |
| 8 | LE32 version=1 |
| 12 | LE32 header size=40 |
| 16 | LE32 flags=1（对象授权） |
| 20 | LE32 parent template ID=1 |
| 24 | LE32 ParentName 长度（包含 TPM2B 的 2 字节长度） |
| 28 | LE32 public blob 长度（完整 TPM2B_PUBLIC） |
| 32 | LE32 private blob 长度（完整 TPM2B_PRIVATE） |
| 36 | LE32 reserved=0 |
| 40… | ParentName、public、private，依次连续保存 |

private blob 是 TPM 保护的密文，不是可直接使用的私钥。记录不包含对象口令和明文。
public/private 可以复制，但固定 TPM/父对象绑定意味着不能拿到另一台 TPM 直接解封。

## Linux 调试（必须先 wrap --RT）

必须是**原实机、原 TPM**，而不是 Windows WSL 的另一个 TPM。示例默认不绑定 PCR，因此启动 Linux 不会仅因 PCR 变化而拒绝解封。

导出脚本只读取记录并拆分文件，**不会发送 TPM 命令**：

```bash
sudo python3 scripts/export-tpmops.py --out /root/tpmops-export
```

默认读取：

```text
/sys/firmware/efi/efivars/TPMTest-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}-a4e9a95e-5fde-4bcb-9f44-2b487f03395d
```

efivarfs 前 4 字节是属性，导出脚本会去掉它们并验证记录。
输出 `public.bin`、`private.bin`、`parent.name`、`record.bin`、`metadata.json`；不会导出口令或明文。
也可用 `--record path/to/record.bin` 解析不含 efivarfs 属性前缀的原始记录。
已有输出文件时会拒绝覆盖。

使用 tpm2-tools 重建同一个父对象并解封（会在目标 TPM 上创建临时对象）：

```bash
# 或在当前用户的临时内存目录中完成导出和解封：
umask 077
WORK=$(mktemp -d "${XDG_RUNTIME_DIR:-/dev/shm}/tpmops.XXXXXX")
sudo cat '/sys/firmware/efi/efivars/TPMTest-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}-a4e9a95e-5fde-4bcb-9f44-2b487f03395d' \
  > "$WORK/efivar.bin"
python3 scripts/export-tpmops.py --efivar "$WORK/efivar.bin" --out "$WORK/blob"

# 口令文件是原始 ASCII 字节，不要添加换行，也不要把口令写进命令历史。
IFS= read -r -s -p 'Object authorization: ' AUTH
printf '\n'
printf '%s' "$AUTH" > "$WORK/object.auth"
unset AUTH

sudo tpm2_createprimary -T device:/dev/tpmrm0 \
  -C o -g sha256 -G rsa2048:aes128cfb -a 0x30472 -c "$WORK/parent.ctx"
sudo tpm2_load -T device:/dev/tpmrm0 \
  -C "$WORK/parent.ctx" -u "$WORK/blob/public.bin" -r "$WORK/blob/private.bin" \
  -c "$WORK/sealed.ctx"
sudo tpm2_unseal -T device:/dev/tpmrm0 \
  -c "$WORK/sealed.ctx" -p "file:$WORK/object.auth" -o "$WORK/unsealed.bin"
```

在安全临时目录中进行，结束后清理授权和明文文件；真实秘密不要放进 Git、日志或公共目录。
Linux 通过 `/dev/tpmrm0` 资源管理器操作，不要照搬测试脚本中针对私有 swtpm 的“清理全部 transient slots”命令到实机。
原 owner auth 非空、内核权限或 TPM lockout 等问题会使操作失败；不要通过清 TPM 修复。

## 构建与验证

已接入 DEBUG/RELEASE 编译、自动签名和 ISO。产物：`out/<目标>/TPMOps.efi`。

```powershell
.\make-iso.cmd
.\make-iso.cmd RELEASE
wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-tpmops-wire.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-tpmops.py
```

模拟集成测试额外依赖 Ubuntu 的 `swtpm tpm2-tools` 软件包。

- Host ASan/UBSan：格式边界、授权错误不重试、busy 有限重试、清理和 10,000 组异常记录/响应。
- 独立 swtpm + OVMF：NV|BS 和 NV|BS|RT、密封前解封校验、变量读回、拒绝覆盖、不回显授权。
- 重启 QEMU 和 swtpm、复用永久 TPM state 与 VARS 后，原 blob 仍可解封。
- Linux tpm2-tools 成功重建父对象、加载 UEFI 生成的 public/private，并解封出相同内容。
- 错误授权仅在私有模拟 TPM 上测试，失败时没有输出明文；不修改实际 TPM 的 DA 配置。
- 日志和导出测试记录位于 `out/qemu-tpmops/<目标>/`。明文测试数据/授权均为公开的模拟测试值。
- **没有在真实 TPM 上创建对象或写实机 NVRAM；需在目标硬件复测。**

不会修改 `SealSecret-DoNotShipToUEFI/SealSecret` / `Unlocker/Unlocker` 应用草稿，也不会改变硬盘安全状态。
