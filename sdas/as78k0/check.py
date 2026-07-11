#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def assembler_mnemonics(path):
    return set(
        re.findall(
            r'\{\s*NULL,\s*"([^".][^"]*)",\s*S_K78K0_',
            path.read_text(errors="ignore"),
        )
    )


def fixture_instructions(path):
    instructions = {}
    for line_number, raw in enumerate(path.read_text(errors="ignore").splitlines(), 1):
        line = raw.split(";", 1)[0].strip().lower()
        if not line or line.startswith(".") or line.endswith(":"):
            continue
        match = re.match(r"([a-z][a-z0-9]*)\b", line)
        if match:
            instructions[line_number] = match.group(1)
    return instructions


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


def emitted_source_lines(path):
    emitted = set()
    for line in path.read_text(errors="ignore").splitlines():
        if len(line) < 28 or not re.fullmatch(r"[0-9A-F]{4}", line[3:7]):
            continue
        tail = re.match(r"^\s+(\d+)\s+", line[27:])
        if tail and line[8:27].strip():
            emitted.add(int(tail.group(1)))
    return emitted


def main():
    source = Path(sys.argv[1])
    listing = Path(sys.argv[2])
    expected = expectations(source)
    actual = listing_bytes(listing)
    emitted = emitted_source_lines(listing)
    instructions = fixture_instructions(source)
    table_mnemonics = assembler_mnemonics(Path(__file__).with_name("k78k0pst.c"))
    fixture_mnemonics = set(instructions.values())
    failures = []

    missing_mnemonics = sorted(table_mnemonics - fixture_mnemonics)
    if missing_mnemonics:
        failures.append("fixture is missing assembler mnemonics: " + " ".join(missing_mnemonics))

    unknown_mnemonics = sorted(fixture_mnemonics - table_mnemonics)
    if unknown_mnemonics:
        failures.append("fixture contains unknown mnemonics: " + " ".join(unknown_mnemonics))

    for line_number, mnemonic in instructions.items():
        if line_number not in emitted:
            failures.append(f"{source}:{line_number}: {mnemonic} produced no instruction bytes")

    for line_number, expected_bytes in expected.items():
        actual_bytes = actual.get(line_number)
        if actual_bytes != expected_bytes:
            failures.append(
                f"{source}:{line_number}: expected {expected_bytes.hex(' ').upper()}, "
                f"got {actual_bytes.hex(' ').upper() if actual_bytes is not None else 'no output'}"
            )

    if failures:
        raise SystemExit("\n".join(failures))
    print(
        f"PASS: {len(expected)} expected encodings, {len(instructions)} instructions, "
        f"{len(fixture_mnemonics)} mnemonics"
    )


if __name__ == "__main__":
    main()
