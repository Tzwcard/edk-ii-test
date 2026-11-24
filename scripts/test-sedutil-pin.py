#!/usr/bin/env python3
"""Real helper + real EDK PKCS5 wrapper, host OpenSSL, Python reference. No I/O."""
import ctypes
import hashlib
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
edk = root / "edk2"
app = root / "MyPkg/ProofOfConcept/OpalTest"
output = root / "out/tests"
output.mkdir(parents=True, exist_ok=True)
library = output / "sedutil-pin.so"
compile_command = [
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
    "-DMDEPKG_NDEBUG", "-fPIC", "-shared", "-Wl,-z,defs",
    "-I", str(edk / "MdePkg/Include"), "-I", str(edk / "MdePkg/Include/X64"),
    "-I", str(edk / "CryptoPkg/Include"),
    "-I", str(root / "tests/crypto-host"),
    str(app / "SedutilPin.c"), str(root / "tests/sedutil-pin-host.c"),
    str(edk / "CryptoPkg/Library/BaseCryptLib/Pk/CryptPkcs5Pbkdf2.c"),
    "-lcrypto", "-o", str(library),
]
subprocess.run(compile_command, check=True)
derive = ctypes.CDLL(str(library)).OpalSedutilDerivePin
derive.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p]
derive.restype = ctypes.c_uint64
invalid = (1 << 63) | 2
count = 0
for salt in (b"OPALTESTONLY".ljust(20, b" "), bytes(range(20)), b" " * 20, b"\0" * 20):
    for password in (b"1", b"1234", b"password", b" ", b"A" * 32, b"B" * 33, b"C" * 256):
        expected = hashlib.pbkdf2_hmac("sha1", password, salt, 75000, 32)
        source = ctypes.create_string_buffer(password, 257)
        result = ctypes.create_string_buffer(b"Z" * 40, 40)
        assert derive(source, len(password), salt, result) == 0
        assert result.raw == expected + b"Z" * 8
        assert derive(source, len(password), salt, source) == 0
        assert source.raw[:32] == expected
        count += 1
        for size in (0, 257):
            assert derive(source, size, salt, result) == invalid
        assert derive(None, 1, salt, result) == invalid
        assert derive(b"a\0b", 3, salt, result) == invalid
        assert derive(password, len(password), None, result) == invalid
        assert derive(password, len(password), salt, None) == invalid
        assert result.raw == expected + b"Z" * 8
assert hashlib.pbkdf2_hmac("sha1", b"1234", b"OPALTESTONLY".ljust(20, b" "), 75000, 32) != hashlib.pbkdf2_hmac("sha1", b"1234", b"OPALTESTONLY", 75000, 32)
failed_library = output / "sedutil-pin-failure.so"
failure_command = compile_command.copy()
failure_command.remove(str(edk / "CryptoPkg/Library/BaseCryptLib/Pk/CryptPkcs5Pbkdf2.c"))
failure_command[-1] = str(failed_library)
failure_command.insert(1, "-DTEST_CRYPTO_FAILURE")
subprocess.run(failure_command, check=True)
failed = ctypes.CDLL(str(failed_library)).OpalSedutilDerivePin
failed.argtypes, failed.restype = derive.argtypes, derive.restype
result = ctypes.create_string_buffer(b"Z" * 32, 32)
assert failed(b"1234", 4, b" " * 20, result) == (1 << 63) | 7
assert result.raw == b"Z" * 32
source = ctypes.create_string_buffer(b"1234", 32)
before = source.raw
assert failed(source, 4, b" " * 20, source) == (1 << 63) | 7
assert source.raw == before
print(f"PASS: {count} PBKDF2 cases vs Python, in-place output, raw 20-byte salts including spaces/NULs, lengths 1–256 and invalid inputs.")
print("Actual SedutilPin.c + EDK PKCS5 wrapper linked to host OpenSSL; no disk attached.")
print("PASS: partial crypto failure leaves separate/in-place caller output unchanged.")
