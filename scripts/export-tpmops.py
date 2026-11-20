#!/usr/bin/env python3
"""Export public/private TPM2B blobs; never unseal or access a TPM automatically."""
import argparse
import json
from pathlib import Path
import struct

GUID = "a4e9a95e-5fde-4bcb-9f44-2b487f03395d"
NAME = f"TPMTest-{{{GUID}}}"
root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
source = parser.add_mutually_exclusive_group()
source.add_argument("--efivar", type=Path,
                    help="efivarfs file, including its 4-byte attribute prefix (default: real TPMTest efivar)")
source.add_argument("--record", type=Path, help="raw TPMWRAP1 record, WITHOUT efivarfs attributes")
parser.add_argument("--out", type=Path, default=root / "out/tpmops-export")
args = parser.parse_args()

attributes = None
if args.record:
    data = args.record.read_bytes()
else:
    path = args.efivar or Path(f"/sys/firmware/efi/efivars/{NAME}-{GUID}")
    raw = path.read_bytes()
    if len(raw) < 4:
        raise SystemExit("Truncated efivarfs attribute prefix")
    attributes = struct.unpack_from("<I", raw)[0]
    if attributes not in (3, 7):
        raise SystemExit(f"Unexpected attributes: 0x{attributes:08x}")
    data = raw[4:]

if not 40 <= len(data) <= 2200 or data[:8] != b"TPMWRAP1":
    raise SystemExit("Invalid TPMOps record header/length")
version, header, flags, template, name_len, public_len, private_len, reserved = struct.unpack_from("<8I", data, 8)
if (version, header, flags, template, reserved) != (1, 40, 1, 1, 0):
    raise SystemExit("Unsupported record version, flags or parent template")
if name_len != 36 or not 2 <= public_len <= 1024 or not 2 <= private_len <= 1024:
    raise SystemExit("Invalid blob sizes")
if len(data) != 40 + name_len + public_len + private_len:
    raise SystemExit("Truncated or trailing record data")
name = data[40:40 + name_len]
public = data[40 + name_len:40 + name_len + public_len]
private = data[40 + name_len + public_len:]
for label, item in (("name", name), ("public", public), ("private", private)):
    if int.from_bytes(item[:2], "big") + 2 != len(item):
        raise SystemExit(f"Invalid TPM2B size: {label}")
if name[2:4] != b"\x00\x0b":
    raise SystemExit("Parent name is not SHA256")

outputs = {
    "public.bin": public,
    "private.bin": private,
    "parent.name": name[2:],
    "record.bin": data,
    "metadata.json": (json.dumps({
        "variable": NAME, "vendor_guid": GUID, "attributes": attributes,
        "parent_hierarchy": "owner", "parent_template": 1,
        "parent_name": name[2:].hex(), "object_auth_required": True, "pcr_policy": None,
    }, indent=2) + "\n").encode(),
}
args.out.mkdir(parents=True, exist_ok=True)
if any((args.out / file).exists() for file in outputs):
    raise SystemExit("Output files already exist; select a fresh --out directory")
for file, content in outputs.items():
    with (args.out / file).open("xb") as stream:
        stream.write(content)
print(f"Exported TPM2B public/private and metadata to {args.out}")
print("No object password or plaintext was exported; no TPM command was sent.")
