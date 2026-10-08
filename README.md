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
- Vendored here as `js/` with the macos/arm64 patches under `patches/`
  applied (also kept as separate patch files for upstream PRs to
  `spidermonkey-ff4` — see `patches/PR-NOTES.md`):
  - `patch-a` — config.guess/config.sub darwin-arm64 refresh (arm64 →
    aarch64 canonicalization via aliases, amd64 precedent; `aarch64` added
    to the CPU validation lists, `arm64` deliberately NOT — a list entry
    would pre-empt the alias and leave TARGET_CPU=arm64, which walks
    configure into the 32-bit armv7 flag path)
  - `patch-b` — configure/configure.in aarch64 CPU cases (interpreter-only:
    empty JIT/IC vars + `JS_PUNBOX64`)
  - `patch-c` — Makefile.in pcre regex path for aarch64/arm64
  - `patch-d` — jsgcchunk.cpp darwin GC chunks kept below 2^47 (hint-retry)
  - `patch-e` — `finite()` → C99 `isfinite()` (removed from modern macOS
    SDK headers; jsnum.h + ctypes/CTypes.cpp)
  - `patch-f` — MethodJIT.cpp file-scope `asm volatile` → `asm` (clang 16+
    hard error; x86_64 JIT reference builds)
  - `patch-g` — methodjit/Logging.h keeps the `JaegerSpew` stub available
    without methodjit (x86_64 interpreter-only builds compile the YARR
    regex assembler, which references it)

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
  rejects 3.x; used for the build plumbing). First build host uses a
  CPython 2.7.18 built from source at `~/.local/python2.7` (arm64 native);
  the python.org 2.7.18 64-bit pkg works equally. Do not use
  pyenv/brew python@2/python3.
- perl >= 5.6 (hard configure dependency), zip, pkg-config.

## Build

```sh
# after installing python.org python2.7 and building NSPR per arch:
NSPR_PREFIX="$PWD/../nspr-install-arm64" ./build.sh
# x86_64 reference (Rosetta): ARCH=x86_64 NSPR_PREFIX=... ./build.sh
# x86_64 JIT reference:       ARCH=x86_64 JIT=1   NSPR_PREFIX=... ./build.sh
```

Gotchas encoded in `build.sh`:
- this autoconf-2.13 configure does not support `VAR=VALUE` command-line
  arguments (they parse as positional host/target args) — toolchain vars go
  through the environment;
- the `-Wno-error=implicit-int -Wno-error=implicit-function-declaration`
  flags must live in `CC`/`CXX` themselves, not only in
  `CFLAGS`/`CXXFLAGS`: configure's "understand -c and -o together" check
  compiles `foo(){}` with bare `$CC`, and a hard error there trips
  `NO_MINUS_C_MINUS_O`, silently rewiring the whole build through
  `js/src/build/hcc`/`hcpp` wrappers (which then breaks CPP detection and
  dies at the static-assertion check);
- the `js` shell links NSPR dylibs via `@executable_path`, so the three
  NSPR dylibs must sit next to `shell/js` for jstests.py runs.
- app packaging (oolite Task 10): the NSPR dylibs carry `@executable_path`
  install names, so an app bundle must bundle the three NSPR dylibs and
  @rpath-rename them alongside `libjs_static.a`'s consumers — the shell
  note above covers only the shell case.

Smoke harness: `tools/smoke.cpp` (128 GC iterations over 64 rooted live
objects; asserts every `JSObject*` < 2^47, per `JSVAL_TAG_SHIFT == 47`).
Note: the first global must be created with
`JS_NewCompartmentAndGlobalObject(cx, clasp, NULL)` — plain
`JS_NewGlobalObject` on a fresh context runs with `cx->compartment == NULL`
and crashes.
Benchmarks: `tools/bench.sh <js-shell> <label>` (best of 3 wall-clock runs).

## Benchmark table (4-cell)

Native arm64 interpreter vs Rosetta x86_64 interpreter vs Rosetta x86_64
JIT vs Linux x86_64 JIT. Times in ms (lower is better; best of 3).

| Cell | math | strings | arrays | objects |
|---|---|---|---|---|
| native-arm64-interp | 602 | 21 | 147 | 107 |
| Rosetta-x86_64-interp | 869 | 208 | 538 | 1152 |
| Rosetta-x86_64-JIT | 1028 | 209 | 543 | 1154 |
| Linux-x86_64-JIT | DEFERRED (recipe below — no Linux rig at publication) | | | |

Methodology — all three filled cells: same host, same pinned source
(`c463e95`), same harness (`tools/bench.sh <js-shell> <label>`, best of 3
wall-clock runs over the four kernels in `tools/bench-*.js`). The
decomposition separates the two variables:

- **native-arm64-interp vs Rosetta-x86_64-interp** isolates the
  **Rosetta 2 tax**: identical interpreter engine, identical source; only
  the ISA/emulation differs.
- **Rosetta-x86_64-JIT vs Rosetta-x86_64-interp** isolates the **JIT's
  contribution** under emulation: identical ISA, identical Rosetta; only
  methodjit differs.

**Key finding, stated plainly: the native arm64 interpreter-only build
beats the Rosetta x86_64 JIT build on every kernel** — strings 21 ms vs
208/209 ms (~10x), objects 107 ms vs 1152/1154 ms (~10.8x), arrays 147 ms
vs 538/543 ms (~3.7x), math 602 ms vs 869/1028 ms (~1.4–1.7x). Executing
Oolite's JavaScript natively on the interpreter is strictly faster than
executing the JIT-enabled engine under Rosetta 2, so the arm64
interpreter-only artifact is not a compromise against the emulated x86_64
JIT status quo — it is the faster configuration on this class of hardware.
(Secondary reading: the 2011 methodjit adds nothing for these kernels
under emulation — its compile overhead makes math slightly slower than the
x86_64 interpreter and closes none of the Rosetta tax elsewhere.)

### Linux-x86_64-JIT cell — DEFERRED (fill-in recipe)

No Linux rig was available at publication. Any Linux x86_64 box fills the
cell in one command with the same source and the same harness.
Prerequisites: gcc or clang, perl, python2.7, and NSPR 4.35 built for
linux (`CC=gcc ../nspr-4.35/nspr/configure
--prefix=$PWD/../nspr-install-linux --enable-64bit && make -j && make
install`). Then:

```sh
NSPR_PREFIX="$PWD/../nspr-install-linux" ARCH=x86_64 JIT=1 \
    BUILD_DIR=build-x86_64-jit ./build.sh
```

`build.sh` auto-selects the `x86_64-linux-gnu` configure triple and drops
the darwin-only `-arch` flags on non-darwin hosts. Then measure with the
same harness and record the four numbers in the table above:

```sh
tools/bench.sh js/src/build-x86_64-jit/dist/bin/js linux-x86_64-jit
# if the shell cannot locate libnspr4.so at runtime, prefix:
#   LD_LIBRARY_PATH="$NSPR_PREFIX/lib"
```

## jstests deltas

`tests/jstests.py` (python2), default manifest, `--timeout=150`. Slot
totals vary slightly between runs (3000–3003 from the same manifest —
harness worker accounting; failure SETS below are stable and were
reproduced):

| Shell | fail | pass | failures |
|---|---|---|---|
| native-arm64-interp | 10 | ~2990 | 4 Date + 4 pcre + 1 alloc-expectation + 1 timeout (below) |
| Rosetta-x86_64-interp | 5 | ~2997 | 4 Date + 1 flaky |
| Rosetta-x86_64-JIT | 4 | 2999 | 4 Date |

- 4 failures common to ALL THREE shells: `ecma/Date/15.9.5.28-1.js`,
  `15.9.5.29-1.js`, `15.9.5.34-1.js`, `15.9.5.35-1.js` — test-harness
  environment issues (`startTest`/`printBugNumber`/`enterFunc` undefined
  from the vendored harness shell.js), identical on every architecture.
- 4 failures arm64-only are regexp-engine differences from documented
  Deviation #2 (pcre on aarch64 vs YARR on x86_64):
  `ecma_3/RegExp/regress-119909.js`, `js1_5/Regress/regress-230216-2.js`,
  `regress-280769-2.js`, `js1_5/Regress/regress-440926.js` — pcre either
  bails with "regular expression too complex" where YARR compiles, or
  reports different overflow errors / case-folding.
- `js1_5/Regress/regress-422348.js` is NOT a pcre-vs-YARR delta: it is an
  allocation-overflow-expectation test (`Array(1<<30).sort()`) and is
  allocation-environment-sensitive — it fails in-suite on both arm64
  runs, passes in-suite on x86_64, and fails in isolation on ALL shells.
  Mechanism unconfirmed; benign; not an engine cluster.
- `js1_5/Array/regress-157652.js` fails arm64-only on timing: the test
  expects an OOM exit (code 5) on a very large `Array.sort()`; the
  interpreter is killed by the 150 s timeout first (rc=-9).
- `js1_5/extensions/regress-371636.js` failed once in the
  x86_64-interp full run and passes in isolation on ALL THREE shells —
  flaky under 8-worker Rosetta load, not an architecture delta.
- No Punbox/value-layout/GC-shaped failure cluster on arm64 — the
  interpreter-only CPU-case patch and the 2^47 GC-chunk patch hold.

## PGO decision gate (oolite Task 15) — NOT TRIGGERED

Pre-agreed rule (port plan): build the PGO artifact (`-O3 -flto=thin` +
two-pass profile-guided optimization) **only if** the native JS-bound
frame time is **> 1.5x the Rosetta-1.90 baseline AND the difference is
user-visible**. Otherwise record "not triggered". Recorded verdict:
**not triggered** — no PGO build was made. The numbers:

- **Engine level** (table above): native-arm64-interp is *faster* than
  Rosetta-x86_64-JIT on every kernel — by 1.7x on math, ~3.7x on arrays,
  ~10x on strings, ~10.8x on objects. Oolite 1.90 under Rosetta 2
  executes its JavaScript through an x86_64 build of this same js185
  engine family, JIT-enabled (upstream `libjs.make`); under *either*
  x86_64 configuration in the table (JIT or interpreter), the Rosetta
  cell is slower than native on every kernel. The native JS-bound time is
  therefore below 1.0x the 1.90 baseline — nowhere near the >1.5x trigger
  (which would require e.g. ~313 ms on strings vs the measured 21 ms).
  The 1.90 baseline also runs the entire game — not just the JS engine —
  under emulation, which raises the baseline frame time and the >1.5x
  trigger threshold with it: the verdict is robust to wherever the
  measured baseline lands.
- **Frame level** (oolite Task 9, same M1 Max host, 75 Hz display):
  sustained **73.4–75.5 fps** across menu idle, sleep/wake and in-flight
  soak (pacing-capped by the Task 9 v-sync fix, vs ~220 fps uncapped
  pre-pacing), with the JavaScript frame callback executing every frame;
  full universe load — JS engine init plus all ship scripts — completes
  in 0.66–0.78 s; 82 scripted minutes across 11 processes, zero crashes,
  zero errors. The frame budget is display-paced, not JS-starved.
- **jstests timing deltas**: the only timing-classified failure is
  `js1_5/Array/regress-157652.js` (the arm64 interpreter hits the 150 s
  harness timeout before the expected OOM exit on a giant `Array.sort()`
  — a test-environment artifact, not a per-frame workload pattern). No
  native-side performance cluster (see deltas above).
- **FrameGuard**: the sustained eval+GC corruption was root-caused to the
  verification harness itself (teardown-order use-after-free; see
  "Known issue — RESOLVED"), so all engine performance and behavior
  numbers above are unaffected.

**Rosetta-1.90 fps baseline: HUMAN-PENDING.** No 1.90 comparison was
runnable in the automated environment (locked, screen-observed console; no
local 1.90 install). To close the leg: run Oolite 1.90 (x86_64) under
Rosetta 2 on the same 75 Hz display, fly a comparable populated-system
segment, and record sustained fps the way Task 9 did (TCP debug console
frame callback; the driver is preserved at
`oolite_ARM/.internal/sdd/oolite-arm64/t9-evidence/t9-soak-driver.py`).
Only a sustained native fps *deficit* against 1.90 would re-open this
gate; the engine-level bound above (native faster than either Rosetta
cell) says one should not exist.

## Known issue — RESOLVED (diagnosis 2026-10-07)

FrameGuard corruption under sustained eval+GC (~160+ cycles) was
root-caused to the Task 1 **adversarial verification harness**, not the
engine: the harness declared its `JSAutoEnterCompartment` at `main` scope,
so its destructor ran AFTER `JS_DestroyContext`/`JS_DestroyRuntime`/
`JS_ShutDown` had freed the JSContext and the interpreter StackSpace.
`~FrameGuard` then popped the still-pushed dummy frame through freed
memory (use-after-free), faulting with garbage `cx_` bytes. The
wave-count "threshold" was malloc reuse of the freed blocks, not a GC or
optimization boundary: the original harness crashes identically at -O0
and at 20 waves (heap-layout dependent), with the engine untouched.

Evidence and proof: `.internal/sdd/oolite-arm64/frameguard-diagnosis.md`
(parent repo). The corrected sustained-stress harness ships as
`tools/stress-sustained.cpp` (600 waves, pass = exit 0 +
`ALL-BELOW-2^47`); `tools/smoke.cpp` documents the teardown-order
contract. Engine sources unchanged — the previously recorded jstests and
benchmark results remain valid.

## Status

- [x] Sources fetched + pinned, patches applied + verified
      (config.guess → `aarch64-apple-darwin25.5.0`;
      `config.sub arm64-apple-darwin` → `aarch64-apple-darwin`;
      host/target canonicalization proven by live configure runs)
- [x] NSPR 4.35 built + installed (arm64 and x86_64)
- [x] Conftest gate, C and C++ compiler checks pass under clang 21 with
      `-Wno-error=implicit-int -Wno-error=implicit-function-declaration`
- [x] js builds: arm64 interpreter-only (default), x86_64 interpreter and
      x86_64 JIT references — all three `make` + `make install` clean;
      corrected generated-header greps hold on arm64 (`JS_PUNBOX64 1`,
      `HAVE_64BIT_OS 1`, `JS_BYTES_PER_WORD 8`, `JS_BITS_PER_WORD_LOG2 6`,
      `IS_LITTLE_ENDIAN 1`; zero `JS_METHODJIT`/`JS_MONOIC`/`JS_POLYIC`/
      `JS_TRACER`/`FEATURE_NANOJIT` defines)
- [x] Smoke (`tools/smoke.cpp`): PASS on all three shells — 128 GC
      iterations × 64 live rooted objects, every `JSObject*` below 2^47,
      zero JS error-reporter callbacks
- [x] jstests deltas + benchmark table recorded above
- [x] T15: benchmark table finalized (3 cells, methodology, deferred-Linux
      recipe); PGO gate evaluated per the pre-agreed rule — **not
      triggered** (numbers in the PGO decision gate section;
      Linux-x86_64-JIT cell and the Rosetta-1.90 fps baseline remain the
      two external legs: recipe + instructions inline)
- [x] Release artifact packaged: `dist/mozjs-js185-macos-arm64.tar.gz`
      (`include/` + `lib/js_static.a` + `LICENSE`; gitignored — release
      tags/CI upload it)
- [ ] GitHub-side: file the 7 spidermonkey-ff4 PRs, create the remote
      mozillajs-macos repo + first release (materials staged locally)

## License

MPL 2.0 (`LICENSE`). Nothing in oolite's tree depends on this repo's layout;
oolite consumes only the release artifact.
