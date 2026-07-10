#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def expectations(path):
    expected = {}
    for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
        match = re.search(r";\s*EXPECT:\s*((?:[0-9A-Fa-f]{2}\s*)+)$", line)
        if match:
            expected[line_number] = bytes.fromhex(match.group(1))
    return expected


def listing_bytes(path):
    actual = {}
    for line in path.read_text(errors="ignore").splitlines():
        if len(line) < 28 or not re.fullmatch(r"[0-9A-F]{4}", line[3:7]):
            continue
        byte_field = line[8:27].strip()
        tail = re.match(r"^\s+(\d+)\s+", line[27:])
        if tail and re.fullmatch(r"(?:[0-9A-F]{2}(?:\s+|$))+", byte_field):
            actual[int(tail.group(1))] = bytes.fromhex(byte_field)
    return actual


def main():
    source = Path(sys.argv[1])
    listing = Path(sys.argv[2])
    expected = expectations(source)
    actual = listing_bytes(listing)
    failures = []

    for line_number, expected_bytes in expected.items():
        actual_bytes = actual.get(line_number)
        if actual_bytes != expected_bytes:
            failures.append(
                f"{source}:{line_number}: expected {expected_bytes.hex(' ').upper()}, "
                f"got {actual_bytes.hex(' ').upper() if actual_bytes is not None else 'no output'}"
            )

    if failures:
        raise SystemExit("\n".join(failures))
    print(f"PASS: {len(expected)} 78K0 instruction encodings")


if __name__ == "__main__":
    main()
