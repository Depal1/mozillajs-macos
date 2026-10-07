#!/bin/bash
# build.sh — configure + make + install + package for mozjs185 macos (libjs.make equivalent).
#
# Requirements (macOS host):
#   - Xcode command-line tools (clang), perl >= 5.6, zip, pkg-config
#   - python2.7 (2.x, NOT python3 — configure's version check rejects 3.x):
#     install the python.org 2.7.18 64-bit pkg:
#     https://www.python.org/ftp/python/2.7.18/python-2.7.18-macosx10.9.pkg
#     (auto-detected; override with PYTHON2=/path/to/python2.7)
#   - NSPR >= 4.7 (arm64-darwin support needs 4.35): built separately, e.g.
#       mkdir nspr-build && cd nspr-build
#       CC="clang -arch arm64" ../nspr-4.35/nspr/configure \
#           --prefix="$PWD/../nspr-install" --enable-64bit && make -j && make install
#     (override with NSPR_PREFIX; default ../nspr-install-$ARCH)
#
# Flavors: ARCH=arm64 (default) | x86_64; JIT=1 keeps the method/trace JIT
# (x86_64 reference builds only — there is no arm64 JIT backend).
#
# Usage: ./build.sh           # configure + make + make install into js/src/<builddir>/dist
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$ROOT/js/src"
ARCH="${ARCH:-arm64}"
JIT="${JIT:-0}"
BUILD_DIR="${BUILD_DIR:-build-$ARCH-interp}"
BUILD="$SRCDIR/$BUILD_DIR"
NSPR_PREFIX="${NSPR_PREFIX:-$(cd "$ROOT/.." && pwd)/nspr-install-$ARCH}"

PYTHON2="${PYTHON2:-$(command -v python2.7 || true)}"
if [ -z "$PYTHON2" ]; then
    echo "ERROR: python2.7 is required by configure (2.x only; python.org 2.7.18 pkg)." >&2
    exit 1
fi
if [ ! -x "$NSPR_PREFIX/lib/libnspr4.dylib" ] && [ ! -f "$NSPR_PREFIX/lib/libnspr4.a" ]; then
    echo "ERROR: NSPR not found at $NSPR_PREFIX (set NSPR_PREFIX)." >&2
    exit 1
fi

JIT_FLAGS="--disable-methodjit --disable-tracejit"
if [ "$JIT" = "1" ]; then JIT_FLAGS=""; fi

mkdir -p "$BUILD"
cd "$BUILD"

# NOTE: this autoconf-2.13 configure does NOT support VAR=VALUE command-line
# arguments (they parse as positional host/target args) — pass the toolchain
# via environment, as below.
CC=clang CXX=clang++ \
CFLAGS="-arch $ARCH -Wno-error=implicit-int -Wno-error=implicit-function-declaration" \
CXXFLAGS="-arch $ARCH -std=gnu++98 -Wno-error=implicit-int -Wno-error=implicit-function-declaration" \
PYTHON="$PYTHON2" MACOSX_DEPLOYMENT_TARGET=11.0 \
"$SRCDIR/configure" --prefix="$BUILD/dist" \
    --host=$ARCH-apple-darwin --build=$ARCH-apple-darwin \
    --disable-shared-js --enable-threadsafe --disable-tests \
    --with-system-nspr --with-nspr-prefix="$NSPR_PREFIX" \
    $JIT_FLAGS --disable-warnings-as-errors

JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
make -j"$JOBS"
make install

# Package (§6.2 artifact layout: include/ + lib/js_static.a; jsdbg_static is
# never packaged — its debug LDFLAGS want -framework ExceptionHandling).
STAGE="$ROOT/dist/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/include" "$STAGE/lib"
if [ -d "$BUILD/dist/include/js" ]; then
    cp -R "$BUILD/dist/include/js/." "$STAGE/include/"
else
    cp -R "$BUILD/dist/include/." "$STAGE/include/"
fi
cp -f "$BUILD/dist/lib/libjs_static.a" "$STAGE/lib/js_static.a"
TARBALL="$ROOT/dist/mozjs-js185-macos-$ARCH.tar.gz"
mkdir -p "$ROOT/dist"
tar -C "$STAGE" -czf "$TARBALL" include lib
echo "PACKAGED: $TARBALL"
