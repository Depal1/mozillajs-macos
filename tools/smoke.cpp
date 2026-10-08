/*
 * smoke.cpp — js185 (spidermonkey-ff4) macos-arm64 interpreter-only smoke harness.
 *
 * Success criteria (micro-plan §5.11): exit 0; every GC iteration re-reads the
 * live `objs` array and every JSObject* is strictly below 2^47 (jsval.h
 * JSVAL_TAG_SHIFT == 47 — pointers at or above that line are engine-level
 * failures); zero JSErrorReporter callbacks.
 *
 * API notes for this 2011-era fork (all verified against jsapi.h):
 *   - JS_NewRuntime(uint32 maxbytes)          1-arg (jsapi.h:719-725)
 *   - JS_NewGlobalObject(cx, clasp)           2-arg, no principals (:2165)
 *   - JSAutoEnterCompartment + enter(cx,obj)  (NOT JSAutoCompartment) (:1029)
 *   - JSClass carries JSCLASS_GLOBAL_FLAGS    (:2004) + 8 mandatory stubs
 *   - JS_BeginRequest/JS_EndRequest           mandatory with --enable-threadsafe
 *   - JS_EvaluateScript                       7-arg (:2850)
 *   - JS_GetProperty 4-arg (:2330); JS_GetElement (:2478)
 *   - JS_AddNamedObjectRoot 3-arg (:1286); JS_GC(JSContext*) (:1748)
 *
 * Build (documented; adjust paths):
 *   clang++ -arch arm64 -std=gnu++98 \
 *       -I<js-src> -I<js-src-build-dir> -I<nspr>/include/nspr \
 *       smoke.cpp -o smoke \
 *       -L<js-src-build-dir> -ljs_static \
 *       -L<nspr>/lib -lnspr4 -lplc4 -lplds4 -framework CoreFoundation \
 *       -Wl,-rpath,<nspr>/lib
 * (Ad-hoc sign with `codesign -s - smoke` if run under a hardened profile.)
 */
#include <cstdio>
#include <cstring>
#include <stdint.h>

#include "jsapi.h"

/* 2^47: 64-bit jsval reserves the top 17 bits for the type tag
 * (JSVAL_TAG_SHIFT == 47 in jsval.h); every live JSObject* must live below
 * this line for debug-build assertions to hold. */
#define SMOKE_VA_LIMIT 0x0000800000000000ULL

static unsigned g_js_errors = 0;

static void
smokeErrorReporter(JSContext *cx, const char *message, JSErrorReport *rep)
{
    (void) cx;
    fprintf(stderr, "JS ERROR: %s (%s:%u)\n",
            message ? message : "<no message>",
            (rep && rep->filename) ? rep->filename : "<unknown>",
            rep ? (unsigned) rep->lineno : 0u);
    ++g_js_errors;
}

static JSClass smokeGlobalClass = {
    "smokeGlobal",
    JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* 64 rooted live objects, each with nested dense arrays — kept alive across
 * every GC so moving/collecting behavior is observable. */
static const char liveObjsSrc[] =
    "var objs = [];\n"
    "for (var i = 0; i < 64; ++i)\n"
    "    objs.push({ n: i, s: 'object-' + i, a: [i, i + 1, i + 2] });\n";

/* Allocation garbage each iteration so GC has real work to do. */
static const char garbageSrc[] =
    "for (var i = 0; i < 200; ++i)\n"
    "    ({ k: i, pad: new Array(64).join('x'), sub: { v: [i, i, i] } });\n";

int
main(int argc, char **argv)
{
    const uint32 ITERATIONS = 128;
    uint32 it, i;
    JSRuntime *rt;
    JSContext *cx;
    JSObject *global;
    JSObject *arr = NULL;
    jsuint len = 0;
    jsval rval;
    jsval v;

    (void) argc;
    (void) argv;

    rt = JS_NewRuntime((uint32) (256UL << 20));       /* jsapi.h:724, 1-arg */
    if (!rt) { fprintf(stderr, "FAIL: JS_NewRuntime\n"); return 1; }
    cx = JS_NewContext(rt, 8192);                     /* jsapi.h:863 */
    if (!cx) { fprintf(stderr, "FAIL: JS_NewContext\n"); return 1; }
    JS_SetErrorReporter(cx, smokeErrorReporter);      /* jsapi.h:3643 */

    /* The FIRST global must be created inside a fresh compartment:
     * plain JS_NewGlobalObject would run with cx->compartment == NULL and
     * crash (jsapi.cpp:3058 asserts/asserts-fail there). This fork's
     * JS_NewCompartmentAndGlobalObject is 3-arg with NULL principals
     * (jsapi.h:2168); JS_NewGlobalObject itself is 2-arg (:2165). */
    global = JS_NewCompartmentAndGlobalObject(cx, &smokeGlobalClass, NULL);
    if (!global) { fprintf(stderr, "FAIL: JS_NewGlobalObject\n"); return 1; }
    if (!JS_AddNamedObjectRoot(cx, &global, "global")) { /* jsapi.h:1286 */
        fprintf(stderr, "FAIL: JS_AddNamedObjectRoot(global)\n");
        return 1;
    }

    JS_BeginRequest(cx);                              /* jsapi.h:743 */
    {
        /* Teardown-order contract: this guard (and the request above) must
         * leave scope BEFORE JS_DestroyContext/JS_DestroyRuntime — its dtor
         * pops a pushed dummy frame, so it must not outlive the runtime.
         * The block scope here ends before the teardown calls below. A
         * guard that lives to the end of main crashes in
         * js::FrameGuard::~FrameGuard (use-after-free; see
         * tools/stress-sustained.cpp for the documented pattern). */
        JSAutoEnterCompartment ac;                    /* jsapi.h:1029 */
        if (!ac.enter(cx, global)) {
            fprintf(stderr, "FAIL: JSAutoEnterCompartment::enter\n");
            return 1;
        }
        if (!JS_InitStandardClasses(cx, global)) {
            fprintf(stderr, "FAIL: JS_InitStandardClasses\n");
            return 1;
        }

        if (!JS_EvaluateScript(cx, global, liveObjsSrc,
                               (uintN) strlen(liveObjsSrc),
                               "smoke.js", 1, &rval)) { /* jsapi.h:2850 */
            fprintf(stderr, "FAIL: JS_EvaluateScript(liveObjs)\n");
            return 1;
        }

        if (!JS_GetProperty(cx, global, "objs", &v) ||  /* jsapi.h:2330 */
            !JSVAL_IS_OBJECT(v)) {
            fprintf(stderr, "FAIL: global.objs missing or not an object\n");
            return 1;
        }
        arr = JSVAL_TO_OBJECT(v);                     /* jsapi.h:208 */
        if (!JS_GetArrayLength(cx, arr, &len) || len != 64) { /* :2452 */
            fprintf(stderr, "FAIL: objs length (got %u, want 64)\n",
                    (unsigned) len);
            return 1;
        }
        /* Root the ARRAY object once; rooting it keeps its dense elements
         * traced/updated across GC, so elements are re-read after each GC. */
        if (!JS_AddNamedObjectRoot(cx, &arr, "objs")) {
            fprintf(stderr, "FAIL: JS_AddNamedObjectRoot(objs)\n");
            return 1;
        }

        for (it = 0; it < ITERATIONS; ++it) {
            if (!JS_EvaluateScript(cx, global, garbageSrc,
                                   (uintN) strlen(garbageSrc),
                                   "smoke.js", 2, &rval)) {
                fprintf(stderr, "FAIL: JS_EvaluateScript(garbage) iter %u\n", it);
                return 1;
            }
            JS_GC(cx);                                /* jsapi.h:1748 */

            for (i = 0; i < len; ++i) {
                jsval elem;
                JSObject *o;
                if (!JS_GetElement(cx, arr, (jsint) i, &elem)) { /* :2478 */
                    fprintf(stderr, "FAIL: JS_GetElement(%u) iter %u\n", i, it);
                    return 1;
                }
                if (!JSVAL_IS_OBJECT(elem)) {
                    fprintf(stderr, "FAIL: objs[%u] not an object (iter %u)\n",
                            i, it);
                    return 1;
                }
                o = JSVAL_TO_OBJECT(elem);
                if ((uintptr_t) o >= SMOKE_VA_LIMIT) {
                    fprintf(stderr,
                            "FAIL: objs[%u]=%p at/above 2^47 (iter %u) — "
                            "engine-level failure\n", i, (void *) o, it);
                    return 1;
                }
            }
            printf("iter %3u: %u live objects re-read after JS_GC, all < 2^47\n",
                   it, (unsigned) len);
        }
    }
    JS_EndRequest(cx);                                /* jsapi.h:746 */

    JS_RemoveObjectRoot(cx, &arr);                    /* jsapi.h:1298 */
    JS_RemoveObjectRoot(cx, &global);
    JS_DestroyContext(cx);                            /* jsapi.h:866 */
    JS_DestroyRuntime(rt);                            /* jsapi.h:731 */
    JS_ShutDown();                                    /* jsapi.h:734 */

    if (g_js_errors != 0) {
        fprintf(stderr, "FAIL: %u JS error(s) reported\n", g_js_errors);
        return 1;
    }
    printf("SMOKE PASS: %u iterations x %u live objects, all pointers below "
           "2^47, 0 JS errors\n", ITERATIONS, (unsigned) len);
    return 0;
}
