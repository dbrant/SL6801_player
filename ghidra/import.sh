#!/usr/bin/env bash
# Creates the Ghidra project ghidra/project/sl6801.gpr with two programs from the flash dump:
# sl6801_app (main firmware) and sl6801_boot (bootloader). Re-running replaces both programs.
# Usage: ghidra/import.sh [path/to/firmware_2M.bin]   (GHIDRA_HOME defaults to C:/Tools/ghidra)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="${1:-$here/../firmware_2M.bin}"
ghidra="${GHIDRA_HOME:-C:/Tools/ghidra}"

mkdir -p "$here/project" "$here/input" "$here/out"
cp "$fw" "$here/input/sl6801_app.bin"
cp "$fw" "$here/input/sl6801_boot.bin"

for prog in sl6801_boot sl6801_app; do
	"$ghidra/support/analyzeHeadless.bat" "$here/project" sl6801 \
		-import "$here/input/$prog.bin" -overwrite \
		-loader BinaryLoader -loader-baseAddr 0xC00000 \
		-processor ARM:LE:32:Cortex -cspec default \
		-scriptPath "$here/scripts" \
		-preScript SL6801Setup.java \
		-postScript SL6801Fixups.java \
		-postScript SL6801Names.java "$here/out" \
		-log "$here/out/$prog.log" -scriptlog "$here/out/${prog}_script.log"
done
