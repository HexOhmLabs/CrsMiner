#!/bin/sh
# Rebuild the native engines. Needs clang + lld (apt install clang lld). Run from the project root:
#   sh native/build_native.sh
# Produces a freestanding Windows x64 DLL (no imports, no CRT) and a Linux .so used by the tests.
set -e
OUT=${1:-native}
clang -O3 -ffreestanding -fno-builtin -fPIC -shared -o "$OUT/libcrs_sha256d_test.so" native/crs_sha256d.c
# Compile with the mingw-style triple (exposes the SHA intrinsics headers), link with lld-link.
clang --target=x86_64-pc-windows-gnu -O3 -ffreestanding -fno-builtin -fno-stack-protector -c \
  -o "$OUT/crs_sha256d_win64.obj" native/crs_sha256d.c
lld-link /dll /noentry /nodefaultlib /machine:x64 /export:crs_abi_version /export:crs_cpu_features \
  /export:crs_engine /export:crs_set_engine /export:crs_hash_nonce /export:crs_scan \
  /out:"$OUT/crs_sha256d_win64.dll" "$OUT/crs_sha256d_win64.obj"
rm -f "$OUT/crs_sha256d_win64.obj" "$OUT/crs_sha256d_win64.lib"
