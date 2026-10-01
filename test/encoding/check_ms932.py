"""Compare the C++ decoder with beatoraja's Java MS932 decoding behavior."""
import argparse
import random
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("decoder", type=Path)
parser.add_argument("--java", default="java")
parser.add_argument("--corpus", type=Path, help="Also check BMS files in this directory")
args = parser.parse_args()

samples = [b""] + [bytes([a]) for a in range(256)]
samples += [bytes([a, b]) for a in range(256) for b in range(256)]
rng = random.Random(932)
samples += [bytes(rng.randrange(256) for _ in range(rng.randrange(256)))
            for _ in range(10000)]
# Exercise the bulk path, error fallback, and InputStreamReader chunk boundaries.
for length in (7, 8, 15, 16, 31, 32, 63, 64, 1023, 1024, 8191, 8192, 65536):
    for data in (b"\x83\x5c", b"\x81\xad", b"\x80", b"\xf0\x40", b"\x81"):
        samples.append(b"a" * length + data + b"b" * length)
if args.corpus:
    samples += [path.read_bytes() for path in sorted(args.corpus.rglob("*"))
                if path.is_file() and path.suffix.lower() in {".bms", ".bme", ".bml", ".pms"}]
records = b"".join(struct.pack(">I", len(sample)) + sample for sample in samples)
reference = subprocess.run(
    [args.java, str(Path(__file__).with_name("MS932Oracle.java"))],
    input=records, stdout=subprocess.PIPE, check=True).stdout
actual = subprocess.run([str(args.decoder.resolve())], input=records,
                        stdout=subprocess.PIPE, check=True).stdout

def unpack(data):
    pos = 0
    while pos < len(data):
        length, = struct.unpack_from(">I", data, pos)
        pos += 4
        if pos + length > len(data):
            raise ValueError("Truncated output record")
        yield data[pos:pos + length]
        pos += length

expected_records, actual_records = list(unpack(reference)), list(unpack(actual))
assert len(expected_records) == len(actual_records) == len(samples)
for sample, expected, result in zip(samples, expected_records, actual_records):
    if expected != result:
        raise SystemExit(f"MS932 mismatch for {sample[:128].hex()} ({len(sample)} bytes): "
                         f"Java={expected.hex()}, C++={result.hex()}")
print(f"Java MS932 parity: {len(samples)} cases passed")
