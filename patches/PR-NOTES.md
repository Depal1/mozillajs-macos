# spidermonkey-ff4 PR series: aarch64/darwin (Apple Silicon) support

All seven patches branch from the pinned commit
`c463e95ea5d1d780301e7f3792783771381125f0` (2017-10-08 "GCC 7 compile fix"),
one commit per patch. Toolchain used for verification: Xcode 26.6 (17F113),
Apple clang 21.0.0, macOS 25.5.0, arm64.

These patches exist so Oolite can build its pinned SpiderMonkey (js185 /
mozilla-2.0) on arm64 Macs. oolite#721 ("Apple silicon release") notes that a
release for Apple Silicon "requires updating some major game dependencies";
this series is that update for the JavaScript engine. It is a build port of
the engine already pinned, not an engine upgrade: the JS API surface is
untouched, so the several hundred OXPs written against js185 keep working
(this distinction matters; see oolite#360, where a 2020 proposal to swap
engines was rejected partly for that reason).

Relation to oolite#721's stated restrictions ("as similar as possible to
existing builds... SDL3... meson... espeak-ng... use OS specific code as
sparingly as possible"): these patches add aarch64 branches only where the
build system needs to know the CPU exists, and otherwise follow the
established powerpc/sparc fallback precedent, so the engine stays a single
source tree for all targets. None of them touch the oolite-side platform
code; how the oolite port itself satisfies the SDL3/meson/espeak-ng
restrictions is covered by the companion oolite PR series and its design
issue.

AI-assistance disclosure: these patches were developed with the assistance
of an AI coding agent (GLM-5.3-Flash, orchestrated in ZCode). Every change
was human-reviewed and validated by the builds and tests described in each
PR below.

## PR A: "Refresh config.guess/config.sub for darwin-arm64"

Patch: `patch-a-config-guess-sub-darwin-arm64.patch`

The 2009-era config scripts cannot name an Apple Silicon host. On this
hardware `uname -p` returns `arm` and `uname -m` returns `arm64`; the old
config.guess uses `uname -p` on Darwin, so it reports
`arm-apple-darwin25.5.0`, and config.sub (2009-12-04) rejects both
`arm64-apple-darwin` and `aarch64-apple-darwin` outright (both shown by live
runs). Downstream, `TARGET_CPU=arm64` fell into configure's 32-bit `arm*`
cases, which injected `-march=armv7-a -marm -mfloat-abi=softfp`, and arm64
clang rejects those flags.

The patch switches the Darwin branch of config.guess to `uname -m` and
canonicalizes `arm64` to `aarch64`, following the existing `amd64` to
`x86_64` alias precedent at config.sub :427-432. `aarch64` is added to the
two CPU validation lists; `arm64` deliberately is not, because a list entry
pre-empts the alias (first matching pattern wins) and would leave
`arm64-apple-darwin` uncanonicalized.

Verified live after the patch: config.guess reports
`aarch64-apple-darwin25.5.0`; config.sub accepts and canonicalizes
`arm64-apple-darwin` to `aarch64-apple-darwin`; `x86_64-apple-darwin` and
plain `arm` behave exactly as before.

This is the first of the three blockers oolite#360 listed for an ARM Mac
build ("libjs needs to be updated to take advantage of the ARMv8/ARM64
architecture"): nothing downstream can even run configure without it.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR B: "Add aarch64/arm64 CPU cases to configure (interpreter-only)"

Patch: `patch-b-configure-aarch64-cpu-cases.patch`

js185's configure knows four CPUs: i386, x86_64, 32-bit arm, and sparc. On
aarch64 no CPU case matches, so no value-representation macro is defined and
the build stops in jsval.h. There is also no ARM64 JIT backend in this tree
(nanojit has i386/x86_64/ARMv7; MethodJIT likewise), so aarch64 must be an
interpreter-only target.

The patch adds an `aarch64*-*|arm64*-*` case before the existing `arm*-*`
case (order matters: `arm*-*` would otherwise swallow the triplet after the
host fix in PR A). The case clears ENABLE_TRACEJIT, ENABLE_METHODJIT,
ENABLE_MONOIC and ENABLE_POLYIC, so FEATURE_NANOJIT/JS_TRACER are never
defined, and defines `JS_PUNBOX64`, the load-bearing 64-bit value-layout
macro. A second, empty case in the AVMPLUS block keeps the 32-bit
`AVMPLUS_ARM` define off. Both the generated `configure` and `configure.in`
are patched in lockstep; the 32-bit `JS_CPU_ARM` cases are untouched.
`--disable-methodjit --disable-tracejit` remain in the consumer's configure
line as belt-and-braces.

Verified on arm64 darwin: js-confdefs.h carries `JS_PUNBOX64 1`,
`HAVE_64BIT_OS 1`, `JS_BYTES_PER_WORD 8`, `JS_BITS_PER_WORD_LOG2 6`, and
zero JIT/IC defines; the js shell and library build clean; the consumer
passes a 600-wave GC-stress soak (48,384 sampled object pointers, all below
2^47) and the js185 test suite with no arm64-specific failure cluster.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR C: "Makefile.in: route aarch64/arm64 to the pcre regex path"

Patch: `patch-c-makefile-pcre-path.patch`

The YARR regex assembler has no arm64 backend in this tree. The Makefile's
CPU filter (`js/src/Makefile.in:378`) sends every CPU except powerpc and
sparc into the Nitro branch, which compiles RegexJIT.cpp and the
MacroAssembler sources; `MacroAssembler.h:57-58` then raises
`#error "The MacroAssembler is not supported on this platform."` because
`assembler/wtf/Platform.h` has no aarch64 case.

The patch extends the filter to `powerpc sparc aarch64 arm64`, putting
arm64 on the same pcre path those targets already use. On that path
`ENABLE_YARR_JIT` resolves to 0 (`assembler/wtf/Platform.h` computes it from
the CPU list) and `jsregexpinlines.h` selects the bundled pcre interpreter.
The forced `-DUSE_SYSTEM_MALLOC/ENABLE_ASSEMBLER/ENABLE_JIT` CXXFLAGS at
:917 stay defined-but-inert, exactly as on ppc/sparc, since no pcre-path TU
compiles assembler code.

Verified: the library builds clean with the filter extended, and the js185
test suite was run on the result. Four regexp tests differ from the YARR
builds (one bails with "regular expression too complex" on a 500-nested-paren
case, one reports a different quantifier-overflow message, one differs on
Turkish case-folding, one accepts a quantifier overflow silently). These are
regex-engine semantic differences, not crashes; the consumer documents them
as a known deviation.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR D: "jsgcchunk.cpp: keep darwin GC chunks below 2^47"

Patch: `patch-d-jsgcchunk-darwin-low47-va.patch`

With `JS_PUNBOX64`, jsval tagging uses `JSVAL_TAG_SHIFT 47`
(`jsval.h:69`), which requires GC chunk addresses below 2^47 for the
debug-build assertions to be sound. On darwin the allocator arm is
`vm_allocate(..., VM_FLAGS_ANYWHERE)` (`jsgcchunk.cpp:269-273`), and
`VM_FLAGS_ANYWHERE` ignores any address hint, so placement near the top of
the address space cannot be ruled out on current or future kernels.

The patch adds a hint-retry before the ANYWHERE fallback: try
`vm_allocate` with `flags=0` at descending fixed addresses starting under
2^47, and only fall back to the original behavior when the region below the
line is exhausted. The fixed-address path and post-conditions are otherwise
byte-identical to the original, and `vm_deallocate` still receives exactly
what `vm_allocate` returned, so unmap stays consistent.

Verified on arm64 darwin (Apple clang 21): three GC-chunk-sized allocations
landed at `0x7ffffdf00000` and descended, all below 2^47, and the consumer's
GC-stress harness sampled 48,384 live object pointers across a 600-wave soak
without a single address at or above the line.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR E: "Replace removed finite() with C99 isfinite()"

Patch: `patch-e-jsnum-ctypes-finite-isfinite.patch`

Modern macOS SDK headers (Xcode 26 / darwin 25) no longer declare the
deprecated POSIX `finite()`. `jsnum.h:104` and `ctypes/CTypes.cpp:467` fail
with "use of undeclared identifier 'finite'" (observed live).

`isfinite()` is the C99 macro for the same predicate and is already the
idiom in these files (`isnan()` appears unqualified ten lines above, and
`isinf()` below). The patch replaces all three bare uses: the
`JSDOUBLE_IS_FINITE` macro (all non-WIN32), the SOLARIS branch of
`JSDOUBLE_IS_INFINITE` (same underlying symbol, latent), and jsctypes'
`FloatIsFinite`.

Verified by building the library clean on the Xcode 26 toolchain.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR F: "MethodJIT.cpp: drop meaningless volatile from file-scope asm"

Patch: `patch-f-methodjit-file-scope-asm.patch`

The methodjit trampolines are declared with file-scope `asm volatile (...)`.
Volatile is meaningless on file-scope asm, and clang 16+ rejects it as a
hard error: "meaningless 'volatile' on asm outside function" (observed under
Apple clang 21, four hits on the x86_64 path).

The patch drops `volatile` at all ten sites (x86_64 and i386 arms). Plain
`asm (...)` is semantically identical here; the statements only emit assembly
into the current section.

Verified with both x86_64 reference builds (JIT-enabled and
interpreter-only) compiling clean and running under Rosetta on Apple
Silicon.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.

## PR G: "methodjit/Logging.h: keep the JaegerSpew stub without methodjit"

Patch: `patch-g-methodjit-logging-stub-for-interp-assembler.patch`

`assembler/X86Assembler.h` calls `js::JaegerSpew(js::JSpew_Insns, ...)`
unconditionally in its per-insn helpers (156 sites). Upstream never noticed
because every shipping x86 build had methodjit enabled: the outer
`(defined JS_METHODJIT || defined JS_TRACER)` guard made Logging.h contribute
nothing, not even the channel enum, once both backends are off. With
`--disable-methodjit` on x86_64 (the interpreter-only reference build), the
YARR regex assembler still includes the headers and hits "no member named
'JaegerSpew' in namespace 'js'" (observed live).

The patch makes the include guard, the `JaegerSpewChannel` enum and the
no-op `JaegerSpew` stub unconditional, while `prmjtime.h` and the real spew
machinery (`JMCheckLogging`, `Profiler`) stay behind the methodjit/tracer
gate. Upstream configurations compile to identical content; checked
case-by-case (methodjit-on release gets the stub, methodjit-on DEBUG gets
full spew, both as before).

Verified with the interpreter-only x86_64 reference build, which now
compiles and runs under Rosetta.

AI-assistance disclosure: developed with AI assistance (GLM-5.3-Flash); human-reviewed and verified by the builds/tests described.

Part of the arm64 enablement oolite#721 names as a prerequisite for an Apple Silicon release ("requires updating some major game dependencies"). Restriction check against #721: this patch adds only the build-system branches aarch64 requires (same fallback precedent as powerpc/sparc), touches no oolite-side platform code, and the consumer port itself satisfies the SDL3/meson/espeak-ng/minimal-OS-specific-code restrictions, covered by the companion oolite series.
