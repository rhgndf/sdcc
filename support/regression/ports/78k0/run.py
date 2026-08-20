#!/usr/bin/env python3
"""Run an SDCC regression image with k0emu's library interface."""

import argparse
import sys
from collections import deque
from pathlib import Path


SIMIF_ADDRESS = 0xFF00


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--k0emu-dir", type=Path)
    parser.add_argument("--max-steps", type=int, default=100_000_000)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()

    if args.k0emu_dir and args.k0emu_dir.is_dir():
        sys.path.insert(0, str(args.k0emu_dir.resolve()))

    try:
        from intelhex import IntelHex, IntelHexError
        from k0emu.devices import MemoryDevice
        from k0emu.processor import Processor, RunState
    except ImportError as error:
        parser.error(f"cannot import regression dependency: {error}")

    try:
        image = IntelHex(str(args.image))
    except (IntelHexError, OSError) as error:
        parser.error(f"cannot load {args.image}: {error}")
    if image and (image.minaddr() < 0 or image.maxaddr() >= 0x10000):
        parser.error(f"{args.image}: data lies outside 78K0 memory")

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

    processor = Processor()
    memory = RegressionMemory()
    processor.bus.add_device(memory, (0x0000, 0xFFFF))
    for start, end in image.segments():
        memory.load(start, image.tobinarray(start=start, end=end - 1).tobytes())
    processor.reset()

    recent_pcs = deque(maxlen=16)
    for _ in range(args.max_steps):
        if memory.stopped:
            break
        instruction_pc = processor.pc
        recent_pcs.append(instruction_pc)
        try:
            processor.step()
        except NotImplementedError as error:
            instruction = bytes(memory.read((instruction_pc + offset) & 0xFFFF) for offset in range(4))
            formatted = " ".join(f"{byte:02x}" for byte in instruction)
            trace = " -> ".join(f"0x{pc:04x}" for pc in recent_pcs)
            raise RuntimeError(
                f"unsupported instruction at PC=0x{instruction_pc:04x}: {formatted}; trace: {trace}"
            ) from error
        if processor.run_state != RunState.RUNNING:
            raise RuntimeError(f"processor stopped at PC=0x{processor.pc:04x}")
    else:
        raise RuntimeError(f"step limit reached at PC=0x{processor.pc:04x}")

    print(f"{len(image)} words read from {args.image}")
    print(f"Total time since last reset= 0 sec ({processor.total_cycles} clks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
