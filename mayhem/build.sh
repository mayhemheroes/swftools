#!/usr/bin/env bash
#
# mayhem/build.sh — build swftools' `swfdump` CLI, the Mayhem file-input target.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/mayhemheroes/base) exports the build contract: CC, CXX, LIB_FUZZING_ENGINE,
# SANITIZER_FLAGS (ASan+UBSan, halting), SRC=/mayhem.
#
# swftools specifics:
#   * Old autotools project. The Mayhem target is the existing `swfdump` CLI (it parses a .swf
#     file given on argv), fuzzed file-input style (Mayhemfile: swfdump @@). There is NO libFuzzer
#     harness here, so there is no <fuzzer>-standalone artifact — `swfdump` is itself the
#     run-once, file-input reproducer.
#   * We build only what swfdump needs (lib/librfxswf + lib/libbase, then src/swfdump), not the
#     whole tree (pdf2swf/python/ruby/avi2swf pull in heavy optional deps we don't fuzz here).
#   * The PROJECT is compiled with $SANITIZER_FLAGS via CFLAGS so the fuzzed code is instrumented.
#     swftools' Makefile.common links with `@CC@` + LIBS=@LDFLAGS@ @LIBS@ and does NOT thread CFLAGS
#     into the link step, so the sanitizer runtime must also be on the LINK line — we pass it via
#     LDFLAGS (configure folds LDFLAGS into LIBS). With an empty SANITIZER_FLAGS this is harmless.
#   * Old C with modern clang needs -Wno-error and several -Wno-* to compile (additive: flags only,
#     no upstream edits).
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs from the ENVIRONMENT (overridable), with sane defaults. SANITIZER_FLAGS uses `=`
# (not `:=`) so an explicit empty --build-arg SANITIZER_FLAGS= is honored (no sanitizers).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
export CC CXX DEBUG_FLAGS

cd "$SRC"

# Quiet the many warnings/implicit-int/etc. in this old codebase so clang's default diagnostics
# don't fail the build. Purely compiler flags; no source edits.
COMPAT_CFLAGS="-Wno-error -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-return-type -Wno-deprecated-non-prototype -Wno-format -fcommon"

# swftools' SWF bit-reader does benign signed/oversized shifts (e.g. rfxswf.c:232) that fire on even
# VALID .swf input; under the default -fno-sanitize-recover that aborts on ~every input, so the
# target can't fuzz. Relax ONLY the `shift` check (ASan + the rest of UBSan stay on and halting) when
# UBSan is in play; the empty off-switch (SANITIZER_FLAGS=) stays untouched.
SAN="$SANITIZER_FLAGS"
case "$SANITIZER_FLAGS" in *undefined*) SAN="$SANITIZER_FLAGS -fno-sanitize=shift" ;; esac

# Compile asan_options.c: bakes detect_leaks=0 into swfdump to prevent
# LeakSanitizer from ptrace-attaching at exit. Mayhem already holds the process
# under ptrace for coverage collection; a second ptrace fails → LSan calls
# _exit(-1) before edges are recorded → 0-edge "Run Failed". Strong symbol wins
# over the ASan runtime's weak __asan_default_options.
ASAN_OBJ="$SRC/mayhem/asan_options.o"
$CC -c "$SRC/mayhem/asan_options.c" -o "$ASAN_OBJ"

# Instrument the PROJECT with the sanitizers (CFLAGS) and ensure the sanitizer runtime lands on the
# link line too (LDFLAGS -> folded into LIBS by configure). With SANITIZER_FLAGS empty this is a no-op.
# Thread DEBUG_FLAGS after SANITIZER_FLAGS so -gdwarf-3 overrides the bare -g from SANITIZER_FLAGS.
# asan_options.o is appended to LDFLAGS so the autotools link step for swfdump includes it
# (Makefile.common.in: L=@CC@ $(DEFS); LIBS=@LDFLAGS@ @LIBS@ — object files in LDFLAGS are passed
# straight to the linker alongside the other objects and archives).
./configure \
  CC="$CC" CXX="$CXX" \
  CFLAGS="$SAN $COMPAT_CFLAGS $DEBUG_FLAGS" \
  CXXFLAGS="$SAN $COMPAT_CFLAGS $DEBUG_FLAGS" \
  LDFLAGS="$SAN $DEBUG_FLAGS $ASAN_OBJ"

# Build only the libraries swfdump links against, then swfdump itself.
make -C lib -j"$MAYHEM_JOBS" librfxswf.a libbase.a
make -C src -j"$MAYHEM_JOBS" swfdump

cp src/swfdump /mayhem/swfdump
echo "built /mayhem/swfdump"
