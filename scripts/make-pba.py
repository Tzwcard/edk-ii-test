#!/usr/bin/env python3
"""Create an 8 MiB GPT/ESP image in a REGULAR FILE, never a physical disk."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

root = Path(__file__).resolve().parent.parent
target = sys.argv[1] if len(sys.argv) > 1 else "DEBUG"
if target not in ("DEBUG", "RELEASE"):
    raise SystemExit("Usage: make-pba.py [DEBUG|RELEASE]")
out = root / "out" / target
loader = out / "OpalPba.efi"  # Already signed by build.sh.
sectors, start, count = 16384, 2048, 12288
entries = bytearray(128 * 128)
struct.pack_into("<16s16sQQQ72s", entries, 0,
                 uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").bytes_le,
                 uuid.UUID("7f096996-a190-46c2-878e-b802c5daf5ec").bytes_le,
                 start, start + count - 1, 0, "OPAL PBA".encode("utf-16-le"))


def header(current, backup, table):
    block = bytearray(512)
    struct.pack_into("<8sIIIIQQQQ16sQIII", block, 0, b"EFI PART", 0x10000, 92, 0, 0,
                     current, backup, 34, sectors - 34,
                     uuid.UUID("e2324b29-623e-4436-a9a1-8fdf9828310a").bytes_le,
                     table, 128, 128, zlib.crc32(entries))
    struct.pack_into("<I", block, 16, zlib.crc32(block[:92]))
    return block


with tempfile.TemporaryDirectory(prefix="pba-", dir=out) as directory:
    temp = Path(directory)
    fat = temp / "esp.fat"
    with fat.open("wb") as stream:
        stream.truncate(count * 512)
    subprocess.run(["mkfs.vfat", "-F", "16", "-s", "1", "-n", "OPAL_PBA", str(fat)], check=True)
    subprocess.run(["mmd", "-i", str(fat), "::/EFI", "::/EFI/BOOT"], check=True)
    subprocess.run(["mcopy", "-i", str(fat), str(loader), "::/EFI/BOOT/BOOTX64.EFI"], check=True)
    image = temp / "OpalPba.img"
    mbr = bytearray(512)
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, sectors - 1)
    mbr[510:512] = b"\x55\xaa"
    with image.open("wb") as stream:
        stream.truncate(sectors * 512)
        for offset, data in ((0, mbr), (512, header(1, sectors - 1, 2)), (1024, entries),
                             (start * 512, fat.read_bytes()),
                             ((sectors - 33) * 512, entries),
                             ((sectors - 1) * 512, header(sectors - 1, 1, sectors - 33))):
            stream.seek(offset)
            stream.write(data)
    image.replace(out / "OpalPba.img")
print(f"UEFI-only, 512-byte LBA PBA image: {out / 'OpalPba.img'}")
