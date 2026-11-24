#!/usr/bin/env python3
"""Only help/argument/safe-refusal tests on disposable QEMU disks WITHOUT OPAL."""
import os
import hashlib
import re
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
    raise SystemExit("Usage: python3 scripts/test-opaltest.py [DEBUG|RELEASE]")
logs = root / "out/qemu-opaltest" / target
logs.mkdir(parents=True, exist_ok=True)
transcript = bytearray()
pending = bytearray()

with tempfile.TemporaryDirectory(prefix="opaltest-") as directory:
    temp = Path(directory)
    nvram = temp / "OVMF_VARS.fd"
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    disk = temp / "DISPOSABLE.img"
    with disk.open("wb") as stream:
        stream.truncate(64 * 1024 * 1024)
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
        "-cdrom", str(root / "out" / f"uefi-shell-x64-{target}.iso"), "-boot", "order=d",
        "-drive", f"if=none,id=testdisk,format=raw,file={disk}", "-device", "nvme,drive=testdisk,serial=OPALTESTONLY",
        "-display", "none", "-serial", "mon:stdio", "-monitor", "none", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def wait(marker):
        deadline = time.monotonic() + 90
        while marker not in pending:
            if time.monotonic() > deadline:
                raise RuntimeError(f"Timeout waiting for {marker!r}")
            if not selector.select(timeout=1):
                if process.poll() is not None:
                    raise RuntimeError("QEMU exited")
                continue
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError("QEMU output closed")
            pending.extend(data)
            transcript.extend(data)
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
        assert b"Usage: OpalTest.efi" in command(b"OpalTest.efi")
        assert b"DESTRUCTIVE:" in command(b"OpalTest.efi --help")
        assert b"PBKDF2-HMAC-SHA1" in command(b"OpalTest.efi --help")
        assert b"OPALTESTONLY" in command(b"OpalTest.efi --list")
        # Execute the actual firmware crypto on an unsupported-OPAL QEMU NVMe.
        # The command must not need SSCP/Discovery or open an OPAL session.
        for password in (b"1", b"1234", b"A" * 32, b"B" * 256):
            process.stdin.write(b"OpalTest.efi derive-pin 0\r")
            process.stdin.flush()
            prompt = wait(b"Password (hidden")
            salt = b"OPALTESTONLY".ljust(20, b" ")
            compact = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", prompt)
            assert salt.hex().upper().encode() in b"".join(compact.split())
            process.stdin.write(password + b"\r")
            process.stdin.flush()
            result = wait(b"FS0:\\>")
            compact = b"".join(re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", result).split())
            # Current user-selected SanitizeDiskSerial converts trailing spaces
            # to NULs. Raw Identify display above intentionally remains unchanged.
            effective_salt = salt.rstrip(b" ").ljust(20, b"\0")
            expected = hashlib.pbkdf2_hmac("sha1", password, effective_salt, 75000, 32).hex().upper().encode()
            assert b"DerivedPINhex(32bytes):" + expected in compact
            assert b"d020" + expected in compact
            assert b"NoOPALsessionorcredentialsent." in compact
            assert b"Authenticate as" not in prompt + result
        process.stdin.write(b"OpalTest.efi derive-pin 0\r")
        process.stdin.flush()
        wait(b"Password (hidden")
        process.stdin.write(b"\r")
        process.stdin.flush()
        assert b"Derived PIN hex" not in wait(b"FS0:\\>")
        assert b"--destroy-data is required" in command(b"OpalTest.efi revert 0")
        assert b"Usage:" in command(b"OpalTest.efi invalid 0")
        assert b"No mutation attempted" in command(b"OpalTest.efi status 999")
        assert b"Invalid or duplicate option" in command(b"OpalTest.efi admin1-set 0 --destroy-data")
        for arguments in ("verify-admin1 0 --sedutil --sedutil", "admin1-set 0 --msid",
                          "sid-set 0 --msid --msid", "revert 0 --msid", "status 0 --unknown"):
            assert b"Invalid or duplicate option" in command(f"OpalTest.efi {arguments}".encode())
        assert b"--destroy-data is required" in command(b"OpalTest.efi revert 0 --sedutil")
        for arguments in ("verify-admin1 0 --sedutil", "sid-set 0 --msid --sedutil",
                          "sid-set 0 --sedutil --msid", "revert 0 --sedutil --destroy-data",
                          "revert 0 --destroy-data --sedutil"):
            result = command(f"OpalTest.efi {arguments}".encode())
            assert b"PIN mode: sedutil PBKDF2-HMAC-SHA1" in result
            assert b"No mutation attempted" in result
            assert b"Authenticate as" not in result and b"Type WRITE" not in result
        for name in ("status", "sid-set", "admin1-set", "user1-set", "activate", "global-lock", "global-unlock",
                     "locking-disable", "user1-disable", "user1-unlock", "mbr-done", "load-pba", "revert"):
            flag = " --destroy-data" if name == "revert" else ""
            result = command(f"OpalTest.efi {name} 0{flag}".encode())
            assert b"No mutation attempted" in result
            assert b"Authenticate as" not in result and b"Type WRITE" not in result
        assert b"StartSession (one attempt)" not in transcript
    except Exception:
        print(transcript[-10000:].decode(errors="replace"))
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
        (logs / "serial.log").write_bytes(transcript)

print("PASS: OpalTest help, sedutil flags/order/duplicates, destructive opt-in and refusal on unsupported QEMU NVMe.")
print("PASS: actual EFI derive-pin output with current trailing-space-to-NUL salt rule vs Python for 1/4/32/256-byte inputs; empty input refused, no OPAL session.")
print("No physical disk attached; QEMU does not test real OPAL password changes, locking or erasure.")
print(f"Log: {logs / 'serial.log'}")
