#!/usr/bin/env python3
"""Boot NVMeDisp with two disposable QEMU NVMe controllers, never host disks."""
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
    raise SystemExit("Usage: python3 scripts/test-nvmedisp.py [DEBUG|RELEASE]")
tests = root / "out/tests"
tests.mkdir(parents=True, exist_ok=True)
log_path = root / "out/qemu-nvmedisp" / target / "serial.log"
log_path.parent.mkdir(parents=True, exist_ok=True)
transcript = bytearray()
success = False

with tempfile.TemporaryDirectory(prefix="nvmedisp-", dir=tests) as directory:
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
    for number in (1, 2):
        disk = temp / f"disposable{number}.img"
        with disk.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024)
        arguments += ["-drive", f"if=none,id=nvme{number},format=raw,file={disk}",
                      "-device", f"nvme,drive=nvme{number},serial=NVMEIDTEST000{number}"]
    process = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    pending = bytearray()
    commands = [b"fs0:", b"NVMeDisp.efi 999", b"NVMeDisp.efi --list --raw",
                b"NVMeDisp.efi --list", b"NVMeDisp.efi 0 --raw", b"NVMeDisp.efi 1", b"echo NVMEDISP_DONE"]
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
            if b"NVMEDISP_DONE" in transcript and index == len(commands):
                success = all(marker in transcript for marker in (
                    b"Controller index 999 unavailable", b"--list cannot be combined",
                    b"NVMeDisp listing complete.", b"NVMEIDTEST0001", b"NVMEIDTEST0002",
                    b"Identify Controller raw data (4096 bytes):",
                    b"OACS:", b"ONCS:", b"SUBNQN:", b"NN (Namespaces):",
                )) and b"0ff0:" in transcript.lower() and transcript.count(b"NVMeDisp completed successfully.") == 2
                break
            if b">" in pending and index < len(commands):
                process.stdin.write(commands[index] + b"\r")
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
    print(transcript[-12000:].decode("utf-8", errors="replace"))
    raise SystemExit(f"FAIL: NVMeDisp test. See {log_path}")
print("PASS: two NVMe controllers identified; listing, selection, raw dump and invalid options verified.")
print(f"Log: {log_path}")
