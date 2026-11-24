# OpalPba — 静态 UEFI 提示镜像

启动后仅显示：
```text
Disk read error: MBR is missing
Contact game provider to resolve this issue.
```

它不收集密码、不查询 OPAL、不修改任何磁盘，也不自动链式引导系统。
文字是静态提示，**不证明 Range 已锁定或 MBRDone 当前一定为 0**。
界面停留到人工重启；没有自动解锁或返回真实系统的逻辑。

## 构建与上传

构建脚本生成并签名 `out/DEBUG/OpalPba.efi`（RELEASE 同理），然后生成 `OpalPba.img`：
8 MiB、512 字节逻辑扇区、GPT + 6 MiB FAT16 ESP，包含签名的 `EFI/BOOT/BOOTX64.EFI`。
**这是 UEFI X64 镜像，不含 Legacy BIOS 引导代码；不适用于 4Kn 命名空间。**

EFI 和镜像均随 Shell ISO 打包。如果用 USB 单独复制 OpalTest，应把 `OpalPba.img`
放到**加载 OpalTest 所用文件系统的根目录**（例如 `FS0:\OpalPba.img`），不是 Shell 当前目录。

```text
OpalTest.efi --list
OpalTest.efi load-pba 0 --sedutil
```

Admin1 认证；确认 `WRITE` 和目标序列号；密码隐藏输入。原始密码模式省略 `--sedutil`。
镜像先完整读入内存并检查大小及基本 MBR/GPT 标识，再允许确认和认证。
通过 Locking SP 的 **MBR byte table.Set(Where=字节偏移, Values=字节串)** 分块上传，
不调用 BlockIo.WriteBlocks、NVMe Write、Format、Revert 或 GenKey。
这不是原子操作：**覆盖已有 Shadow MBR/PBA，失败可能留下半份镜像**。首个错误立即停止，不自动重传。
每块只检查方法状态，未实现整幅镜像 Get 读回验证。8 MiB 小于 Opal 2.30 §4.3.5.4 规定的最小 128 MiB MBR 表。

上传不修改 `MBRControl.Enable/Done/DoneOnReset` 或 Range 锁位。
仅当 Shadow 激活（Enable=1、Done=0）且固件从该盘的 UEFI fallback 路径启动时，才可能显示提示；
现有指向真实 ESP 的 Boot#### 路径、固件缓存和厂商 Shadow 实现可能影响启动，需要实机单独验证。
不要把看到/未看到提示当成密码或介质加密测试结果。

解锁需从另一张 USB/ISO 启动 OpalTest，按已知凭据解锁 Range，并单独执行 `mbr-done`。
不要在 Shadow 生效时运行系统安装程序或格式化工具。

## 验证范围

`scripts/test-opal-ops.sh` 模拟上传全部 8 MiB，检查 UID、每块字节偏移和二进制内容、尾块、失败停止及写入开关。
`scripts/test-pba.py [DEBUG|RELEASE]` 将镜像作为只读 QEMU NVMe 磁盘启动，检查当前两行提示。
签名测试检查镜像内的 loader 和 ISO 两份镜像的内容一致性。
**不在物理盘自动上传，不声称已验证实盘 MBR Shadow 或固件 Secure Boot 信任。**
