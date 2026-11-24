# EDK II UEFI 应用开发环境

已配置路线：**Windows 编辑代码 → Ubuntu-24.04 WSL / GCC 编译 → QEMU + OVMF 验证**。
无需启动 Docker，也无需打开 Visual Studio 开发者命令行。

## 0. 克隆仓库与首次准备

本仓库只保存自己的应用、脚本和测试。`edk2/` 是 Git submodule，固定在
`edk2-stable202608` 对应提交 `2970e5699ba6267f3384ffab20f96647578aebc8`，不是拷贝进主仓库的源码。

```bash
git clone <你的远程仓库URL> EDKII
cd EDKII

# Ubuntu / WSL 依赖。初始化脚本只下载实际需要的上游子模块。
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential git python3 python3-venv uuid-dev nasm acpica-tools \
  openssl sbsigntool qemu-system-x86 ovmf xorriso dosfstools mtools
bash scripts/setup.sh

# 配置自己的签名材料路径；此本机文件已被 .gitignore 排除。
cp signing.local.sh.example signing.local.sh
# 编辑 signing.local.sh，填写实际 EFI_SIGN_KEY / EFI_SIGN_CERT 路径。
bash scripts/build.sh DEBUG
```

也可使用 `git clone --recurse-submodules`，但它会下载 EDK II 中很多本工程不需要的依赖。
从 remote 拉取了 submodule 指针更新后，重新执行 `bash scripts/setup.sh`。
脚本按主仓库记录的提交更新 edk2，不使用浮动的最新分支；存在本地冲突时由 Git 报错，不强制丢弃修改。

`Build/`、`Conf/`、`out/`、日志、磁盘镜像、私钥/证书文件、本机签名配置和编辑器缓存不纳入版本管理；已有文件无需删除。
私钥必须留在仓库外，不能依赖 `.gitignore` 替代密钥保管。不要硬编码真实密码到应用源码后提交。

首次 push：

```bash
git remote add origin <你的远程仓库URL>
git push -u origin main
```

## 1. 一键编译和运行

在 Windows PowerShell 中：

```powershell
cd D:\Workspace\EDKII
.\build.cmd                  # DEBUG
.\build.cmd RELEASE          # RELEASE
.\run-qemu.cmd               # 验证 DEBUG
.\run-qemu.cmd RELEASE       # 验证 RELEASE
```

`.cmd` 入口仅对它启动的 PowerShell 进程指定 `ExecutionPolicy Bypass`，不修改系统执行策略。
编译和运行不需要管理员权限。

输出文件：

| 路径 | 用途 |
| --- | --- |
| `out/DEBUG/HelloWorld.efi` | DEBUG 版 X64 UEFI 可执行程序 |
| `out/RELEASE/HelloWorld.efi` | RELEASE 版 X64 UEFI 可执行程序 |
| `out/esp/EFI/BOOT/BOOTX64.EFI` | 最近一次编译的程序，按可移动介质默认启动路径放置 |
| `Build/MyPkg/DEBUG_GCC/X64/HelloWorld.debug` | DEBUG 版 ELF 调试符号 |
| `out/qemu/DEBUG/serial.log` | DEBUG 虚拟机串口日志 |
| `out/qemu/RELEASE/serial.log` | RELEASE 虚拟机串口日志 |

`.efi` 不是 Windows 用户态程序，不能在 Windows 中直接双击运行。

## 2. 从哪里开始写代码

```text
EDKII/
├── MyPkg/
│   ├── MyPkg.dsc                         # 平台：架构、库映射、待编译模块
│   ├── Application/             # NVMe/、Test/、SealSecret-DoNotShipToUEFI/、Unlocker/、Util/
│   └── ProofOfConcept/          # 独立的调研/验证代码，与 Application 同级
│       ├── HelloWorld.c                 # 从这里开始修改；入口 UefiMain
│       └── HelloWorld.inf               # 模块：源文件、依赖包、库
├── edk2/                                # 固定提交的上游 Git submodule
├── scripts/                             # WSL 环境、初始化、编译、虚拟机脚本
├── Conf/                                # 自动生成的 EDK II 配置
├── Build/                               # 自动生成的中间产物
└── out/                                 # 汇总产物和测试日志
```

修改 `MyPkg/ProofOfConcept/HelloWorld/HelloWorld.c` 后重新执行 `build.cmd` 即可增量编译。
`Application/` 与 `ProofOfConcept/` 仅在源代码目录上分离；仍统一由 `MyPkg.dsc` 构建、签名并打包到同一个 Shell ISO，不需要独立构建命令。
示例使用 `Print(L"...\r\n")` 输出 UTF-16 字符串，并在 5 秒后返回调用者。
这是 `UEFI_APPLICATION`，不是 DXE 驱动，也不是完整主板固件。

新增应用时：

1. 复制 `HelloWorld` 目录，修改文件名和 `.inf` 中的 `BASE_NAME`、`FILE_GUID`、`[Sources]`；每个模块使用独立 GUID，可通过 PowerShell `[guid]::NewGuid()` 生成。
2. 将新 `.inf` 路径加入 `MyPkg/MyPkg.dsc` 的 `[Components]`。
3. 如引入新库，在 `.inf` 的 `[LibraryClasses]` 声明，在 `.dsc` 中映射对应实现；协议/GUID 按 EDK II 规则在 `.inf` 中声明。
4. 编译后的新应用位于 `Build/MyPkg/<DEBUG或RELEASE>_GCC/X64/<BASE_NAME>.efi`。一键脚本会收集并签名当前十二个应用；增加/更名应用后同步修改 `build.sh` 和 ISO 打包列表。

可使用 VS Code、Visual Studio 或其他编辑器直接编辑 Windows 路径下的源码。
本次没有配置 Visual Studio 原生 MSVC 编译，也没有安装 Docker 镜像。

## 3. WSL 中使用原生 EDK II 命令

进入 `Ubuntu-24.04` 后：

```bash
cd /mnt/d/Workspace/EDKII
source scripts/env.sh
build -p MyPkg/MyPkg.dsc -a X64 -t GCC -b DEBUG -n 8

# 或使用自动收集产物的脚本
bash scripts/build.sh DEBUG
bash scripts/run-qemu.sh DEBUG
```

该版本的工具链标签是 **`GCC`**，不是旧教程中的 `GCC5`。
`env.sh` 配置独立的 `WORKSPACE`、`PACKAGES_PATH` 和 `CONF_PATH`，无需把自己的包放入上游仓库。
建议在普通 Bash 环境中 source；上游环境脚本不兼容 `set -u`。
可通过 `JOBS=4 bash scripts/build.sh` 调整编译并发度，默认 8。

## 4. 虚拟机验证如何工作

`run-qemu.cmd` 使用 Ubuntu 提供的 OVMF 固件和 QEMU 的 TCG 软件模拟，不依赖 KVM 或图形界面。
每个构建目标使用独立的 `out/qemu/<目标>/esp` 测试目录和 NVRAM 副本。
它不挂载宿主物理磁盘，不修改真实 UEFI 启动项，虚拟机网络被关闭。

测试会：

1. 将指定目标的 `.efi` 复制成测试盘的 `EFI/BOOT/BOOTX64.EFI`。
2. 自动启动 UEFI 应用，将控制台输出保存到 `serial.log`。
3. 在 30 秒后终止虚拟机；此时出现 `terminating on signal 15 ... (timeout)` 属于预期行为。
4. 检查日志中是否出现 `UEFI application is running successfully (X64).`，只有找到才报告 PASS。

修改示例输出后，需要同步调整 `scripts/run-qemu.sh` 中的检查字符串。
这是一项启动冒烟测试，不等价于所有真机兼容性验证。

## 5. 版本和重建环境

- EDK II：`edk2-stable202608`
- 固定提交：`2970e5699ba6267f3384ffab20f96647578aebc8`
- WSL：Ubuntu 24.04，GCC 13.3，Python 3.12，NASM 2.16
- 运行验证：Ubuntu 软件包提供的 QEMU 8.2 + OVMF 2024.02（不是用本次 EDK II 源码重编译的 OVMF）

若重建 WSL 环境，先在 Ubuntu 中安装依赖：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential git python3 python3-venv uuid-dev nasm acpica-tools openssl sbsigntool \
  qemu-system-x86 ovmf
cd /mnt/d/Workspace/EDKII
bash scripts/setup.sh
bash scripts/build.sh
```

`setup.sh` 使用 Git submodule 按主仓库固定提交初始化 edk2，不强制覆盖已有本地改动。
当前初始化 BaseTools Brotli、MdePkg MipiSysT、MdeModulePkg Brotli 和 SecurityPkg libspdm 四个所需子模块；这是应用开发环境，不是全平台固件构建环境。
未来构建 OVMF、加密模块或其他平台时，再按所需平台初始化其余子模块。
保留原有上游仓库，不要用清理命令覆盖自己未提交的改动。

## 6. 注意事项

- 当前仅配置并验证 **X64**，不包括 IA32、AArch64。
- `out` 中的应用和 ISO 内 Shell 现已自动使用现有 MOK 证书签名；`Build` 保留未签名原始产物。固件仍需信任该证书，详见第 11 节。不要覆盖系统 EFI 分区或 Windows Boot Manager。
- 需要 U 盘启动时，可将 `out/esp` 内的 `EFI` 目录复制到兼容的 FAT32 U 盘根目录；复制前检查不要覆盖已有文件。未执行任何真机部署。
- 编译日志中可能出现缺少 `*.pdb` 的 `Error 1 (ignored)`：这是上游通用复制规则尝试复制 MSVC 符号；GCC 产出的是 `.debug`，若构建以 `- Done -` 结束则该提示不影响产物。
- 默认 `DebugLib` 为 Null 实现，示例通过 `Print` 输出；需要 `DEBUG()`/断言日志时另行映射合适的 DebugLib。
- 源码位于 `/mnt/d`，便于 Windows 编辑；大型固件全量编译若遇到 I/O 性能问题，可后续迁移到 WSL ext4 工作区，目前示例无需迁移。

## 7. 本次验证

2026-11-17 已完成：

- BaseTools 编译成功，附带 **301 项测试通过**（`basetools-build.log`）。
- X64 DEBUG 和 RELEASE 构建成功，均识别为 PE32+ EFI application。
- DEBUG 和 RELEASE 均在 QEMU / OVMF 中实际启动并打印成功标记（对应 `serial.log`）。

## 8. Bootable ISO with an interactive UEFI Shell

An actual bootable ISO is available at:

```text
D:\Workspace\EDKII\out\uefi-shell-x64-DEBUG.iso
```

It contains an EDK II Shell built from the same pinned source tree, plus
`HelloWorld.efi`. The default boot application is now the **shell**, at the
standard X64 path `EFI/BOOT/BOOTX64.EFI` (not `efiboot64.efi`).
The ISO uses a FAT El Torito EFI boot image. No Linux OS is included or needed.

### Rebuild and launch

```powershell
cd D:\Workspace\EDKII
.\make-iso.cmd           # Rebuild the application, shell, and DEBUG ISO
.\run-iso.cmd            # Interactive QEMU session in your terminal

# Optional: package the RELEASE version of HelloWorld instead
.\make-iso.cmd RELEASE
.\run-iso.cmd RELEASE
```

Press **Ctrl+A, then X** to quit QEMU. `exit` leaves the shell and returns to the
VM firmware; it does not necessarily close QEMU. Unlike `run-qemu.cmd`, this
interactive launcher has no automatic timeout.
Do not run two sessions with the same target simultaneously: they share its
test NVRAM file. The launch script resets that VM's NVRAM from the template.

In VMware / VirtualBox / another hypervisor, attach the ISO to the virtual
CD/DVD drive, select **X64 UEFI firmware** (not legacy BIOS), and boot from CD/DVD.
The ISO is now signed with the configured MOK certificate; Secure Boot requires
enrollment into the VM firmware db (MOK alone is not firmware trust). The ISO is
UEFI-only; it is not a BIOS-bootable or hybrid USB image. A VM with no additional
disks is sufficient and safest for initial experimentation.

### Commands at the shell prompt

```text
Shell> help
Shell> ver
Shell> map -r
Shell> fs0:
FS0:\> ls
FS0:\> HelloWorld.efi
FS0:\> help cp
FS0:\> exit
```

Filesystem numbering can vary when other disks are attached. If `fs0:` does
not contain `HelloWorld.efi` and `startup.nsh`, use `map -r` and try `fs1:`, etc.
Standard level 1/2/3 commands are built in: directory/file operations, command
help, scripts, environment variables, and application launching. Optional
network/debug/driver profiles are intentionally omitted to keep this small.
This is **UEFI Shell**, not Bash, PowerShell, or a Linux command environment.

The ISO is read-only. Rebuild it after changing your application. For file-write
experiments, attach a separate disposable FAT disk; do not attach your Windows
system/EFI partitions. Commands can modify any writable disk exposed to the VM.

`iso/startup.nsh` controls the welcome commands. It deliberately leaves you at
an interactive prompt rather than automatically launching the application.
`scripts/make-iso.sh` packages the applications listed below; add further apps to both
the ISO staging tree and its FAT boot image when extending it.

### ISO tooling and verification

Additional Ubuntu dependencies (already installed here):

```bash
sudo apt-get install -y --no-install-recommends xorriso dosfstools mtools
```

The builder uses ordinary image files with `mkfs.vfat` / mtools, not physical
block devices or privileged loop mounts. The shell configuration is
`MyPkg/Shell.dsc`, separate from your application platform.

Automated verification of the actual ISO:

```powershell
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-iso.py
```

Verified on 2026-11-17: ISO boot, shell startup script, serial command input,
`map -r`, `fs0:`, `ls`, and successful execution of `HelloWorld.efi`.
Transcript: `out/qemu-iso/DEBUG/smoke-test.log`.
This smoke test assumes the supplied example output and a VM with only this CD.

## 9. TCG OPAL 检测工具

已新增 `TCGOpalCheck.efi`，并加入 DEBUG/RELEASE 编译和 ISO 打包。
在 UEFI Shell 中切换到工具所在文件系统后运行：

```text
TCGOpalCheck.efi
```

产物：`out/DEBUG/TCGOpalCheck.efi`。
源码、函数说明、结果解释和测试范围见
[`MyPkg/ProofOfConcept/TCGOpalCheck/README.md`](MyPkg/ProofOfConcept/TCGOpalCheck/README.md)。

目前 ISO 包含 `HelloWorld.efi`、`PCRRead.efi`、`TCGOpalCheck.efi`、`AtaPassUtil.efi`、`SMARTDisp.efi`、`UEFINVTest.efi`、`TPMOps.efi`、`NVMeDisp.efi`、`NVMeResetTest.efi`、`TestMain.efi`、`OpalTest.efi`、`OpalPba.efi`，以及用于 Shadow MBR 的 `OpalPba.img`。
OPAL 工具只做 Level 0 Discovery 读取；未知状态不等于不支持。
已验证 QEMU 中无 OPAL 接口的分支，未验证真实 OPAL SSD。
不要并发执行多个编译/ISO 构建脚本：上游环境脚本会写共享的 `Conf/BuildEnv.sh`。

## 10. AtaPassUtil — EF / ATA Security 操作接口

新增 `out/DEBUG/AtaPassUtil.efi` 和 `out/RELEASE/AtaPassUtil.efi`，DEBUG ISO 已包含。
默认运行 **只读枚举和状态查询，不执行任何 SendData 操作**。

按需在 `MyPkg/ProofOfConcept/AtaPassUtil/Main.c` 的 `UefiMain()` 中调用：
状态、设置 User/Master 密码、解锁、禁用密码、冻结、擦除准备、普通/增强擦除接口。
所有修改接口需要显式启用写开关，擦除还需要额外确认参数及准备步骤。
**不要在默认设备枚举循环中直接加入批量写操作，不要在重要数据盘上实验设置密码或擦除。**

函数签名：`MyPkg/ProofOfConcept/AtaPassUtil/AtaPassUtil.h`。
完整调用说明、密码编码约定及验证范围：
[`MyPkg/ProofOfConcept/AtaPassUtil/README.md`](MyPkg/ProofOfConcept/AtaPassUtil/README.md)。
修改操作只使用模拟接口测试，未在真实 SSD 上执行。

## 11. 自动签名：复用驱动 MOK 证书

已将 `scripts/sign-efi.sh` 接入编译和 ISO 打包。签名路径来自不提交的 `signing.local.sh`，或环境变量：

```bash
cp signing.local.sh.example signing.local.sh
# 编辑其中的 EFI_SIGN_KEY / EFI_SIGN_CERT 为实际 Linux/WSL 路径。
```

私钥 `MOK.priv` 与 PEM 证书 `MOK.crt` 留在工程之外；DER 证书 `MOK.der` 用于固件/MOK 导入。
脚本只比较导出的公钥，不输出私钥。当前机器的原签名路径已保存在本机配置，原有签名流程无需改变。
`signing.local.sh` 是由当前用户控制的 Bash 配置文件，不要 source 来历不明的配置。

### 日常使用

```powershell
.\build.cmd             # DEBUG 应用：编译 → 签名 → 验签 → 收集
.\build.cmd RELEASE
.\make-iso.cmd          # DEBUG 应用 + RELEASE Shell 全部签名后打包
.\make-iso.cmd RELEASE
```

- `out/<目标>/HelloWorld.efi`、`PCRRead.efi`、`TCGOpalCheck.efi`、`AtaPassUtil.efi`、`SMARTDisp.efi`、`UEFINVTest.efi`、`TPMOps.efi`、`NVMeDisp.efi`、`NVMeResetTest.efi`、`TestMain.efi`、`OpalTest.efi`、`OpalPba.efi` 均为已签名产物。
- `out/esp/EFI/BOOT/BOOTX64.EFI` 使用已签名的 HelloWorld。
- ISO 的 `EFI/BOOT/BOOTX64.EFI` 使用已签名 Shell，亦保存为 `out/<目标>/Shell.efi`；Shell 始终使用 RELEASE 构建。
- ISO 根目录与 El Torito FAT 启动镜像均使用相同的已签名文件。
- `Build/` 内保留未签名原始产物；直接复制 `Build/` 文件不会自动获得签名。
- 证书或私钥缺失、二者不匹配、签名或验签失败时脚本停止，不静默降级成未签名发布。
- 签名验证成功后才替换对应输出文件。私钥不会被复制到工程输出或 ISO。
- 默认签名器使用配置的未加密私钥，不会交互询问私钥密码。未来改用加密私钥时，需要调整签名器或接入外部签名流程。

原生 Linux 可指定其他路径：

```bash
export EFI_SIGN_KEY=/secure/path/MOK.priv
export EFI_SIGN_CERT=/secure/path/MOK.crt
bash scripts/make-iso.sh DEBUG
```

依赖：`sudo apt install openssl sbsigntool`。当前 WSL 已安装。

### 同一证书导入 db 和 MOK

可以复用同一张证书，两处导入是独立操作：

1. **固件 db**：在 BIOS/UEFI Key Management 中，向 Authorized Signatures / db **追加** `MOK.der`。如界面只接受 `.cer`，可复制 DER 文件并命名为 `MOK.cer`；仅改扩展名不改变证书内容。部分固件要求 ESL，需按固件要求转换。
2. **MOK**：在目标实机 Linux 中运行 `sudo mokutil --import /path/to/MOK.der`，然后重启，在 MokManager 中确认。如果已经 enroll 过同一张证书，则无需重复导入；可用 `mokutil --test-key /path/to/MOK.der` 检查。

**不要导入 `MOK.priv`，不要清空原有 PK、KEK、db 或 dbx。**
MOK 是 shim/Linux 使用的信任库；固件直接启动 Shell 仍依赖 db。
添加 db 本身不会安装 PK，也不会自动把 Setup Mode 转成已启用 Secure Boot。
若缺少 PK 或无法追加证书，先确认主板密钥管理流程，不要盲目删除/替换系统密钥。

本次没有修改实机 db/MOK，也没有清 TPM 或重新绑定 LUKS。导入后 PCR7 可能变化，先准备好 LUKS/BitLocker 恢复凭据，再在最终的正常 OS 启动状态下更新 TPM 绑定。
复用驱动私钥使两种用途共享同一信任边界；保护好原私钥，后续正式使用建议单独管理开发 EFI 签名密钥。

### 验证

```powershell
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-signatures.py
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-signatures.py RELEASE
wsl -d Ubuntu-24.04 --exec python3 /mnt/d/Workspace/EDKII/scripts/test-iso.py
```

签名测试会从实际 ISO 中提取 FAT 启动镜像，验证 Shell 和十二个应用的签名及各副本一致性，检查 PBA 镜像内的签名 loader，并验证缺少私钥时不会替换已有输出。
QEMU 测试仍使用普通 OVMF，**不能据此宣称已通过开启 Secure Boot 且导入证书的固件验证**；实机信任配置需要独立测试。

## 12. SMARTDisp — NVMe SMART / Health

新增 `out/DEBUG/SMARTDisp.efi` / `out/RELEASE/SMARTDisp.efi`，已自动签名并加入 ISO。

```text
SMARTDisp.efi          # 默认读取 UEFI 枚举的控制器 0
SMARTDisp.efi --list   # 列出控制器身份，核对 Linux nvme0 对应哪一个
SMARTDisp.efi 1        # 读取其他控制器
```

UEFI 编号不保证与 Linux 的 `/dev/nvme0` 顺序一致，需核对序列号和型号。
工具只读取 Identify Controller 与标准 SMART 日志页，不修改磁盘或安全状态。
功能和测试说明见 [`MyPkg/ProofOfConcept/SMARTDisp/README.md`](MyPkg/ProofOfConcept/SMARTDisp/README.md)。

## 13. UEFINVTest — UEFI 非易失变量

新增已签名的 `out/DEBUG/UEFINVTest.efi` / `out/RELEASE/UEFINVTest.efi`，已加入 ISO。

```text
UEFINVTest.efi          # 默认只读
UEFINVTest.efi test     # 新建测试变量并读回校验；不覆盖已有变量
UEFINVTest.efi read     # 重启后可再次读取
UEFINVTest.efi write    # 显式创建/覆盖
UEFINVTest.efi delete   # 删除测试变量
```

固定名称：`TestVariable-{bd29e016-764e-4a58-9d53-d64fe953edab}`；VendorGuid 同此 GUID。
属性为 NV|BS|RT，只操作该自定义变量，不修改系统启动项或 Secure Boot 变量。
`test`/`write` 会写真实固件 NVRAM；自动验证仅在隔离的 QEMU VARS 文件中进行。
后续修改名称、GUID 和数据的位置，以及 Linux 读取方法，见
[`MyPkg/ProofOfConcept/UEFINVTest/README.md`](MyPkg/ProofOfConcept/UEFINVTest/README.md)。

## 14. TPMOps — TPM 2.0 密封 / 解封

新增已签名的 `out/<目标>/TPMOps.efi`，通过 TCG2 协议调用 TPM2_CreatePrimary/Create/Load/Unseal。

```text
TPMOps.efi wrap          # 交互设置对象授权；密封示例数据，存为 NV|BS
TPMOps.efi wrap --RT     # 加 RT，供 Linux 读取记录进行调试
TPMOps.efi unwrap       # 输入对象授权，解封并打印示例明文 hex
TPMOps.efi info
TPMOps.efi dump
TPMOps.efi delete
TPMOps.efi wrap --pcr0    # 独立变量，绑定当前 SHA256 PCR0，无对象密码
TPMOps.efi unwrap --pcr0  # policy-only 解封；PCR 改变则拒绝，不回退
```

变量：`TPMTest-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}`，VendorGuid 同此 GUID。
保存 TPM public/private blob，不保存明文或授权口令；已有变量拒绝覆盖。
默认不绑定 PCR，不改变 hierarchy auth、不清 TPM、不分配持久句柄或 TPM NV index。
`--pcr0` 另存 `TPMTest-PCR0-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}`，
使用 PCR0(SHA256) + Unseal policy，清除 userWithAuth，不询问对象密码、不自动重新密封。
固件更新可能使旧对象无法解封；PCR0 一致不代表调用程序身份。两种模式都不具备 TPM 参数加密；
真实秘密应移除明文打印并加强会话/策略设计。

Linux 导出：`scripts/export-tpmops.py`。接口、记录格式和 tpm2-tools 调试步骤见
[`MyPkg/ProofOfConcept/TPMOps/README.md`](MyPkg/ProofOfConcept/TPMOps/README.md)。
集成测试仅使用独立 swtpm 和临时 OVMF VARS，不访问真实 TPM 或主板 NVRAM。

## 15. NVMeDisp — Identify Controller

新增已签名的 `out/<目标>/NVMeDisp.efi`，读取 NVMe Identify Controller（CNS=1）的 4096 字节身份与能力数据。

```text
NVMeDisp.efi             # 默认 UEFI 控制器 0
NVMeDisp.efi --list      # 列举控制器身份
NVMeDisp.efi 1           # 选择其他控制器
NVMeDisp.efi 0 --raw     # 附加完整 Identify hex
```

显示型号、序列号、固件、版本、OACS/ONCS、Security Send/Receive 声明、固件/日志/电源能力、容量字段和 SUBNQN 等。
只读，不执行安全、格式化或擦除操作；UEFI 索引不保证与 Linux nvme0 一致。
详细字段解释与适配入口见 [`MyPkg/ProofOfConcept/NVMeDisp/README.md`](MyPkg/ProofOfConcept/NVMeDisp/README.md)。

## 16. NVMeResetTest — 逐步检查 / Reset / 再检查

```text
NVMeResetTest.efi --list
NVMeResetTest.efi 0
```

核对目标身份后，依次输入 `CHECK`、`RESET`、`CHECK`，每次回车才执行对应步骤；`q` 回车或 Esc 取消。
复用 TCGOpalCheck 的只读查询，只检查选中设备；中间通过驱动执行一次 `BlockIo->Reset(TRUE)`。
reset 后重新枚举并核对原路径/身份，汇总 EF Enabled/Locked/Frozen 与 TCG Locking flags 前后变化。

**不是保证解冻的工具。** reset 可能影响控制器上全部 namespace、打断 I/O 或重新锁盘，请从独立 USB/ISO 运行并先备份。
不直接写寄存器，不设置密码、不擦除、不写磁盘数据；自动测试仅连接 QEMU 临时磁盘。
详见 [`MyPkg/ProofOfConcept/NVMeResetTest/README.md`](MyPkg/ProofOfConcept/NVMeResetTest/README.md)。

## 17. TestMain — 模块测试入口

```text
TestMain.efi nvme-enum   # 枚举 NVMe 控制器，显示数量
TestMain.efi nvme-info   # 要求只有一个控制器，再调用 NVMeGetControllerInfo 并打印结果
TestMain.efi --help
```

只实现 `SealSecret-DoNotShipToUEFI/SealSecret.c` 的 `UefiMain()` 当前已有的只读流程，不执行头部 plan。
无参数只显示帮助；每次选择一项测试。可调用函数声明在
`MyPkg/Application/Test/TestMain.h`，后续模块直接添加函数和参数分支。
产物 `out/<目标>/TestMain.efi` 已签名并加入 ISO；SealSecret 与 Unlocker 草稿未接入构建或打包。
`scripts/test-funcionality.py` 使用 QEMU 临时磁盘检查 0/1/2 个控制器和参数分支，不访问实盘。

## 18. OpalTest — OPAL 改密 / 锁定 / 撤销 / 独立 Revert 测试

位于 `MyPkg/ProofOfConcept/OpalTest/`，仅供可丢弃的 OPAL NVMe 测试盘。

```text
OpalTest.efi --list
OpalTest.efi status 0
OpalTest.efi sid-set 0
OpalTest.efi activate 0
OpalTest.efi admin1-set 0
OpalTest.efi user1-set 0
OpalTest.efi global-lock 0
OpalTest.efi global-unlock 0
OpalTest.efi global-status 0
OpalTest.efi locking-disable 0
OpalTest.efi user1-disable 0
```

各参数只执行一项操作；密码隐藏输入，写操作另要求 `WRITE` 和目标序列号。
使用 Linux sedutil 默认模式设置的密码时，显式加 `--sedutil`，例如 `OpalTest.efi verify-admin1 0 --sedutil`。
该模式对当前和新口令执行 PBKDF2-HMAC-SHA1（75000 次、原始 20 字节 NVMe 序列号 salt、32 字节 PIN）；
MSID 不派生。不带参数仍为原始 ASCII 模式，不自动回退，也不会清除已有认证锁定。
恢复原 SID/Admin1 口令使用对应 setter；User1 禁用不擦除口令。
Global Range 锁定/解锁不是介质转明文/密文，也不证明覆盖独立 range 或所有 namespace。

**破坏性恢复出厂单独使用 `OpalTest.efi revert 0 --destroy-data`**，另需输入 `ERASE`、序列号和 SID PIN。
可能擦除所有数据；绝不自动串联、失败后回退、重试或执行 PSID/GenKey。
从独立 USB/ISO 启动，先备份；当前只做 mock 改密/锁定/擦除测试，没有实盘执行结果。
规范依据、逐项语义和测试命令见 [OpalTest/README.md](MyPkg/ProofOfConcept/OpalTest/README.md)。

### 静态 PBA / Shadow MBR 提示

`OpalPba.img` 是 8 MiB 的 UEFI X64 GPT/FAT 镜像，启动后只显示锁定与设置 MBRDone 的提示，不查询/认证硬盘。
使用 `OpalTest.efi load-pba 0 --sedutil`（或原始密码模式省略该选项），输入 `WRITE`、目标序列号和 Admin1 密码，
把应用所在文件系统根目录的 `\OpalPba.img` 上传至 MBR byte table。它覆盖旧 PBA，不写真实用户 LBA，
不自动修改 MBREnable/MBRDone 或范围锁定；失败可能只写了一部分，不能盲目重试。
镜像及 EFI 已自动加入对应 ISO。详见 [OpalPba/README.md](MyPkg/ProofOfConcept/OpalPba/README.md)。
