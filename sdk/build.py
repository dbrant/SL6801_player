"""Build an injectable app.

Usage: build.py [--zig] <app dir>   e.g. build.py sdk/apps/hello
Uses arm-none-eabi-gcc (ARM_GCC_BIN, default C:/Tools/arm-gnu-toolchain/bin), or zig cc from the
ziglang pip package with --zig. Outputs <app dir>/build/: app.elf, app.bin, app.json (and app.map,
app.lst with gcc). app.json is what `assets.py pack --app` reads: load address, flash offset and
the BL patches pointing firmware call sites at app symbols. The app dir's patches.json lists the
patches as {"bl_at": "0x...", "target": "symbol", "expect": "0x..."}.
"""
import json
import os
import struct
import subprocess
import sys

SDK = os.path.dirname(os.path.abspath(__file__))
FLASH = 0xC00000
GCC_BIN = os.environ.get("ARM_GCC_BIN", "C:/Tools/arm-gnu-toolchain/bin")

COMMON = ["-mthumb", "-Os", "-std=c11", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
          "-g", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
          "-Werror=implicit-function-declaration"]
GCC_FLAGS = ["-mcpu=cortex-m4", "-mfloat-abi=soft", "-fno-pic", "-fno-tree-loop-distribute-patterns", *COMMON]
ZIG_FLAGS = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m4", "-fno-pic", *COMMON]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"failed: {os.path.basename(cmd[0])} {' '.join(cmd[1:3])}")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def compile_gcc(srcs, incs, ld, out, entry):
    elf = os.path.join(out, "app.elf")
    run([f"{GCC_BIN}/arm-none-eabi-gcc", *GCC_FLAGS, *incs, *srcs, "-nostdlib", "-T", ld, f"-Wl,-e,{entry}",
         "-Wl,--gc-sections", f"-Wl,-Map={os.path.join(out, 'app.map')}", "-lgcc", "-o", elf])
    run([f"{GCC_BIN}/arm-none-eabi-objcopy", "-O", "binary", "--only-section=.text", elf,
         os.path.join(out, "app.bin")])
    with open(os.path.join(out, "app.lst"), "w") as f:
        f.write(run([f"{GCC_BIN}/arm-none-eabi-objdump", "-d", "-S", elf]))
    return elf


def compile_zig(srcs, incs, ld, out, entry):
    zig = [sys.executable, "-m", "ziglang"]
    elf = os.path.join(out, "app.elf")
    run([*zig, "cc", *ZIG_FLAGS, *incs, *srcs, "-nostdlib", "-static", "-Wl,--gc-sections", f"-Wl,-e,{entry}",
         "-Wl,--no-dynamic-linker", "-Wl,-z,norelro", "-T", ld, "-o", elf])
    run([*zig, "objcopy", "-O", "binary", "--only-section=.text", elf, os.path.join(out, "app.bin")])
    return elf


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
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zig", action="store_true", help="build with zig cc instead of arm-none-eabi-gcc")
    ap.add_argument("app")
    a = ap.parse_args()
    app = os.path.abspath(a.app)
    out = os.path.join(app, "build")
    os.makedirs(out, exist_ok=True)
    srcs = sorted(os.path.join(app, f) for f in os.listdir(app) if f.endswith(".c"))
    srcs += [os.path.join(SDK, "src", f) for f in sorted(os.listdir(os.path.join(SDK, "src"))) if f.endswith(".c")]
    incs = ["-I", os.path.join(SDK, "include"), "-I", app]
    ld = os.path.join(SDK, "app.ld")
    patches = json.load(open(os.path.join(app, "patches.json")))
    elf = (compile_zig if a.zig else compile_gcc)(srcs, incs, ld, out, patches[0]["target"])
    binf = os.path.join(out, "app.bin")

    syms = elf_symbols(elf)
    base = 0x00DD0000
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
