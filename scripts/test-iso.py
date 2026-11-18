#!/usr/bin/env python3
"""Boot the actual ISO, enter shell commands over serial, and check the app."""
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time

root = Path(__file__).resolve().parent.parent
target = sys.argv[1] if len(sys.argv) > 1 else "DEBUG"
if target not in ("DEBUG", "RELEASE"):
    raise SystemExit("Usage: python3 scripts/test-iso.py [DEBUG|RELEASE]")
log_path = root / "out" / "qemu-iso" / target / "smoke-test.log"
log_path.parent.mkdir(parents=True, exist_ok=True)
process = subprocess.Popen(
    ["bash", str(root / "scripts/run-iso.sh"), target],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
)
selector = selectors.DefaultSelector()
selector.register(process.stdout, selectors.EVENT_READ)
transcript = bytearray()
pending = bytearray()
commands = [b"map -r\r", b"fs0:\r", b"ls\r", b"TCGOpalCheck.efi\r", b"AtaPassUtil.efi\r", b"HelloWorld.efi\r"]
index = 0
success = False
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
        if b"UEFI application is running successfully (X64)." in transcript:
            success = (
                b"UEFI_SHELL_ISO_READY" in transcript
                and b"TCGOpalCheck - read-only TCG Level 0 Discovery" in transcript
                and b"Includes ATA Device Server Password Security (0xEF) status." in transcript
                and b"OPAL support: Unknown" in transcript
                and b"no write commands issued." in transcript
                and b"AtaPassUtil finished: read-only entry; no SendData calls." in transcript
                and index == len(commands)
            )
            break
        # A prompt ends in '>'. Clear the buffer after each command so that
        # the next command waits for a new prompt, not an earlier one.
        if b">" in pending and index < len(commands):
            process.stdin.write(commands[index])
            process.stdin.flush()
            index += 1
            pending.clear()
finally:
    if process.poll() is None:
        try:
            process.stdin.write(b"\x01x")  # QEMU mux: Ctrl+A, X
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
    print(transcript[-6000:].decode("utf-8", errors="replace"))
    raise SystemExit(f"FAIL: ISO shell / application test. See {log_path}")
print("PASS: ISO booted; shell, TCGOpalCheck, AtaPassUtil read-only entry, and HelloWorld executed.")
print(f"Log: {log_path}")
