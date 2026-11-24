#!/usr/bin/env python3
"""Boot the PBA image on disposable QEMU NVMe, without OPAL or physical disks."""
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
    raise SystemExit("Usage: test-pba.py [DEBUG|RELEASE]")
image = root / "out" / target / "OpalPba.img"
logs = root / "out/qemu-pba" / target
logs.mkdir(parents=True, exist_ok=True)
transcript = bytearray()
with tempfile.TemporaryDirectory(prefix="pba-boot-") as directory:
    nvram = Path(directory) / "OVMF_VARS.fd"
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
        "-drive", f"if=none,id=pba,format=raw,readonly=on,file={image}",
        "-device", "nvme,drive=pba,serial=PBANOTICEONLY",
        "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        deadline = time.monotonic() + 90
        while b"Contact game provider to resolve this issue." not in transcript:
            if time.monotonic() > deadline:
                raise RuntimeError("PBA boot timed out")
            if not selector.select(timeout=1):
                if process.poll() is not None:
                    raise RuntimeError("QEMU exited before PBA notice")
                continue
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError("QEMU output closed")
            transcript.extend(data)
        assert b"Disk read error: MBR is missing\r\nContact game provider to resolve this issue." in transcript
    except Exception:
        print(transcript.decode(errors="replace"))
        raise
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        selector.close()
        (logs / "serial.log").write_bytes(transcript)
print("PASS: GPT/FAT PBA image boots on QEMU NVMe and prints the current two-line notice.")
print("No OPAL device simulated, no hardware upload or firmware Secure Boot trust tested.")
