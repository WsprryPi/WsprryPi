#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent exact-rational oracle for binary64 Hz to integer nHz."""
from fractions import Fraction
import math
import random
import struct
import subprocess
import sys

rng = random.Random(0x575450)
values = [0.0, -0.0, math.inf, -math.inf, math.nan, 0.5e-9, 1e-9,
          0.1, 137500.123456789, 14097100.0, 18446744073.709551616]
for value in list(values):
    if math.isfinite(value):
        values += [math.nextafter(value, -math.inf), math.nextafter(value, math.inf)]
for exponent in range(-1074, 36):
    values.append(math.ldexp(1.0, exponent))
for _ in range(400):
    values.append(math.ldexp(rng.uniform(0.5, 1.0), rng.randrange(-35, 36)))
for _ in range(400):
    values.append(rng.uniform(18_446_744_070, 18_446_744_075))

def expected(value):
    if not math.isfinite(value) or value <= 0:
        return "rejected"
    exact = Fraction.from_float(value) * 1_000_000_000
    rounded = (2 * exact.numerator + exact.denominator) // (2 * exact.denominator)
    return str(rounded) if 0 < rounded < 2**64 else "rejected"

data = "".join(f'{struct.unpack(">Q", struct.pack(">d", v))[0]:016x}\n' for v in values)
result = subprocess.run([sys.argv[1], "--frequency-vectors"], input=data,
                        text=True, capture_output=True, check=True, timeout=30)
actual = result.stdout.splitlines()
assert len(actual) == len(values), (len(actual), len(values), result.stderr)
for value, got in zip(values, actual):
    assert got == expected(value), (value.hex(), got, expected(value))
print(f"Independent WTP frequency vectors passed: {len(values)}")
