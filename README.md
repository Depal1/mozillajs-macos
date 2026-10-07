# mozillajs-macos

macOS (Apple Silicon first) build of the SpiderMonkey 1.8.5 fork used by
Oolite — mirrors the [mozillajs-linux](https://github.com/OoliteProject/mozillajs-linux)
pattern: patched vendored sources, build CI, and a release artifact
(`include/` + `lib/js_static.a`) that oolite consumes; oolite itself never
vendors js patches.

## Pinned source

- Upstream: `OoliteProject/spidermonkey-ff4`
- Commit: **`c463e95ea5d1d780301e7f3792783771381125f0`** (2017-10-08, "GCC 7
  compile fix. Closes #264." — repo HEAD at adoption, 2026-10-07)
- Vendored here as `js/` with the four macos/arm64 patches under `patches/`
  applied (also kept as separate patch files for upstream PRs to
  `spidermonkey-ff4` — see `patches/PR-NOTES.md`).

## Deviations from upstream js185 (both intentional, both documented)

1. **JIT off — interpreter-only.** No arm64 JIT backend exists in this 2011
   codebase (no aarch64 case in `assembler/wtf/Platform.h`;
   `MacroAssembler.h:57-58` `#error`s). The configure CPU case sets
   `ENABLE_TRACEJIT`/`ENABLE_METHODJIT`/`ENABLE_MONOIC`/`ENABLE_POLYIC` empty
   and defines `JS_PUNBOX64` for the 64-bit value layout.
2. **pcre regexes.** `aarch64`/`arm64` are routed to the pcre regex path
   (same as the existing powerpc/sparc precedent, `Makefile.in:378`):
   `ENABLE_YARR_JIT` is defined nowhere, so pcre is the fallback engine
   (`jsregexpinlines.h:48-52`).

## Toolchain (first build host)

- Xcode 26.6 (Build 17F113), Apple clang 21.0.0, arm64-apple-darwin25.5.0
- macOS deployment target: `MACOSX_DEPLOYMENT_TARGET=11.0` — the
  README-documented lower floor for this codebase; in a static lib it buys
  headroom vs. the oolite app minimum (26.0) at zero cost.
- NSPR 4.35 (`md5 5e0acf9fbdde85181bddd510f4624841`) built separately per
  architecture; the repo's vendored `nsprpub` (4.8.7) has no arm64 support
  and is NOT used.
- **python2.7 is REQUIRED by configure** (2.x only — the version check
  rejects 3.x; used for the build plumbing). Install the python.org 2.7.18
  64-bit pkg; do not use pyenv/brew python@2/python3.
- perl >= 5.6 (hard configure dependency), zip, pkg-config.

## Build

```sh
# after installing python.org python2.7 and building NSPR per arch:
NSPR_PREFIX="$PWD/../nspr-install-arm64" ./build.sh
# x86_64 reference (Rosetta): ARCH=x86_64 NSPR_PREFIX=... ./build.sh
# x86_64 JIT reference:       ARCH=x86_64 JIT=1   NSPR_PREFIX=... ./build.sh
```

Gotcha encoded in `build.sh`: this autoconf-2.13 configure does not support
`VAR=VALUE` command-line arguments (they parse as positional host/target
args) — toolchain vars go through the environment.

Smoke harness: `tools/smoke.cpp` (128 GC iterations over 64 rooted live
objects; asserts every `JSObject*` < 2^47, per `JSVAL_TAG_SHIFT == 47`).
Benchmarks: `tools/bench.sh <js-shell> <label>`.

## Benchmark table (4-cell)

Planned cells: native-arm64-interp / Rosetta-x86_64-interp /
Rosetta-x86_64-JIT / Linux-x86_64-JIT.

| Cell | math | strings | arrays | objects |
|---|---|---|---|---|
| native-arm64-interp | DEFERRED | DEFERRED | DEFERRED | DEFERRED |
| Rosetta-x86_64-interp | DEFERRED | DEFERRED | DEFERRED | DEFERRED |
| Rosetta-x86_64-JIT | DEFERRED | DEFERRED | DEFERRED | DEFERRED |
| Linux-x86_64-JIT | DEFERRED (one-off Linux run, oolite Task 15) | | | |

Rosetta 2 confirmed present on the first build host; kernel timings land
here once the js builds complete.

## jstests deltas

DEFERRED — `tests/jstests.py` needs python2 on PATH, which is pending on the
build host (python.org 2.7.18 pkg; a human-install step). The delta
(arm64-interp vs x86_64-JIT vs x86_64-interp) will be recorded here per the
acceptance criteria as soon as the runs execute.

## Status

- [x] Sources fetched + pinned, 4 patches applied + verified
      (config.guess → `aarch64-apple-darwin25.5.0`; `sh -n configure` clean;
      host canonicalization `arm64-apple-darwin` proven by a live configure
      run)
- [x] NSPR 4.35 built + installed (arm64 and x86_64)
- [x] Conftest gate, C and C++ compiler checks pass under clang 21 with
      `-Wno-error=implicit-int -Wno-error=implicit-function-declaration`
- [ ] js build — **blocked: configure hard-requires python 2.x
      (`configure.in:984-986` errors "python was not found in $PATH");
      the host lacks python2** — python.org 2.7.18 pkg install pending
      (human step). Everything else is ready to rerun.
- [ ] Smoke, benchmarks, jstests deltas, release artifact/tag — blocked on
      the js build.

## License

MPL 2.0 (`LICENSE`). Nothing in oolite's tree depends on this repo's layout;
oolite consumes only the release artifact.
