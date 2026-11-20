"""Cross-check the actual C formatter against Python's arbitrary precision ints."""
import ctypes
from pathlib import Path
import random

root = Path(__file__).resolve().parent.parent
library = ctypes.CDLL(str(root / "out/tests/libsmart-data.so"))
pointer = ctypes.POINTER(ctypes.c_ubyte)
library.SmartDecimal.argtypes = [pointer, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
library.SmartDecimal.restype = ctypes.c_ubyte
library.SmartDataBytes.argtypes = [pointer, ctypes.c_char_p, ctypes.c_size_t]
library.SmartDataBytes.restype = ctypes.c_ubyte
rng = random.Random(12345)
numbers = [0, 1, 9, 10, 255, 256, 2**64, 2**128 - 1]
numbers += [rng.getrandbits(128) for _ in range(10000)]
for value in numbers:
    raw = (ctypes.c_ubyte * 16).from_buffer_copy(value.to_bytes(16, "little"))
    text = ctypes.create_string_buffer(50)
    assert library.SmartDecimal(raw, 16, text, 50)
    assert text.value.decode() == str(value)
    assert library.SmartDataBytes(raw, text, 50)
    assert text.value.decode() == str(value * 512000)
print("PASS: 10008 full-width counters and byte totals match Python arbitrary-precision integers.")
