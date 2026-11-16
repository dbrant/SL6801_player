"""Build an injectable app with zig cc (clang + lld, from the ziglang pip package).

Usage: build.py <app dir>   e.g. build.py sdk/apps/hello
Outputs <app dir>/build/: app.elf, app.bin, app.json. app.json is what `assets.py pack --app`
reads: load address, flash offset and the BL patches pointing firmware call sites at app symbols.
The app dir's patches.json lists the patches as {"bl_at": "0x...", "target": "symbol"}.
"""
import json
import os
import struct
import subprocess
import sys

SDK = os.path.dirname(os.path.abspath(__file__))
FLASH = 0xC00000

CFLAGS = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m4", "-mthumb", "-Os", "-std=c11",
          "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pic",
          "-g", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror=implicit-function-declaration"]
LDFLAGS = ["-nostdlib", "-static", "-Wl,--gc-sections", "-Wl,--no-dynamic-linker", "-Wl,-z,norelro"]


def zig(*args):
    cmd = [sys.executable, "-m", "ziglang", *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"failed: {' '.join(args[:2])}")
    return r.stdout


def elf_symbols(path):
    """Return {name: value} for defined symbols in an ELF32 little-endian file."""
    d = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for o in range(s[4], s[4] + s[5], 16):
            name, value, _size, _info, _other, shndx = struct.unpack_from("<IIIBBH", d, o)
            if name and shndx:
                n = d[strtab[4] + name:d.index(0, strtab[4] + name)].decode()
                out[n] = value
    return out


def main():
    app = os.path.abspath(sys.argv[1])
    out = os.path.join(app, "build")
    os.makedirs(out, exist_ok=True)
    srcs = sorted(os.path.join(app, f) for f in os.listdir(app) if f.endswith(".c"))
    elf, binf = os.path.join(out, "app.elf"), os.path.join(out, "app.bin")
    zig("cc", *CFLAGS, "-I", os.path.join(SDK, "include"), "-I", app, *srcs,
        *LDFLAGS, "-T", os.path.join(SDK, "app.ld"), "-o", elf)
    zig("objcopy", "-O", "binary", "--only-section=.text", elf, binf)

    syms = elf_symbols(elf)
    base = 0x00DD0000
    patches = json.load(open(os.path.join(app, "patches.json")))
    resolved = []
    for p in patches:
        if p["target"] not in syms:
            raise SystemExit(f"symbol {p['target']} not found")
        resolved.append(dict(bl_at=p["bl_at"], target=f"{syms[p['target']] & ~1:#x}", symbol=p["target"],
                             expect=p.get("expect")))
    info = dict(bin=binf, load=f"{base:#x}", flash_offset=f"{base - FLASH:#x}",
                size=os.path.getsize(binf), patches=resolved)
    with open(os.path.join(out, "app.json"), "w") as f:
        json.dump(info, f, indent=1)
    print(f"built {binf}: {info['size']} bytes at {base:#x}")
    for p in resolved:
        print(f"  patch bl at {p['bl_at']} -> {p['symbol']} ({p['target']})")


if __name__ == "__main__":
    main()
