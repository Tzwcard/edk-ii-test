#!/usr/bin/env python3
"""Actual TpmSeal.c against a PRIVATE swtpm; PCR extension happens ONLY here."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent.parent
for tool in ("gcc", "swtpm", "tpm2_createprimary", "tpm2_load", "tpm2_unseal", "tpm2_flushcontext",
             "tpm2_startauthsession", "tpm2_policypcr", "tpm2_policycommandcode", "tpm2_pcrextend"):
    if not shutil.which(tool):
        raise SystemExit(f"Missing {tool}")
out = root / "out/tests"
out.mkdir(parents=True, exist_ok=True)
library = out / "tpmops-swtpm.so"
subprocess.run([
    "gcc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-fshort-wchar", "-fPIC", "-shared", "-Wl,-z,defs",
    "-I", str(root / "edk2/MdePkg/Include"), "-I", str(root / "edk2/MdePkg/Include/X64"),
    "-I", str(root / "MyPkg/ProofOfConcept/TPMOps"),
    str(root / "tests/tpmops-swtpm-host.c"), str(root / "MyPkg/ProofOfConcept/TPMOps/TpmSeal.c"),
    "-o", str(library),
], check=True)
lib = ctypes.CDLL(str(library))
lib.HostConnect.argtypes, lib.HostConnect.restype = [ctypes.c_char_p], ctypes.c_int
lib.HostBanks.argtypes = [ctypes.c_uint32]
lib.HostWrap.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_uint32)]
lib.HostWrap.restype = ctypes.c_uint64
lib.HostUnwrap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_uint32)]
lib.HostUnwrap.restype = ctypes.c_uint64
expected = b"TPMOps PCR0 test only\0"

with tempfile.TemporaryDirectory(prefix="tpmops-pcr0-", dir="/dev/shm") as directory:
    temp = Path(directory)
    state = temp / "state"
    state.mkdir()
    server, control = temp / "tpm.sock", temp / "tpm.sock.ctrl"
    with (out / "tpmops-pcr0-swtpm.log").open("wb") as log:
        emulator = subprocess.Popen([
            "swtpm", "socket", "--tpm2", "--tpmstate", f"dir={state}",
            "--ctrl", f"type=unixio,path={control}", "--server", f"type=unixio,path={server}",
            "--flags", "not-need-init,startup-clear",
        ], stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while not server.exists():
                if emulator.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("private swtpm failed to start")
                time.sleep(0.05)
            assert lib.HostConnect(os.fsencode(server)) == 1
            record = ctypes.create_string_buffer(2200)
            size, rc = ctypes.c_size_t(2200), ctypes.c_uint32()
            lib.HostBanks(0)
            assert lib.HostWrap(record, ctypes.byref(size), ctypes.byref(rc)) == (1 << 63) | 3  # unsupported bank
            assert lib.HostCreates() == 0 and lib.HostUnseals() == 0
            lib.HostBanks(2)  # EFI_TCG2_BOOT_HASH_ALG_SHA256
            assert lib.HostWrap(record, ctypes.byref(size), ctypes.byref(rc)) == 0
            saved = record.raw[:size.value]
            assert saved[:8] == b"TPMWRAP2"
            (temp / "record.bin").write_bytes(saved)
            plain, length = ctypes.create_string_buffer(128), ctypes.c_size_t(128)
            assert lib.HostUnwrap(record, size.value, plain, ctypes.byref(length), ctypes.byref(rc)) == 0
            assert plain.raw[:length.value] == expected
            creates, unseals = lib.HostCreates(), lib.HostUnseals()
            lib.HostDisconnect()

            tcti = f"swtpm:path={server}"

            def run(*args):
                result = subprocess.run([str(arg) for arg in args], capture_output=True)
                if result.returncode:
                    raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
                return result

            exported = temp / "export"
            run("python3", root / "scripts/export-tpmops.py", "--record", temp / "record.bin", "--out", exported)
            metadata = json.loads((exported / "metadata.json").read_text())
            assert metadata["object_auth_required"] is False and metadata["pcr_policy"]["pcr"] == 0
            run("tpm2_createprimary", "-T", tcti, "-C", "o", "-g", "sha256", "-G", "rsa2048:aes128cfb", "-a", "0x30472", "-c", temp / "parent.ctx")
            run("tpm2_flushcontext", "-T", tcti, "-t")
            run("tpm2_load", "-T", tcti, "-C", temp / "parent.ctx", "-u", exported / "public.bin", "-r", exported / "private.bin", "-c", temp / "object.ctx")
            run("tpm2_flushcontext", "-T", tcti, "-t")
            # PCR-only object must not accept an empty password as a bypass.
            rejected = subprocess.run(["tpm2_unseal", "-T", tcti, "-c", str(temp / "object.ctx"), "-o", str(temp / "bypass.bin")], capture_output=True)
            assert rejected.returncode != 0
            assert not (temp / "bypass.bin").exists() or (temp / "bypass.bin").stat().st_size == 0
            run("tpm2_flushcontext", "-T", tcti, "-t")
            run("tpm2_startauthsession", "-T", tcti, "--policy-session", "-S", temp / "policy.ctx")
            run("tpm2_policypcr", "-T", tcti, "-S", temp / "policy.ctx", "-l", "sha256:0")
            run("tpm2_policycommandcode", "-T", tcti, "-S", temp / "policy.ctx", "-L", temp / "policy.digest", "TPM2_CC_Unseal")
            assert (temp / "policy.digest").read_bytes().hex() == metadata["pcr_policy"]["auth_policy"]
            run("tpm2_unseal", "-T", tcti, "-c", temp / "object.ctx", "-p", f"session:{temp / 'policy.ctx'}", "-o", temp / "plain.bin")
            assert (temp / "plain.bin").read_bytes() == expected
            # ALL commands point to this private socket; never extend a host PCR.
            run("tpm2_flushcontext", "-T", tcti, "-t")
            run("tpm2_flushcontext", "-T", tcti, "-l")
            run("tpm2_flushcontext", "-T", tcti, "-s")
            run("tpm2_pcrextend", "-T", tcti, "0:sha256=" + "00" * 32)
            assert lib.HostConnect(os.fsencode(server)) == 1
            plain, length = ctypes.create_string_buffer(b"X" * 128, 128), ctypes.c_size_t(128)
            assert lib.HostUnwrap(record, size.value, plain, ctypes.byref(length), ctypes.byref(rc)) == (1 << 63) | 26
            assert length.value == 0 and plain.raw == bytes(128)
            assert lib.HostCreates() == creates and lib.HostUnseals() == unseals  # no reseal/fallback or Unseal attempt
            assert record.raw[:size.value] == saved
        finally:
            lib.HostDisconnect()
            emulator.terminate()
            try:
                emulator.wait(timeout=5)
            except subprocess.TimeoutExpired:
                emulator.kill()
                emulator.wait()
print("PASS: production PCR0 wrap/unwrap, SHA256 bank refusal, v2 export, tpm2-tools policy digest and unseal interoperability.")
print("PASS: no password bypass; changed PCR0 refuses with zeroized output and no Unseal/reseal/fallback.")
print("Only private swtpm sockets were accessed; no host TPM, PCR or NVRAM was changed.")
