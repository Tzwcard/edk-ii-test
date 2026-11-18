#!/usr/bin/env python3
"""Verify signed outputs and the actual ISO's EFI boot image, without booting."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
target = sys.argv[1] if len(sys.argv) > 1 else "DEBUG"
if target not in ("DEBUG", "RELEASE"):
    raise SystemExit("Usage: python3 scripts/test-signatures.py [DEBUG|RELEASE]")
cert = Path(subprocess.check_output(
    ["bash", str(root / "scripts/sign-efi.sh"), "--cert-path"], text=True
).strip())
iso = root / "out" / f"uefi-shell-x64-{target}.iso"
apps = ("HelloWorld", "PCRRead", "TCGOpalCheck", "AtaPassUtil")


def run(*args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args[0]}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def verify(path):
    run("sbverify", "--cert", cert, path)


def same(left, right):
    if hashlib.sha256(left.read_bytes()).digest() != hashlib.sha256(right.read_bytes()).digest():
        raise RuntimeError(f"EFI copies differ: {left} vs {right}")


tests = root / "out" / "tests"
tests.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="efi-signatures-", dir=tests) as temporary:
    temp = Path(temporary)
    boot_image = temp / "efiboot.img"
    run("xorriso", "-osirrox", "on", "-indev", iso,
        "-extract", "/boot/efiboot.img", boot_image)

    # Verify the Shell that firmware actually loads from the ISO boot image.
    shell = temp / "BOOTX64.EFI"
    run("mcopy", "-i", boot_image, "::/EFI/BOOT/BOOTX64.EFI", shell)
    verify(shell)
    same(shell, root / "out" / target / "Shell.efi")
    iso_shell = temp / "iso-shell.efi"
    run("xorriso", "-osirrox", "on", "-indev", iso,
        "-extract", "/EFI/BOOT/BOOTX64.EFI", iso_shell)
    same(shell, iso_shell)

    for app in apps:
        output = root / "out" / target / f"{app}.efi"
        verify(output)
        embedded = temp / f"fat-{app}.efi"
        run("mcopy", "-i", boot_image, f"::/{app}.efi", embedded)
        verify(embedded)
        same(output, embedded)
        iso_root_copy = temp / f"iso-{app}.efi"
        run("xorriso", "-osirrox", "on", "-indev", iso,
            "-extract", f"/{app}.efi", iso_root_copy)
        same(output, iso_root_copy)

    # A missing key must fail, without replacing a previously signed output.
    sentinel = temp / "unchanged.efi"
    sentinel.write_bytes(b"must remain unchanged")
    environment = dict(os.environ, EFI_SIGN_KEY=str(temp / "missing-private-key"))
    result = subprocess.run(
        ["bash", str(root / "scripts/sign-efi.sh"),
         str(root / "Build" / "MyPkg" / f"{target}_GCC" / "X64" / "HelloWorld.efi"),
         str(sentinel)],
        env=environment, capture_output=True, text=True,
    )
    if result.returncode == 0 or sentinel.read_bytes() != b"must remain unchanged":
        raise RuntimeError("Missing-key failure handling did not protect the output")

print(f"PASS: {target} Shell + four apps match the signing certificate.")
print("PASS: ISO root and embedded FAT boot-image copies match the verified outputs.")
print("PASS: missing signing key fails without overwriting the existing output.")
print("This verifies signatures, not enrollment into firmware db or MOK.")
