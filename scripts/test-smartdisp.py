#!/usr/bin/env python3
"""Boot SMARTDisp against a disposable QEMU NVMe, never a host disk."""
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
    raise SystemExit("Usage: python3 scripts/test-smartdisp.py [DEBUG|RELEASE]")
tests = root / "out/tests"
tests.mkdir(parents=True, exist_ok=True)
log_path = root / "out/qemu-smart" / target / "serial.log"
log_path.parent.mkdir(parents=True, exist_ok=True)
transcript = bytearray()
success = False

with tempfile.TemporaryDirectory(prefix="smartdisp-nvme-", dir=tests) as directory:
    temp = Path(directory)
    disk = temp / "disposable-nvme.img"
    with disk.open("wb") as stream:
        stream.truncate(64 * 1024 * 1024)
    nvram = temp / "OVMF_VARS.fd"
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
        "-cdrom", str(root / "out" / f"uefi-shell-x64-{target}.iso"), "-boot", "order=d",
        "-drive", f"if=none,id=smartdisk,format=raw,file={disk}",
        "-device", "nvme,drive=smartdisk,serial=SMARTDISPTEST0001",
        "-display", "none", "-serial", "mon:stdio", "-monitor", "none", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    pending = bytearray()
    commands = [b"fs0:\r", b"SMARTDisp.efi 999\r", b"SMARTDisp.efi --list\r", b"SMARTDisp.efi\r"]
    index = 0
    deadline = time.monotonic() + 90
    try:
        while time.monotonic() < deadline:
            if not selector.select(timeout=1):
                if process.poll() is not None:
                    break
                continue
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                break
            transcript.extend(data)
            pending.extend(data)
            if b"SMARTDisp completed successfully." in transcript:
                success = all(marker in transcript for marker in (
                    b"Controller index 999 unavailable", b"Controller listing complete.",
                    b"SMARTDISPTEST0001", b"SMART / Health Information", b"Power On Hours:",
                    b"Data Units Read:", b"Composite Temperature:",
                )) and index == len(commands)
                break
            if b">" in pending and index < len(commands):
                process.stdin.write(commands[index])
                process.stdin.flush()
                index += 1
                pending.clear()
    finally:
        if process.poll() is None:
            try:
                process.stdin.write(b"\x01x")
                process.stdin.flush()
                process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        selector.close()
        log_path.write_bytes(transcript)

if not success:
    print(transcript[-10000:].decode("utf-8", errors="replace"))
    raise SystemExit(f"FAIL: SMARTDisp QEMU NVMe test. See {log_path}")
print("PASS: SMARTDisp lists QEMU NVMe, rejects invalid index, reads Identify and SMART log 02h.")
print(f"Log: {log_path}")
