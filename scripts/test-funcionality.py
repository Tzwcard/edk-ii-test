#!/usr/bin/env python3
"""Run TestMain with 0/1/2 disposable QEMU NVMe controllers."""
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parent.parent
target = sys.argv[1] if len(sys.argv) > 1 else "DEBUG"
if target not in ("DEBUG", "RELEASE"):
    raise SystemExit("Usage: python3 scripts/test-funcionality.py [DEBUG|RELEASE]")
logs = root / "out/qemu-funcionality" / target
logs.mkdir(parents=True, exist_ok=True)

for count in (0, 1, 2):
    with tempfile.TemporaryDirectory(prefix="funcionality-") as directory:
        temp = Path(directory)
        nvram = temp / "OVMF_VARS.fd"
        shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
        arguments = [
            "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
            "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
            "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
            "-cdrom", str(root / "out" / f"uefi-shell-x64-{target}.iso"), "-boot", "order=d",
            "-display", "none", "-serial", "mon:stdio", "-monitor", "none", "-no-reboot",
        ]
        for index in range(count):
            disk = temp / f"disk{index}.img"
            with disk.open("wb") as stream:
                stream.truncate(64 * 1024 * 1024)
            arguments += ["-drive", f"if=none,id=nvme{index},format=raw,file={disk}",
                          "-device", f"nvme,drive=nvme{index},serial=FUNCTIONTEST{index}"]
        process = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        transcript = bytearray()
        pending = bytearray()

        def wait(marker):
            deadline = time.monotonic() + 90
            while marker not in pending:
                if time.monotonic() > deadline:
                    raise RuntimeError(f"Timeout: {marker!r}")
                if not selector.select(timeout=1):
                    if process.poll() is not None:
                        raise RuntimeError("QEMU exited")
                    continue
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    raise RuntimeError("QEMU output closed")
                transcript.extend(data)
                pending.extend(data)
            result = bytes(pending)
            pending.clear()
            return result

        def command(text):
            process.stdin.write(text + b"\r")
            process.stdin.flush()
            return wait(b"FS0:\\>")

        try:
            wait(b"Shell>")
            command(b"fs0:")
            assert b"Usage: TestMain.efi" in command(b"TestMain.efi")
            assert b"nvme-enum:" in command(b"TestMain.efi --help")
            assert b"Unknown test:" in command(b"TestMain.efi invalid")
            assert b"Choose one test" in command(b"TestMain.efi nvme-info extra")
            enumeration = command(b"TestMain.efi nvme-enum")
            info = command(b"TestMain.efi nvme-info")
            if count == 0:
                assert b"No NVMe PassThru controllers available" in enumeration
                assert b"No NVMe PassThru controllers available" in info
            else:
                assert f"NVMe controller count: {count}".encode() in enumeration
                if count == 1:
                    assert b"NVMeGetControllerInfo: Success" in info and b"Serial: FUNCTIONTEST0" in info
                    assert b"IsEnabled:" in info and b"AtaFrozen: N/A" in info
                else:
                    assert b"Should be only 1 controller available; found 2." in info
                    assert b"STEP - NVMeGetControllerInfo" not in info
        except Exception:
            print(transcript[-8000:].decode(errors="replace"))
            raise
        finally:
            if process.poll() is None:
                try:
                    process.stdin.write(b"\x01x")
                    process.stdin.flush()
                    process.wait(timeout=5)
                except (BrokenPipeError, subprocess.TimeoutExpired):
                    process.kill()
                    process.wait()
            selector.close()
            (logs / f"nvme-{count}.log").write_bytes(transcript)

print("PASS: help/invalid args, enumeration, no-controller path, single-controller fetch, multi-controller refusal.")
print(f"Only disposable QEMU disks used. Logs: {logs}")
