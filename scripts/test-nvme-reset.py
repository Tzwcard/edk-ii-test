#!/usr/bin/env python3
"""Interactive reset gate test using ONLY two disposable QEMU NVMe devices."""
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
    raise SystemExit("Usage: python3 scripts/test-nvme-reset.py [DEBUG|RELEASE]")
logs = root / "out/qemu-nvme-reset" / target
logs.mkdir(parents=True, exist_ok=True)
tests = root / "out/tests"
tests.mkdir(parents=True, exist_ok=True)
transcript = bytearray()
pending = bytearray()

with tempfile.TemporaryDirectory(prefix="nvme-reset-", dir=tests) as directory:
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
                      "-device", f"nvme,drive=nvme{number},serial=RESETTEST000{number}"]
    process = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def receive(timeout=1):
        if selector.select(timeout=timeout):
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                raise RuntimeError("QEMU exited")
            transcript.extend(data)
            pending.extend(data)
        elif process.poll() is not None:
            raise RuntimeError("QEMU exited")

    def wait(marker):
        deadline = time.monotonic() + 100
        while marker not in pending:
            if time.monotonic() > deadline:
                raise RuntimeError(f"Timeout waiting for {marker!r}")
            receive()
        data = bytes(pending)
        pending.clear()
        return data

    def send(line):
        process.stdin.write(line + b"\r")
        process.stdin.flush()

    def shell(command):
        send(command)
        return wait(b"FS0:\\>")

    check = b"Type CHECK then Enter to continue; q then Enter or Esc cancels: "
    reset = b"Type RESET then Enter to continue; q then Enter or Esc cancels: "
    try:
        wait(b"Shell>")
        shell(b"fs0:")
        listing = shell(b"NVMeResetTest.efi --list")
        assert b"RESETTEST0001" in listing and b"RESETTEST0002" in listing
        assert b"STEP 1/3" not in listing
        assert b"Usage: NVMeResetTest.efi" in shell(b"NVMeResetTest.efi --help")
        assert b"Controller index unavailable" in shell(b"NVMeResetTest.efi 999")
        assert b"STEP 1/3" not in shell(b"NVMeResetTest.efi --list 0")
        # Default path and first-step cancellation: NO reset.
        send(b"NVMeResetTest.efi")
        wait(check)
        assert b"Cancelled" in shell(b"q")

        # Wrong/empty input must not advance. Cancel at the reset gate.
        send(b"NVMeResetTest.efi 1")
        wait(check)
        send(b"")
        rejected = wait(check)
        assert b"Not confirmed" in rejected and b"Before check returned" not in rejected
        send(b"CHECK")
        before = wait(reset)
        assert b"Before check returned" in before and b"RESETTEST0002" in before
        send(b"yes")
        rejected = wait(reset)
        assert b"Not confirmed" in rejected and b"Calling BlockIo" not in rejected
        assert b"Cancelled" in shell(b"q")
        assert b"Calling BlockIo" not in transcript

        # One reset, then cancellation at final gate: NO post-check.
        send(b"NVMeResetTest.efi 1")
        wait(check)
        send(b"CHECK")
        wait(reset)
        send(b"RESET")
        reset_result = wait(check)
        assert b"BlockIo->Reset(TRUE): Success" in reset_result
        assert b"After check returned" not in shell(b"q")

        # Full sequence: confirm it stays idle at each gate before typing.
        send(b"NVMeResetTest.efi 1")
        wait(check)
        for token, next_marker, forbidden in (
            (b"CHECK", reset, b"Before check returned"),
            (b"RESET", check, b"Calling BlockIo"),
            (b"CHECK", b"FS0:\\>", b"After check returned"),
        ):
            receive(timeout=0.5)
            assert forbidden not in pending, "Step advanced without confirmation"
            send(token)
            part = wait(next_marker)
            assert forbidden in part
        assert transcript.count(b"Calling BlockIo->Reset(TRUE) now") == 2
        assert transcript.count(b"BlockIo->Reset(TRUE): Success") == 2
        assert transcript.count(b"NVMeResetTest sequence completed") == 1
        assert b"EF comparison: Unknown / unsupported; NOT evidence of unfreezing." in transcript
        # Both controllers still usable; regular shared TCGOpalCheck still runs.
        assert b"RESETTEST0001" in shell(b"NVMeDisp.efi 0")
        assert b"RESETTEST0002" in shell(b"NVMeDisp.efi 1")
        assert b"no write commands issued" in shell(b"TCGOpalCheck.efi")
    except Exception:
        print(transcript[-12000:].decode(errors="replace"))
        raise
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
        (logs / "serial.log").write_bytes(transcript)

print("PASS: typed step gates, wrong/empty input, cancellation at all three gates, selected controller reset.")
print("PASS: driver reset succeeded, target reidentified, both controllers usable, shared TCGOpalCheck executed.")
print("QEMU has no OPAL/EF security state: this does NOT test clearing Frozen on a real SSD.")
print(f"Log: {logs / 'serial.log'}")
