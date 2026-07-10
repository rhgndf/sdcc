#!/usr/bin/env python3
"""Run an SDCC regression image with k0emu's library interface."""

import argparse
import sys
from pathlib import Path


SIMIF_ADDRESS = 0xFF00


def parse_ihx(path):
    image = {}
    upper = 0

    with path.open(encoding="ascii") as source:
        for line_number, line in enumerate(source, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"{path}:{line_number}: malformed Intel HEX record")

            record = bytes.fromhex(line[1:])
            if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
                raise ValueError(f"{path}:{line_number}: invalid Intel HEX record")

            count = record[0]
            address = (record[1] << 8) | record[2]
            record_type = record[3]
            data = record[4:4 + count]

            if record_type == 0x00:
                base = upper + address
                if base + count > 0x10000:
                    raise ValueError(f"{path}:{line_number}: data lies outside 78K0 memory")
                image.update((base + offset, value) for offset, value in enumerate(data))
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                upper = int.from_bytes(data, "big") << 4
            elif record_type == 0x04:
                upper = int.from_bytes(data, "big") << 16
            elif record_type not in (0x03, 0x05):
                raise ValueError(f"{path}:{line_number}: unsupported Intel HEX record type {record_type}")

    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--k0emu-dir", type=Path)
    parser.add_argument("--max-steps", type=int, default=100_000_000)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()

    if args.k0emu_dir and args.k0emu_dir.is_dir():
        sys.path.insert(0, str(args.k0emu_dir.resolve()))

    try:
        from k0emu.devices import MemoryDevice
        from k0emu.processor import Processor, RunState
    except ImportError as error:
        parser.error(f"cannot import k0emu: {error}")

    class RegressionMemory(MemoryDevice):
        def __init__(self):
            super().__init__("memory", size=0x10000, fill=0x00)
            self.awaiting_character = False
            self.stopped = False

        def write(self, register, value):
            if register != SIMIF_ADDRESS:
                super().write(register, value)
            elif self.awaiting_character:
                sys.stdout.buffer.write(bytes((value,)))
                sys.stdout.buffer.flush()
                self.awaiting_character = False
            elif value == ord("p"):
                self.awaiting_character = True
            elif value == ord("s"):
                self.stopped = True

    image = parse_ihx(args.image)
    processor = Processor()
    memory = RegressionMemory()
    processor.bus.add_device(memory, (0x0000, 0xFFFF))
    for address, value in image.items():
        memory.load(address, bytes((value,)))
    processor.reset()

    for _ in range(args.max_steps):
        if memory.stopped:
            break
        processor.step()
        if processor.run_state != RunState.RUNNING:
            raise RuntimeError(f"processor stopped at PC=0x{processor.pc:04x}")
    else:
        raise RuntimeError(f"step limit reached at PC=0x{processor.pc:04x}")

    print(f"{len(image)} words read from {args.image}")
    print(f"Total time since last reset= 0 sec ({processor.total_cycles} clks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
