"""Find Thumb-2 BL/B.W instructions that target given addresses in the flash dump.

Usage: xref.py firmware.bin 0xADDR [0xADDR ...]
"""
import struct
import sys

FLASH = 0xC00000


def decode_bl(hw1, hw2, pc):
    """Return (kind, target) for a 32-bit BL / B.W (T4) at pc, else None."""
    if hw1 & 0xF800 != 0xF000:
        return None
    if hw2 & 0xD000 == 0xD000:
        kind = "bl"
    elif hw2 & 0xD000 == 0x9000:
        kind = "b.w"
    else:
        return None
    s = (hw1 >> 10) & 1
    j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
    i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
    imm = (s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
    if s:
        imm -= 1 << 25
    return kind, pc + 4 + imm


# (flash start, flash end, run address of flash start)
CODE = ((0xE030, 0xF030, 0x804800), (0xF030, 0x18A8E, FLASH + 0xF030),
        (0x39470, 0x4731C, 0x80CC40), (0xD6000, 0x182270, FLASH + 0xD6000))


def find_calls(d, targets, ranges=CODE):
    out = []
    for lo, hi, run in ranges:
        for o in range(lo, hi - 4, 2):
            hw1, hw2 = struct.unpack_from("<HH", d, o)
            pc = run + (o - lo)
            r = decode_bl(hw1, hw2, pc)
            if r and r[1] in targets:
                out.append((pc, r[0], r[1]))
    return out


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    targets = {int(a, 16) & ~1 for a in sys.argv[2:]}
    for addr, kind, tgt in find_calls(d, targets):
        print(f"{addr:#x}: {kind} {tgt:#x}")
