# spidermonkey-ff4 PR skeletons (file these 5 PRs; one per patch group)

All five patches branch from the pinned commit
`c463e95ea5d1d780301e7f3792783771381125f0` (2017-10-08 "GCC 7 compile fix"),
one commit per patch. Reference the pin and the toolchain used for
verification: Xcode 26.6 (17F113), Apple clang 21.0.0, macOS 25.5.0 arm64.

## PR A — "Refresh config.guess/config.sub for darwin-arm64"

Patch: `patch-a-config-guess-sub-darwin-arm64.patch`

- 2009-era `config.guess` uses `uname -p` on Darwin, which returns `arm` on
  Apple Silicon (yields `arm-apple-darwin`); `config.sub` (2009-12-04) rejects
  both `arm64-` and `aarch64-apple-darwin` (shown by live dry-run).
- Switch Darwin to `uname -m`; canonicalize `arm64` → `aarch64` (mirrors the
  existing `amd64` → `x86_64` alias precedent at config.sub :427-432).
- x86_64/Intel behavior unchanged: the i386 → x86_64 LP64 probe is preserved;
  `config.sub x86_64-apple-darwin` echoes unchanged (live-verified).
- Live outputs on Apple Silicon: `config.guess` →
  `aarch64-apple-darwin25.5.0`; `config.sub arm64-apple-darwin` →
  `aarch64-apple-darwin`; `config.sub arm64` → `aarch64-unknown-none`;
  `config.sub amd64-apple-darwin` → `x86_64-apple-darwin` (precedent intact).
- `aarch64` is added to the two CPU validation lists; `arm64` deliberately is
  NOT — `amd64` lives in neither list, and a list entry pre-empts the alias
  (first matching pattern wins), leaving `arm64-apple-darwin` uncanonicalized.
  Observed live before this fix: `TARGET_CPU=arm64` matched configure's
  32-bit `arm*` cases and injected `-march=armv7-a -marm -mfloat-abi=softfp`,
  which arm64 clang rejects.

## PR B — "Add aarch64/arm64 CPU cases to configure (interpreter-only)"

Patch: `patch-b-configure-aarch64-cpu-cases.patch`

- CPU case 1 (JIT/Nanojit selection block, `configure` ~:7967 /
  `configure.in` ~:2929, inserted before `arm*-*`): empty
  ENABLE_TRACEJIT/ENABLE_METHODJIT/ENABLE_MONOIC/ENABLE_POLYIC (so
  FEATURE_NANOJIT/JS_TRACER are never defined) + `JS_PUNBOX64` — the
  load-bearing 64-bit value-layout define.
- AVMPLUS block (~`configure`:8145 / `configure.in`:3014): empty
  `aarch64*-*|arm64*-*) ;;` case before `arm*-*` so the 32-bit AVMPLUS_ARM
  define is not applied.
- Both the generated script and the .in source patched in lockstep; the
  32-bit JS_CPU_ARM cases are untouched. Note: the `configure` hunk
  alignment repeats the JS_PUNBOX64 define block (diff artifact — the
  aarch64 case is its own case after the x86_64 one; `sh -n` passes).
- Intent: interpreter-only (Oolite ships JIT off). With the case body, no
  JIT/IC vars are set, making `--disable-methodjit --disable-tracejit`
  defaults; the flags stay as belt-and-braces.

## PR C — "Makefile.in: route aarch64/arm64 to the pcre regex path"

Patch: `patch-c-makefile-pcre-path.patch`

- Extends the `powerpc sparc` filter at `js/src/Makefile.in`:378 with
  `aarch64 arm64`; without it the Nitro else-branch (:391-439) compiles
  `RegexJIT.cpp` / `MacroAssembler*.cpp` and hits the
  `MacroAssembler.h:57-58` `#error` (no aarch64 case in
  `assembler/wtf/Platform.h`).
- pcre is the documented default: `ENABLE_YARR_JIT` is defined nowhere
  (`jsregexpinlines.h:48-52`); same as the powerpc/sparc precedent.
- The :917 forced `-DUSE_SYSTEM_MALLOC=1 -DENABLE_ASSEMBLER=1
  -DENABLE_JIT=1` CXXFLAGS remain defined-but-inert on the pcre path (the
  assembler sources are never compiled), exactly as on ppc/sparc.

## PR D — "jsgcchunk.cpp: keep darwin GC chunks below 2^47"

Patch: `patch-d-jsgcchunk-darwin-low47-va.patch`

- jsval tag shift 47 (`jsval.h:69`, `JSVAL_TAG_SHIFT`) ⇒ GC chunks must live
  below 2^47 for debug-build assertions to hold.
- `VM_FLAGS_ANYWHERE` can return addresses above that line on arm64 darwin
  (the kernel places shared regions near the top of the address space).
- Fix: hint-retry with `flags=0` at descending addresses from 2^47; fall
  back to `VM_FLAGS_ANYWHERE` only when the region below the line is
  exhausted. The fixed-address (`addr != NULL`) path and the
  post-conditions are byte-identical to the original.
- Compile+run evidence on arm64 darwin (Apple clang 21, macOS 25.5.0):
  three GC-chunk-sized allocations landed at `0x7ffffdf00000` and
  descended — all below 2^47.

## PR E — "Replace removed finite() with C99 isfinite()"

Patch: `patch-e-jsnum-ctypes-finite-isfinite.patch`

- The deprecated POSIX `finite()` is no longer declared by modern macOS SDK
  headers (Xcode 26 / darwin 25): `jsnum.h:104` and `ctypes/CTypes.cpp:467`
  fail with "use of undeclared identifier 'finite'" (observed live).
- `isfinite()` is the C99 macro for the same predicate and is already the
  established idiom in this file (`isnan()` unqualified ten lines above,
  `isinf()` below).
- Fixes all three bare uses: `JSDOUBLE_IS_FINITE` (all non-WIN32), the
  SOLARIS branch of `JSDOUBLE_IS_INFINITE` (same symbol, latent), and
  jsctypes' `FloatIsFinite`.

## PR F — "MethodJIT.cpp: drop meaningless volatile from file-scope asm"

Patch: `patch-f-methodjit-file-scope-asm.patch`

- The methodjit trampolines are declared with file-scope
  `asm volatile (...)`; volatile is meaningless there and clang 16+
  rejects it as a hard error ("meaningless 'volatile' on asm outside
  function" — observed under Apple clang 21, 4 hits on the x86_64 path).
- Plain `asm (...)` is semantically identical (the statements only emit
  assembly into the current section); all ten sites (x86_64 + i386
  arms) are converted.

## PR G — "methodjit/Logging.h: keep the JaegerSpew stub without methodjit"

Patch: `patch-g-methodjit-logging-stub-for-interp-assembler.patch`

- `assembler/X86Assembler.h` calls `js::JaegerSpew(js::JSpew_Insns, …)`
  unconditionally in its per-insn helpers (156 sites). That compiled only
  because every upstream x86 build had methodjit enabled: the outer
  `(defined JS_METHODJIT || defined JS_TRACER)` guard made Logging.h
  contribute nothing — not even the channel enum — once both backends
  are disabled.
- With `--disable-methodjit` on x86_64 (interpreter-only reference), the
  YARR regex JIT still compiles the assembler headers →
  "no member named 'JaegerSpew' in namespace 'js'" (observed live).
- Restructure: include guard + `JaegerSpewChannel` enum + the no-op
  `JaegerSpew` stub are now unconditional; `prmjtime.h` and the real
  spew machinery (`JMCheckLogging`, `Profiler`) stay behind the
  methodjit/tracer gate, so upstream configurations compile to
  identical content (verified case-by-case: methodjit-on release →
  stub, methodjit-on DEBUG → full spew, both as before).
