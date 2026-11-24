# UEFINVTest.efi

用 UEFI Runtime Services 测试一个独立的非易失变量。
**这是 UEFI NVRAM variable，不是 TPM NV index。**

## 固定测试对象

```text
VariableName = TestVariable-{bd29e016-764e-4a58-9d53-d64fe953edab}
VendorGuid   = bd29e016-764e-4a58-9d53-d64fe953edab
Attributes   = 0x00000007 = NV | BS | RT
Payload      = ASCII "UEFINVTest: Hello NVRAM! v1"，包含结尾 NUL
```

- NV：非易失，正常情况下重启后保留。
- BS：Boot Services 阶段可访问。
- RT：运行时可访问，例如 Linux 的 efivarfs；仍受固件/OS 策略控制。
- 实际变量身份是 **名称 + VendorGuid**；名称里再包含 GUID 是按本工具命名要求添加的。

不使用 `EFI_GLOBAL_VARIABLE` GUID，不修改 `Boot####`、PK、KEK、db、dbx 等系统变量。

## 命令

```text
UEFINVTest.efi           # 默认 read，不写变量；未创建时返回 Not Found
UEFINVTest.efi info      # QueryVariableInfo：显示容量信息，不写变量
UEFINVTest.efi test      # 仅当变量不存在时创建，立即读回校验，保留变量
UEFINVTest.efi read      # 显示属性、大小、hex，以及是否匹配当前内置 payload
UEFINVTest.efi write     # 显式创建/替换测试变量，立即读回校验
UEFINVTest.efi delete    # 仅删除这个名称/GUID，然后确认 Not Found
UEFINVTest.efi --help
```

`test` 遇到已存在变量会拒绝覆盖；需要先读，再决定是否 `write` 或 `delete`。
`write` / `delete` 遇到不是预期 NV|BS|RT 属性的变量会拒绝修改。
测试变量会保留，便于重启后再读；完成后用 `delete` 清理。
同一次执行内读回成功并不能证明非易失持久性，应另行重启验证。
不要对固件 NVRAM 做高频循环写入，也不要将普通 UEFI 变量用于存储明文秘密。

## 后续 adapt

修改 `NvVariable.c` 中：

```c
gNvTestVariableName  // CHAR16 名称
gNvTestVendorGuid    // 真正的变量命名空间 GUID
gNvTestPayload      // 二进制数据；当前是 ASCII 字符串
```

修改 `NvVariable.h` 中的 `NV_TEST_ATTRIBUTES` 可调整属性。
同一变量不能随意更换属性；需要更改属性时，先显式删除旧测试变量。
不要改成系统 GUID 或复用其他软件的重要变量。

`NvVariable.c/.h` 提供可在 `UefiMain()` 中直接调用的接口：

```c
NvTestRead (&Attributes, &Size, &Data); // 成功后调用者 FreePool(Data)
NvTestWrite (FALSE);                  // 创建，不覆盖已有变量
NvTestWrite (TRUE);                   // 允许覆盖同名同 GUID、同属性变量
NvTestVerify ();                     // 全量比较属性、长度、payload
NvTestDelete ();                     // 删除并由调用者决定是否再次读取确认
```

核心 UEFI 函数是 `gRT->GetVariable()`、`gRT->SetVariable()` 和 `gRT->QueryVariableInfo()`。
读取采用先查询长度、再分配缓冲区的方式；上限 64 KiB，遇到大小竞争最多重试三次。
`SetVariable(Name, Guid, 0, 0, NULL)` 用于删除本工具的普通、非认证变量。
这些接口不能直接套用于需要认证更新的 Secure Boot 变量。

## Linux 读取

真机写入后，正常启动同一台机器的 UEFI Linux：

```bash
sudo hexdump -C '/sys/firmware/efi/efivars/TestVariable-{bd29e016-764e-4a58-9d53-d64fe953edab}-bd29e016-764e-4a58-9d53-d64fe953edab'
```

efivarfs 文件前 4 字节是小端属性（这里应为 `07 00 00 00`），后面才是 payload。
名称中的大括号属于变量名称，文件末尾追加的 GUID 才是 efivarfs 的 VendorGuid 后缀。
如果看不到 efivarfs，先检查是否以 UEFI 模式启动，以及 efivarfs 是否已挂载。
建议使用本工具 `delete` 清理，不要对其他固件变量做批量删除或更改 immutable 属性。

## 编译与测试

```powershell
cd D:\Workspace\EDKII
.\build.cmd
.\make-iso.cmd

wsl -d Ubuntu-24.04 --exec bash /mnt/d/Workspace/EDKII/scripts/test-nv-variable.sh
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-uefinvtest.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-uefinvtest.py RELEASE
```

产物：`out/DEBUG/UEFINVTest.efi` / `out/RELEASE/UEFINVTest.efi`，已自动签名并加入相应 ISO。

- 模拟 Runtime Services 在 ASan/UBSan 下测试创建、读取、验证、覆盖保护、删除、异常及长度变化重试。
- QEMU 使用临时的 OVMF VARS 文件，验证真实变量服务的写入/读回、拒绝覆盖、显式覆盖。
- 停止并重新启动 QEMU，使用同一份 VARS 文件，验证数据持久化、再次读取及删除。
- **没有在宿主机或真实主板上写变量。** 实机执行 `test` / `write` 会写入真实 NVRAM，请按需执行。

注意：现有 `run-iso.cmd` 每次都会从模板重置其虚拟 NVRAM，不能用连续运行两次它来验证持久性。
`test-uefinvtest.py` 专门复用同一份 VARS 文件完成跨启动测试，日志在 `out/qemu-nv/<目标>/`。
