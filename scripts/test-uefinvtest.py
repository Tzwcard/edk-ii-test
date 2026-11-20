#!/usr/bin/env python3
"""Verify NV writes persist across two QEMU processes with a disposable VARS file."""
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
    raise SystemExit("Usage: python3 scripts/test-uefinvtest.py [DEBUG|RELEASE]")
tests = root / "out/tests"
tests.mkdir(parents=True, exist_ok=True)
logs = root / "out/qemu-nv" / target
logs.mkdir(parents=True, exist_ok=True)


def boot(nvram, commands, marker, log):
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
        "-cdrom", str(root / "out" / f"uefi-shell-x64-{target}.iso"), "-boot", "order=d",
        "-display", "none", "-serial", "mon:stdio", "-monitor", "none", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    transcript = bytearray()
    pending = bytearray()
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
            if marker in transcript and index == len(commands):
                return bytes(transcript)
            if b">" in pending and index < len(commands):
                process.stdin.write(commands[index] + b"\r")
                process.stdin.flush()
                index += 1
                pending.clear()
        raise RuntimeError(f"QEMU did not complete NV test; see {log}")
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
        log.write_bytes(transcript)


with tempfile.TemporaryDirectory(prefix="uefinvtest-", dir=tests) as directory:
    nvram = Path(directory) / "OVMF_VARS.fd"
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    first = boot(nvram, [
        b"fs0:", b"UEFINVTest.efi", b"UEFINVTest.efi test", b"UEFINVTest.efi test",
        b"UEFINVTest.efi write", b"echo NVTEST_BOOT1_DONE",
    ], b"NVTEST_BOOT1_DONE", logs / "boot1.log")
    assert b"GetVariable: Not Found" in first
    assert b"UEFINVTest PASS: NV|BS|RT payload verified" in first
    assert b"Test refused: variable already exists" in first
    assert first.count(b"Read-back verification: Success") == 2

    # Reuse EXACTLY the same VARS image; run-iso.sh would reset it and cannot be used here.
    second = boot(nvram, [
        b"fs0:", b"UEFINVTest.efi read", b"UEFINVTest.efi delete", b"UEFINVTest.efi read",
        b"echo NVTEST_BOOT2_DONE",
    ], b"NVTEST_BOOT2_DONE", logs / "boot2.log")
    assert b"GetVariable: Success" in second
    assert b"Payload matches built-in test value: Yes" in second
    assert b"UEFINVTest DELETE PASS: variable is absent." in second
    assert b"GetVariable: Not Found" in second
    assert b"SetVariable (NV|BS|RT)" not in second

print("PASS: NV create/read/verify, refusal to overwrite in test mode, explicit overwrite.")
print("PASS: payload survived a full QEMU stop/start; read-only recovery and delete verified.")
print(f"Logs: {logs}")
print("Only a disposable OVMF VARS file was written; no host firmware variables were touched.")
