#!/usr/bin/env python3
"""Exercise sealing on an isolated swtpm + disposable QEMU VARS image only."""
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parent.parent
target = sys.argv[1] if len(sys.argv) > 1 else "DEBUG"
if target not in ("DEBUG", "RELEASE"):
    raise SystemExit("Usage: python3 scripts/test-tpmops.py [DEBUG|RELEASE]")
for tool in ("swtpm", "qemu-system-x86_64", "tpm2_createprimary", "tpm2_load", "tpm2_unseal", "tpm2_flushcontext"):
    if not shutil.which(tool):
        raise SystemExit(f"Missing {tool}; install swtpm qemu-system-x86")
logs = root / "out/qemu-tpmops" / target
logs.mkdir(parents=True, exist_ok=True)
auth = b"TpmOpsTestOnly-2026"  # Public test credential, used ONLY inside this emulator.
prompt = b"input hidden; Esc cancels): "


def boot(nvram, control, steps, marker, log):
    process = subprocess.Popen([
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256", "-net", "none",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,unit=1,file={nvram}",
        "-cdrom", str(root / "out" / f"uefi-shell-x64-{target}.iso"), "-boot", "order=d",
        "-chardev", f"socket,id=chrtpm,path={control}",
        "-tpmdev", "emulator,id=tpm0,chardev=chrtpm", "-device", "tpm-tis,tpmdev=tpm0",
        "-display", "none", "-serial", "mon:stdio", "-monitor", "none", "-no-reboot",
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    transcript = bytearray()
    pending = bytearray()
    index = 0
    deadline = time.monotonic() + 150
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
            if marker in transcript and index == len(steps):
                if auth in transcript:
                    raise RuntimeError("Object authorization was echoed to the console")
                return bytes(transcript)
            if index < len(steps) and steps[index][0] == prompt and b"FS0:\\>" in pending:
                print(transcript[-10000:].decode("utf-8", errors="replace"))
                raise RuntimeError(f"TPMOps returned before authorization prompt; see {log}")
            if index < len(steps) and steps[index][0] in pending:
                process.stdin.write(steps[index][1] + b"\r")
                process.stdin.flush()
                index += 1
                pending.clear()
        print(transcript[-10000:].decode("utf-8", errors="replace"))
        raise RuntimeError(f"TPMOps phase did not finish (step {index}/{len(steps)}); see {log}")
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


# AF_UNIX emulator sockets must live on a Linux filesystem, not Windows /mnt/d.
with tempfile.TemporaryDirectory(prefix="edkii-tpmops-") as directory:
    temp = Path(directory)
    state = temp / "tpm-state"
    state.mkdir()
    control = temp / "tpm.sock.ctrl"
    server = temp / "tpm.sock"
    nvram = temp / "OVMF_VARS.fd"
    shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    with (logs / "swtpm.log").open("wb") as emulator_log:
        def start_emulator(tool_mode=False):
            arguments = [
                "swtpm", "socket", "--tpm2", "--tpmstate", f"dir={state}",
                "--ctrl", f"type=unixio,path={control}",
                "--flags", "not-need-init,startup-clear" if tool_mode else "not-need-init",
            ]
            if tool_mode:
                arguments += ["--server", f"type=unixio,path={server}"]
            instance = subprocess.Popen(arguments, stdout=emulator_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 10
            while not control.exists() or (tool_mode and not server.exists()):
                if instance.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("swtpm startup failed; see swtpm.log")
                time.sleep(0.05)
            return instance

        emulator = start_emulator()
        try:
            first = boot(nvram, control, [
                (b"Shell>", b"fs0:"), (b">", b"TPMOps.efi wrap"),
                (prompt, auth), (prompt, auth),
                (b">", b"TPMOps.efi info"), (b">", b"TPMOps.efi unwrap"), (prompt, auth),
                (b">", b"TPMOps.efi wrap --RT"),  # Must refuse existing blob, no password prompt.
                (b">", b"TPMOps.efi delete"), (b">", b"TPMOps.efi wrap --RT"),
                (prompt, auth), (prompt, auth), (b">", b"TPMOps.efi info"),
                (b">", b"echo TPMOPS_BOOT1_DONE"),
            ], b"TPMOPS_BOOT1_DONE", logs / "boot1.log")
            assert b"TPMOps WRAP PASS: sealed blob stored and verified; RT=0." in first
            assert b"attributes=0x00000003" in first
            assert b"Wrap refused: variable exists" in first
            assert b"TPMOps UNWRAP PASS." in first
            assert b"TPMOps WRAP PASS: sealed blob stored and verified; RT=1." in first
            assert b"attributes=0x00000007" in first

            # QEMU disconnect can terminate swtpm. Restart it with the SAME
            # permanent state to test persistence across a complete TPM power cycle.
            if emulator.poll() is None:
                emulator.terminate()
                emulator.wait(timeout=5)
            control.unlink(missing_ok=True)
            emulator = start_emulator()
            second = boot(nvram, control, [
                (b"Shell>", b"fs0:"), (b">", b"TPMOps.efi info"),
                (b">", b"TPMOps.efi unwrap"), (prompt, auth),
                (b">", b"TPMOps.efi dump"), (b">", b"TPMOps.efi delete"),
                (b">", b"TPMOps.efi info"), (b">", b"echo TPMOPS_BOOT2_DONE"),
            ], b"TPMOPS_BOOT2_DONE", logs / "boot2.log")
            assert second.count(b"TPMOps UNWRAP PASS.") == 1
            assert b"RT=1" in second and b"GetVariable: Not Found" in second
            match = re.search(rb"TPMOPS_BLOB_BEGIN\s+([0-9a-fA-F\s]+)TPMOPS_BLOB_END", second)
            assert match, "Wrapped blob dump not found"
            record = bytes.fromhex(match.group(1).decode())
            (logs / "wrapped-record.bin").write_bytes(record)
            assert b"TPMOps example sealed payload v1" not in record
            assert auth not in record

            # Verify the UEFI record can ALSO be loaded/unsealed with Linux tools.
            # All -T arguments target this private emulator, NEVER /dev/tpm*.
            if emulator.poll() is None:
                emulator.terminate()
                emulator.wait(timeout=5)
            control.unlink(missing_ok=True)
            server.unlink(missing_ok=True)
            emulator = start_emulator(tool_mode=True)
            exported = temp / "export"

            def run(*arguments):
                result = subprocess.run([str(item) for item in arguments], capture_output=True)
                if result.returncode:
                    raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
                return result

            run("python3", root / "scripts/export-tpmops.py", "--record", logs / "wrapped-record.bin", "--out", exported)
            auth_file = temp / "test-auth"
            auth_file.write_bytes(auth)
            auth_file.chmod(0o600)
            tcti = f"swtpm:path={server}"
            run("tpm2_createprimary", "-T", tcti, "-C", "o", "-g", "sha256", "-G", "rsa2048:aes128cfb",
                "-a", "0x30472", "-c", temp / "parent.ctx")
            # No resource manager exists on this disposable swtpm connection.
            # Flush transient slots between tool processes; contexts remain usable.
            run("tpm2_flushcontext", "-T", tcti, "-t")
            run("tpm2_load", "-T", tcti, "-C", temp / "parent.ctx", "-u", exported / "public.bin",
                "-r", exported / "private.bin", "-c", temp / "sealed.ctx")
            run("tpm2_flushcontext", "-T", tcti, "-t")
            run("tpm2_unseal", "-T", tcti, "-c", temp / "sealed.ctx", "-p", f"file:{auth_file}",
                "-o", temp / "unsealed.bin")
            assert (temp / "unsealed.bin").read_bytes() == b"TPMOps example sealed payload v1\0"
            # Negative auth test comes LAST: unclean power cycles after a failed
            # DA-protected attempt can intentionally trigger TPM lockout recovery.
            run("tpm2_flushcontext", "-T", tcti, "-t")
            wrong_file = temp / "wrong-test-auth"
            wrong_file.write_bytes(b"WrongTestAuth")
            rejected = subprocess.run([
                "tpm2_unseal", "-T", tcti, "-c", str(temp / "sealed.ctx"),
                "-p", f"file:{wrong_file}", "-o", str(temp / "wrong-output.bin"),
            ], capture_output=True)
            assert rejected.returncode != 0 and b"0x0000098e" in rejected.stderr
            assert not (temp / "wrong-output.bin").exists() or (temp / "wrong-output.bin").stat().st_size == 0
        finally:
            emulator.terminate()
            try:
                emulator.wait(timeout=5)
            except subprocess.TimeoutExpired:
                emulator.kill()
                emulator.wait()

print("PASS: TPM2 seal/unseal, NV|BS and --RT attributes, overwrite refusal, hidden authorization.")
print("PASS: same wrapped blob unseals after QEMU restart; wrong authorization fails; delete verified.")
print("PASS: Linux tpm2-tools recreate the parent and load/unseal the UEFI-generated blobs.")
print(f"Logs: {logs}")
print("Only disposable swtpm state and OVMF VARS were used; no real TPM or host variables accessed.")
