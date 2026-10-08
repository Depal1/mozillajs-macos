/* Sustained eval+GC stress (regression guard for the resolved "FrameGuard
   known issue"): deep object graphs + string churn + arrays, no fixed live
   set; 64-slot root ring over wave-built chains, 5000 allocations per wave,
   JS_GC per wave, deep chain walks sampled, 600 waves (3x the historically
   reported failing threshold).

   Teardown-order contract (the original adversarial copy of this harness
   violated it): the JSAutoEnterCompartment guard is scoped to end BEFORE
   JS_EndRequest/JS_ClearContextThread/JS_DestroyContext/JS_DestroyRuntime/
   JS_ShutDown. If the guard outlives the runtime, its destructor pops the
   still-pushed dummy frame through freed JSContext/StackSpace memory
   (use-after-free) and crashes in js::FrameGuard::~FrameGuard with
   garbage 'cx_' bytes - an embedding bug, not an engine defect.

   Build (paths as in tools/smoke.cpp), then run; exit 0 + ALL-BELOW-2^47
   is the pass criterion:
     clang++ -arch arm64 -std=gnu++98 -I<dist>/include/js -I<nspr>/include/nspr \
         tools/stress-sustained.cpp -o stress-sustained <dist>/lib/js_static.a \
         -L<nspr>/lib -lnspr4 -lplc4 -lplds4 -framework CoreFoundation */
#include <jsapi.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#define LIMIT47 ((uint64_t)1 << 47)
#define RING 64
#define WAVES 600
#define CHAIN 1000   /* 5 allocations per node => 5000 allocs/wave between GCs */

static JSClass globalClass = { "Global", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
    JSCLASS_NO_OPTIONAL_MEMBERS };

static JSObject *ring[RING];
static long gc_count = 0;
static uint64_t min_addr = ~0ull, max_addr = 0;
static long sampled = 0;

static void sample(JSObject *o) {
    if (!o) { fprintf(stderr, "FATAL: null sampled object\n"); exit(9); }
    uint64_t a = (uint64_t)(uintptr_t)o;
    if (a >= LIMIT47) {
        fprintf(stderr, "FATAL: object pointer 0x%llx >= 2^47\n", (unsigned long long)a);
        exit(1);
    }
    if (a < min_addr) min_addr = a;
    if (a > max_addr) max_addr = a;
    sampled++;
}

static void walk_and_sample(JSContext *cx, JSObject *head, int depth) {
    JSObject *o = head;
    for (int i = 0; i < depth && o; i++) {
        sample(o);
        jsval nv;
        if (JS_GetProperty(cx, o, "next", &nv) && JSVAL_IS_OBJECT(nv))
            o = JSVAL_TO_OBJECT(nv);
        else break;
    }
}

int main(void) {
    JSRuntime *rt = JS_NewRuntime(256u << 20);
    if (!rt) return 2;
    JSContext *cx = JS_NewContext(rt, 32768);
    if (!cx) return 3;
    JS_SetContextThread(cx);
    JS_SetContextThread(cx);
    JS_BeginRequest(cx);
    JSObject *glob = JS_NewCompartmentAndGlobalObject(cx, &globalClass, NULL);
    if (!glob) return 4;
    if (!JS_InitStandardClasses(cx, glob)) return 6;

    /* FIXED: the compartment guard lives in an inner scope that ends BEFORE
       any runtime teardown. (The original kept `ac` alive to the end of main,
       so its destructor popped a dummy frame from the freed StackSpace after
       JS_DestroyContext/JS_DestroyRuntime/JS_ShutDown - a use-after-free that
       manifested as ~FrameGuard SEGVs after enough heap churn.) */
    {
    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, glob)) return 5;

    for (int i = 0; i < RING; i++) JS_AddNamedObjectRoot(cx, &ring[i], "ring");

    char script[512];
    for (int w = 0; w < WAVES; w++) {
        /* 1000 nodes x {object, string, array, number} = ~5000 allocations since last GC */
        snprintf(script, sizeof script,
            "var h=null; for (var i=0;i<%d;i++) h={n:i,w:%d,next:h,"
            "s:'wave'+%d+'_'+i+'_'+'paddingpaddingpadding',"
            "a:[i,i*2,%d,i+0.5,'x'+i]}; h;", CHAIN, w, w, w);
        jsval rval;
        if (!JS_EvaluateScript(cx, glob, script, strlen(script), "<stress>", 1, &rval)
            || !JSVAL_IS_OBJECT(rval)) {
            fprintf(stderr, "FATAL: script failed at wave %d\n", w);
            if (JS_IsExceptionPending(cx)) {
                jsval ex; JS_GetPendingException(cx, &ex);
                JSString *m = JS_ValueToString(cx, ex);
                char b[512]; size_t l=0; const jschar *c = JS_GetStringCharsAndLength(cx, m, &l);
                for (size_t k=0;k<l&&k<511;k++) b[k]=(char)c[k]; b[l<511?l:511]=0;
                fprintf(stderr, "JS exception: %s\n", b);
            }
            return 7;
        }
        JSObject *head = JSVAL_TO_OBJECT(rval);
        ring[w % RING] = head;             /* rotate root ring (old head may die) */
        JS_GC(cx); gc_count++;
        for (int i = 0; i < RING; i++) {
            if (!ring[i]) continue;
            sample(ring[i]);
            /* liveness probe: a rooted object must survive every GC */
            jsval nv;
            if (!JS_GetProperty(cx, ring[i], "n", &nv) || !JSVAL_IS_NUMBER(nv)) {
                fprintf(stderr, "FATAL: rooted ring[%d] did not survive GC (wave %d)\n", i, w);
                return 8;
            }
        }
        if (w % 10 == 0) walk_and_sample(cx, head, 200); /* deep walk sampling */
        if (w % 40 == 0)
            fprintf(stderr, "wave %4d: gcs=%ld sampled=%ld min=0x%llx max=0x%llx\n",
                    w, gc_count, sampled, (unsigned long long)min_addr, (unsigned long long)max_addr);
    }
    for (int i = 0; i < RING; i++) JS_RemoveObjectRoot(cx, &ring[i]);
    printf("FIXED-STRESS RESULT: waves=%d allocs>=%ld gcs=%ld sampled=%ld "
           "min=0x%llx max=0x%llx limit=0x%llx => %s\n",
           WAVES, (long)WAVES*CHAIN*5, gc_count, sampled,
           (unsigned long long)min_addr, (unsigned long long)max_addr,
           (unsigned long long)LIMIT47,
           max_addr < LIMIT47 ? "ALL-BELOW-2^47" : "VIOLATION");
    } /* end compartment-guard scope: dummy frame popped BEFORE teardown */
    JS_EndRequest(cx);
    JS_ClearContextThread(cx);
    JS_ClearContextThread(cx);
    JS_DestroyContext(cx);
    JS_DestroyRuntime(rt);
    JS_ShutDown();
    return max_addr < LIMIT47 ? 0 : 1;
}
