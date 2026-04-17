/*
 * js_bridge.c — QuickJS-ng bridge for OscaWeb
 *
 * Provides JS engine lifecycle (init/eval/destroy), console.log,
 * and DOM bindings (document.getElementById, element.textContent, etc.)
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

/* ── Oscan runtime types needed for DOM access ───────────── */

typedef struct {
    void    *data;
    int32_t  len;
    int32_t  capacity;
    int32_t  elem_size;
} osc_array;

typedef struct osc_map osc_map;

typedef struct {
    osc_str   tag;
    osc_str   text;
    osc_map  *attrs;
    int32_t   first_child;
    int32_t   next_sibling;
    uint8_t   is_text;
} HtmlNode;

/* ── Forward declarations for Oscan runtime functions ────── */

extern void osc_print(osc_str s);
extern void osc_println(osc_str s);
extern int32_t osc_array_len(osc_array *arr);
extern void *osc_array_get(osc_array *arr, int32_t index);
extern void osc_array_push(void *arena, osc_array *arr, void *value);
extern osc_str osc_str_concat(void *arena, osc_str a, osc_str b);
extern int32_t osc_str_len(osc_str s);
extern uint8_t osc_str_eq(osc_str a, osc_str b);
extern uint8_t osc_map_has(osc_map *m, osc_str key);
extern osc_str osc_map_get(osc_map *m, osc_str key);
extern void osc_map_set(void *arena, osc_map *m, osc_str key, osc_str value);
extern osc_map *osc_map_new(void *arena);

/* Global arena — created by Oscan-generated main */
extern void *osc_global_arena;

/* ── DOM state ───────────────────────────────────────────── */

static osc_array *g_dom_nodes = NULL;
static int g_dom_dirty = 0;

/* ── Helper: get HtmlNode by index ───────────────────────── */

static HtmlNode *dom_get_node(int32_t idx) {
    if (!g_dom_nodes || idx < 0 || idx >= osc_array_len(g_dom_nodes))
        return NULL;
    return (HtmlNode *)osc_array_get(g_dom_nodes, idx);
}

/* Recursive text content collection */
static void dom_collect_text(int32_t idx, char *buf, int *pos, int max) {
    HtmlNode *node = dom_get_node(idx);
    if (!node) return;
    if (node->is_text) {
        int to_copy = node->text.len;
        if (*pos + to_copy >= max) to_copy = max - *pos - 1;
        if (to_copy > 0) {
            memcpy(buf + *pos, node->text.data, to_copy);
            *pos += to_copy;
        }
        return;
    }
    int32_t child = node->first_child;
    while (child != -1) {
        if (*pos > 0 && *pos < max - 1) {
            buf[*pos] = ' ';
            (*pos)++;
        }
        dom_collect_text(child, buf, pos, max);
        HtmlNode *cn = dom_get_node(child);
        child = cn ? cn->next_sibling : -1;
    }
}

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

/* ── Element class ───────────────────────────────────────── */

static JSClassID js_element_class_id;

static JSValue js_element_wrap(JSContext *ctx, int32_t node_idx) {
    if (node_idx < 0) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, js_element_class_id);
    if (JS_IsException(obj)) return obj;
    int32_t *data = (int32_t *)malloc(sizeof(int32_t));
    *data = node_idx;
    JS_SetOpaque(obj, data);
    return obj;
}

static void js_element_finalizer(JSRuntime *rt, JSValue val) {
    int32_t *data = (int32_t *)JS_GetOpaque(val, js_element_class_id);
    if (data) free(data);
}

static JSClassDef js_element_class = {
    "Element",
    .finalizer = js_element_finalizer,
};

/* element.tagName getter */
static JSValue js_element_get_tagName(JSContext *ctx, JSValueConst this_val) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_NULL;
    char *s = osc_str_to_cstr_alloc(node->tag);
    JSValue r = JS_NewString(ctx, s ? s : "");
    free(s);
    return r;
}

/* element.textContent getter */
static JSValue js_element_get_textContent(JSContext *ctx, JSValueConst this_val) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_NULL;

    char buf[8192];
    int pos = 0;
    buf[0] = '\0';
    dom_collect_text(*data, buf, &pos, sizeof(buf));
    buf[pos] = '\0';
    return JS_NewString(ctx, buf);
}

/* element.textContent setter */
static JSValue js_element_set_textContent(JSContext *ctx, JSValueConst this_val,
                                           JSValueConst val) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node || !g_dom_nodes) return JS_EXCEPTION;

    const char *str = JS_ToCString(ctx, val);
    if (!str) return JS_EXCEPTION;

    /* Copy string into arena so it persists after JS_FreeCString */
    osc_str text_str = osc_str_concat(osc_global_arena,
                                       osc_str_from_cstr(str),
                                       osc_str_from_cstr(""));
    JS_FreeCString(ctx, str);

    /* Create a new text node and make it the only child */
    HtmlNode text_node;
    text_node.tag = osc_str_from_cstr("");
    text_node.text = text_str;
    text_node.attrs = osc_map_new(osc_global_arena);
    text_node.first_child = -1;
    text_node.next_sibling = -1;
    text_node.is_text = 1;

    osc_array_push(osc_global_arena, g_dom_nodes, &text_node);
    int32_t new_idx = osc_array_len(g_dom_nodes) - 1;

    /* Detach old children and set new text node as only child */
    node = dom_get_node(*data);  /* re-fetch after push may realloc */
    if (node) {
        node->first_child = new_idx;
        /* Detach siblings of the new text node */
        HtmlNode *tn = dom_get_node(new_idx);
        if (tn) tn->next_sibling = -1;
    }

    g_dom_dirty = 1;
    return JS_UNDEFINED;
}

/* element.getAttribute(name) */
static JSValue js_element_getAttribute(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data || argc < 1) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_NULL;

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;

    osc_str key = osc_str_from_cstr(name);
    JSValue result;
    if (osc_map_has(node->attrs, key)) {
        osc_str val = osc_map_get(node->attrs, key);
        char *s = osc_str_to_cstr_alloc(val);
        result = JS_NewString(ctx, s ? s : "");
        free(s);
    } else {
        result = JS_NULL;
    }
    JS_FreeCString(ctx, name);
    return result;
}

/* element.setAttribute(name, value) */
static JSValue js_element_setAttribute(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data || argc < 2) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_EXCEPTION;

    const char *name = JS_ToCString(ctx, argv[0]);
    const char *value = JS_ToCString(ctx, argv[1]);
    if (!name || !value) {
        if (name) JS_FreeCString(ctx, name);
        if (value) JS_FreeCString(ctx, value);
        return JS_EXCEPTION;
    }

    /* Copy strings into arena so they persist */
    osc_str key = osc_str_concat(osc_global_arena,
                                  osc_str_from_cstr(name),
                                  osc_str_from_cstr(""));
    osc_str val = osc_str_concat(osc_global_arena,
                                  osc_str_from_cstr(value),
                                  osc_str_from_cstr(""));

    osc_map_set(osc_global_arena, node->attrs, key, val);

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);
    g_dom_dirty = 1;
    return JS_UNDEFINED;
}

/* element.children getter */
static JSValue js_element_get_children(JSContext *ctx, JSValueConst this_val) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_NewArray(ctx);

    JSValue arr = JS_NewArray(ctx);
    int32_t child = node->first_child;
    uint32_t i = 0;
    while (child != -1) {
        HtmlNode *cn = dom_get_node(child);
        if (cn && !cn->is_text) {
            JS_SetPropertyUint32(ctx, arr, i++, js_element_wrap(ctx, child));
        }
        child = cn ? cn->next_sibling : -1;
    }
    return arr;
}

/* element.id getter */
static JSValue js_element_get_id(JSContext *ctx, JSValueConst this_val) {
    int32_t *data = (int32_t *)JS_GetOpaque(this_val, js_element_class_id);
    if (!data) return JS_EXCEPTION;
    HtmlNode *node = dom_get_node(*data);
    if (!node) return JS_NewString(ctx, "");

    osc_str key = osc_str_from_cstr("id");
    if (osc_map_has(node->attrs, key)) {
        osc_str val = osc_map_get(node->attrs, key);
        char *s = osc_str_to_cstr_alloc(val);
        JSValue r = JS_NewString(ctx, s ? s : "");
        free(s);
        return r;
    }
    return JS_NewString(ctx, "");
}

static const JSCFunctionListEntry js_element_proto_funcs[] = {
    JS_CGETSET_DEF("tagName", js_element_get_tagName, NULL),
    JS_CGETSET_DEF("textContent", js_element_get_textContent, js_element_set_textContent),
    JS_CGETSET_DEF("children", js_element_get_children, NULL),
    JS_CGETSET_DEF("id", js_element_get_id, NULL),
    JS_CFUNC_DEF("getAttribute", 1, js_element_getAttribute),
    JS_CFUNC_DEF("setAttribute", 2, js_element_setAttribute),
};

/* ── document object ─────────────────────────────────────── */

/* document.getElementById(id) */
static JSValue js_doc_getElementById(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    if (!g_dom_nodes || argc < 1) return JS_NULL;

    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_EXCEPTION;

    osc_str id_key = osc_str_from_cstr("id");
    int32_t count = osc_array_len(g_dom_nodes);
    for (int32_t i = 0; i < count; i++) {
        HtmlNode *node = (HtmlNode *)osc_array_get(g_dom_nodes, i);
        if (node->is_text) continue;
        if (osc_map_has(node->attrs, id_key)) {
            osc_str attr_id = osc_map_get(node->attrs, id_key);
            if (attr_id.len == (int32_t)strlen(id) &&
                memcmp(attr_id.data, id, attr_id.len) == 0) {
                JS_FreeCString(ctx, id);
                return js_element_wrap(ctx, i);
            }
        }
    }
    JS_FreeCString(ctx, id);
    return JS_NULL;
}

/* document.getElementsByTagName(tag) */
static JSValue js_doc_getElementsByTagName(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    if (!g_dom_nodes || argc < 1) return JS_NewArray(ctx);

    const char *tag = JS_ToCString(ctx, argv[0]);
    if (!tag) return JS_EXCEPTION;

    JSValue arr = JS_NewArray(ctx);
    int32_t count = osc_array_len(g_dom_nodes);
    uint32_t found = 0;
    for (int32_t i = 0; i < count; i++) {
        HtmlNode *node = (HtmlNode *)osc_array_get(g_dom_nodes, i);
        if (node->is_text) continue;
        if (node->tag.len == (int32_t)strlen(tag) &&
            memcmp(node->tag.data, tag, node->tag.len) == 0) {
            JS_SetPropertyUint32(ctx, arr, found++, js_element_wrap(ctx, i));
        }
    }
    JS_FreeCString(ctx, tag);
    return arr;
}

static void js_register_dom(JSContext *ctx) {
    /* Register Element class */
    JS_NewClassID(JS_GetRuntime(ctx), &js_element_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_element_class_id, &js_element_class);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_element_proto_funcs,
                                sizeof(js_element_proto_funcs) / sizeof(js_element_proto_funcs[0]));
    JS_SetClassProto(ctx, js_element_class_id, proto);

    /* Register document object */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, doc, "getElementById",
        JS_NewCFunction(ctx, js_doc_getElementById, "getElementById", 1));
    JS_SetPropertyStr(ctx, doc, "getElementsByTagName",
        JS_NewCFunction(ctx, js_doc_getElementsByTagName, "getElementsByTagName", 1));
    JS_SetPropertyStr(ctx, global, "document", doc);
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
    js_register_dom(ctx);
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
    g_dom_nodes = NULL;
    g_dom_dirty = 0;
}

/* ── DOM pointer + dirty flag ────────────────────────────── */

void js_bridge_set_dom(osc_array *nodes) {
    g_dom_nodes = nodes;
}

int32_t js_bridge_is_dom_dirty(void) {
    return g_dom_dirty;
}

void js_bridge_clear_dom_dirty(void) {
    g_dom_dirty = 0;
}
