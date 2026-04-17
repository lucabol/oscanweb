/*
 * js_bridge.c — QuickJS-ng bridge for OscaWeb
 *
 * Provides JS engine lifecycle (init/eval/destroy) and console.log.
 * Compiled via: oscan browser.osc --extra-c js_bridge.c --extra-c libs/quickjs/quickjs.c
 */

#include "libs/quickjs/quickjs.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── osc_str helpers ──────────────────────────────────────── */

typedef struct { const char *data; int32_t len; } osc_str;

static osc_str osc_str_from_cstr(const char *s) {
    osc_str r;
    r.data = s;
    r.len = s ? (int32_t)strlen(s) : 0;
    return r;
}

/* Allocate a null-terminated copy of an osc_str */
static char *osc_str_to_cstr_alloc(osc_str s) {
    char *buf = (char *)malloc(s.len + 1);
    if (!buf) return NULL;
    memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';
    return buf;
}

/* ── Forward declarations for Oscan runtime I/O ──────────── */

extern void osc_print(osc_str s);
extern void osc_println(osc_str s);

/* ── console.log / warn / error ──────────────────────────── */

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            osc_print(osc_str_from_cstr(str));
            JS_FreeCString(ctx, str);
        }
        if (i < argc - 1) osc_print(osc_str_from_cstr(" "));
    }
    osc_println(osc_str_from_cstr(""));
    return JS_UNDEFINED;
}

static void js_register_console(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
        JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "warn",
        JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error",
        JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

/* ── Engine lifecycle ────────────────────────────────────── */

uintptr_t js_engine_init(void) {
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) return 0;
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }
    js_register_console(ctx);
    return (uintptr_t)ctx;
}

osc_str js_engine_eval(uintptr_t ctx_handle, osc_str code) {
    JSContext *ctx = (JSContext *)ctx_handle;
    if (!ctx) return osc_str_from_cstr("");

    char *buf = osc_str_to_cstr_alloc(code);
    if (!buf) return osc_str_from_cstr("out of memory");

    JSValue val = JS_Eval(ctx, buf, code.len, "<script>", JS_EVAL_TYPE_GLOBAL);
    free(buf);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        osc_str result = osc_str_from_cstr(msg ? msg : "JS Error");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, val);
        return result;
    }

    if (JS_IsUndefined(val) || JS_IsNull(val)) {
        JS_FreeValue(ctx, val);
        return osc_str_from_cstr("");
    }

    const char *str = JS_ToCString(ctx, val);
    osc_str result = osc_str_from_cstr(str ? str : "");
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, val);
    return result;
}

void js_engine_destroy(uintptr_t ctx_handle) {
    JSContext *ctx = (JSContext *)ctx_handle;
    if (!ctx) return;
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
