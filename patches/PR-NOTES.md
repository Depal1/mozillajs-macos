# spidermonkey-ff4 PR skeletons (file these 4 PRs; one per patch group)

All four patches branch from the pinned commit
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
- Live output on Apple Silicon: `config.guess` → `aarch64-apple-darwin25.5.0`;
  `config.sub arm64-apple-darwin` → `aarch64-apple-darwin`.

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
