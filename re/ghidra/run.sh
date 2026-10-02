#!/bin/sh
# Import WELLTRIS.EXE into a headless Ghidra project and dump decompiled C to wt_decomp.c.
# Needs GHIDRA (default ~/tools/ghidra_*) and, on aarch64, a natively built decompiler.
set -e
cd "$(dirname "$0")"
GHIDRA=${GHIDRA:-$(ls -d ~/tools/ghidra_*_PUBLIC | tail -1)}
mkdir -p proj
"$GHIDRA/support/analyzeHeadless" "$PWD/proj" wt -import "$PWD/../../original/welltris.exe" -overwrite \
    -scriptPath "$PWD" -postScript ApplyNames.java "$PWD/names.txt" -postScript DumpAll.java "$PWD/wt_decomp.c" > headless.log 2>&1
grep -c '=====' wt_decomp.c
