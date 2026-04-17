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
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#endif

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


/* ── Verbose file logging ─────────────────────────────────
 * The browser is a GUI subsystem app on Windows, so stdout is
 * not attached to any terminal.  When the user passes --verbose
 * we append diagnostic lines to browser.log (cwd) with a
 * millisecond-precision wall-clock timestamp plus a monotonic
 * delta so we can see where time is being spent (e.g. TLS
 * handshake, recv loop, parse, render).
 */

static FILE *g_log_fp = NULL;
static int   g_log_enabled = 0;

static long long vlog_now_ms(void) {
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC)) {
        return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
    }
    return (long long)(clock() * 1000LL / CLOCKS_PER_SEC);
}

void vlog_enable(osc_str path) {
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    /* If the caller passed an empty path, auto-resolve a predictable
     * location: %TEMP%\oscanweb.log on Windows, /tmp/oscanweb.log
     * otherwise.  This keeps the log discoverable regardless of what
     * cwd the user's launcher chose. */
    char auto_path[1024];
    const char *p = NULL;
    if (path.len == 0) {
#ifdef _WIN32
        const char *tmp = getenv("TEMP");
        if (!tmp) tmp = getenv("TMP");
        if (!tmp) tmp = "C:\\Windows\\Temp";
        snprintf(auto_path, sizeof(auto_path), "%s\\oscanweb.log", tmp);
#else
        snprintf(auto_path, sizeof(auto_path), "/tmp/oscanweb.log");
#endif
        p = auto_path;
    } else {
        char *cp = osc_str_to_cstr_alloc(path);
        if (!cp) return;
        g_log_fp = fopen(cp, "w");
        free(cp);
        goto opened;
    }
    g_log_fp = fopen(p, "w");
opened:
    if (g_log_fp) {
        g_log_enabled = 1;
        fprintf(g_log_fp, "# OscaWeb verbose log\n");
        fflush(g_log_fp);
        /* Also shout the path on stderr for any console-attached run. */
        fprintf(stderr, "[oscanweb] verbose log: %s\n", p ? p : "(caller path)");
        fflush(stderr);
    }
}

void vlog_msg(osc_str tag, osc_str msg) {
    if (!g_log_enabled || !g_log_fp) return;
    long long ms = vlog_now_ms();
    char *t = osc_str_to_cstr_alloc(tag);
    char *m = osc_str_to_cstr_alloc(msg);
    fprintf(g_log_fp, "[%lld] %s %s\n",
            ms,
            t ? t : "",
            m ? m : "");
    fflush(g_log_fp);
    free(t); free(m);
}

void vlog_int(osc_str tag, int32_t value) {
    if (!g_log_enabled || !g_log_fp) return;
    long long ms = vlog_now_ms();
    char *t = osc_str_to_cstr_alloc(tag);
    fprintf(g_log_fp, "[%lld] %s %d\n",
            ms,
            t ? t : "",
            (int)value);
    fflush(g_log_fp);
    free(t);
}

int32_t vlog_is_enabled(void) {
    return g_log_enabled;
}


/* ── Socket recv timeout (SO_RCVTIMEO) ────────────────────
 * The Oscan runtime exposes raw OS sockets but has no timeout
 * primitive, so a server that accepts TCP but never sends data
 * (e.g. text.npr.org:80) will hang the browser forever.  This
 * FFI applies SO_RCVTIMEO + SO_SNDTIMEO to a socket fd; returns
 * 0 on success, nonzero on failure.
 */
int32_t net_set_recv_timeout(int32_t sock, int32_t ms) {
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    int r1 = setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO,
                        (const char *)&tv, sizeof(tv));
    int r2 = setsockopt((SOCKET)sock, SOL_SOCKET, SO_SNDTIMEO,
                        (const char *)&tv, sizeof(tv));
    return (int32_t)(r1 | r2);
#else
    (void)sock; (void)ms;
    return -1;
#endif
}


/* ── Bounded TCP connect ──────────────────────────────────
 * socket_connect in the Oscan runtime has no timeout; on Windows a
 * connect() to an unreachable host takes ~21s (SYN + 2 retries).
 * This FFI does the same thing but with a caller-specified timeout
 * by putting the socket temporarily into non-blocking mode and
 * waiting for the connection via select().  Returns 0 on success,
 * nonzero on timeout / error.
 */
int32_t net_connect_timeout_ipv4(int32_t sock, int32_t ip_be, int32_t port, int32_t ms) {
#ifdef _WIN32
    SOCKET s = (SOCKET)sock;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = (u_long)ip_be; /* already network byte order */

    u_long nb = 1;
    if (ioctlsocket(s, FIONBIO, &nb) != 0) return -1;

    int cr = connect(s, (struct sockaddr*)&addr, sizeof(addr));
    int rc = 0;
    if (cr != 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) { rc = -2; }
        else {
            fd_set wset, eset;
            FD_ZERO(&wset); FD_SET(s, &wset);
            FD_ZERO(&eset); FD_SET(s, &eset);
            struct timeval tv;
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            int sr = select(0, NULL, &wset, &eset, &tv);
            if (sr <= 0) { rc = -3; }
            else if (FD_ISSET(s, &eset)) { rc = -4; }
            /* sr > 0 and wset set → connected */
        }
    }

    /* Restore blocking mode regardless so subsequent recv/send behave
     * like the rest of the code expects (with SO_RCVTIMEO honored). */
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    return rc;
#else
    (void)sock; (void)ip_be; (void)port; (void)ms;
    return -1;
#endif
}

/* Resolve a hostname to an IPv4 address in network byte order.
 * Returns 0 on failure.  Uses getaddrinfo which has its own internal
 * timeouts on Windows but those are short enough in practice.
 */
int32_t net_resolve_ipv4(osc_str host) {
#ifdef _WIN32
    char *h = osc_str_to_cstr_alloc(host);
    if (!h) return 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo(h, NULL, &hints, &res);
    free(h);
    if (r != 0 || !res) return 0;
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    int32_t ip = (int32_t)sa->sin_addr.s_addr;
    freeaddrinfo(res);
    return ip;
#else
    (void)host;
    return 0;
#endif
}


/* ── WinHTTP-based HTTPS fetch ────────────────────────────
 * The Oscan runtime's built-in tls_connect occasionally hangs
 * forever on certain hosts (text.npr.org being the canonical
 * example).  There's no timeout primitive we can apply to it.
 *
 * WinHTTP is built into Windows, has robust timeouts (resolve,
 * connect, send, receive), handles redirects internally, and
 * shares its TLS stack with every other Windows app — including
 * Edge — so it handles the same protocol quirks they do.
 *
 * This function takes a full URL and returns the response body as
 * a string (via osc_str), or an empty string on error (with the
 * error message also returned via osc_str).  Caller owns nothing;
 * the returned osc_str's data is malloc'd and must be freed via
 * winhttp_free_response.
 */

typedef struct {
    osc_str body;
    osc_str error;
    int32_t status_code;
} WinHttpResult;

/* Convert a UTF-8 C string to a newly-malloc'd wide string. */
#ifdef _WIN32
static wchar_t *utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen);
    return w;
}
#endif

/* Copy a C buffer into a fresh osc_str allocated on the given arena
 * (so the Oscan side owns the memory lifetime normally). */
static osc_str osc_str_from_bytes(void *arena, const char *data, int32_t len) {
    /* Build a C-string-terminated copy via osc_str_concat to avoid
     * reimplementing arena allocation here.  concat with "" gives us
     * an arena-owned copy of the input bytes. */
    osc_str empty = { "", 0 };
    osc_str in = { data, len };
    return osc_str_concat(arena, empty, in);
}

WinHttpResult winhttp_fetch(void *arena, osc_str url_s, int32_t timeout_ms) {
    WinHttpResult r = {0};
    r.body  = (osc_str){ "", 0 };
    r.error = (osc_str){ "", 0 };
#ifdef _WIN32
    char *url = osc_str_to_cstr_alloc(url_s);
    if (!url) {
        r.error = osc_str_from_cstr("oom");
        return r;
    }

    wchar_t *wurl = utf8_to_wide(url);
    free(url);
    if (!wurl) {
        r.error = osc_str_from_cstr("utf16 conversion failed");
        return r;
    }

    /* Crack the URL. */
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]  = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName     = host;  uc.dwHostNameLength     = 256;
    uc.lpszUrlPath      = path;  uc.dwUrlPathLength      = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        free(wurl);
        r.error = osc_str_from_cstr("bad url");
        return r;
    }

    HINTERNET hSession = WinHttpOpen(L"OscaWeb/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        free(wurl);
        r.error = osc_str_from_cstr("WinHttpOpen failed");
        return r;
    }
    /* Set all four timeouts. */
    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        free(wurl);
        r.error = osc_str_from_cstr("WinHttpConnect failed");
        return r;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        free(wurl);
        r.error = osc_str_from_cstr("WinHttpOpenRequest failed");
        return r;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        free(wurl);
        char msg[64];
        snprintf(msg, sizeof(msg), "WinHttp send/recv failed (err=%lu)", err);
        r.error = osc_str_from_bytes(arena, msg, (int32_t)strlen(msg));
        return r;
    }

    /* Fetch status code. */
    DWORD status = 0;
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status_code = (int32_t)status;

    /* Read the body.  Grow a plain malloc buffer, then copy to the
     * arena at the end so the Oscan caller can keep it. */
    size_t cap  = 8192;
    size_t used = 0;
    char  *buf  = (char *)malloc(cap);
    if (!buf) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        free(wurl);
        r.error = osc_str_from_cstr("oom");
        return r;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;
        if (used + avail + 1 > cap) {
            while (used + avail + 1 > cap) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, buf + used, avail, &got)) break;
        if (got == 0) break;
        used += got;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);

    if (buf) {
        r.body = osc_str_from_bytes(arena, buf, (int32_t)used);
        free(buf);
    }
    return r;
#else
    (void)arena; (void)url_s; (void)timeout_ms;
    r.error = osc_str_from_cstr("winhttp not supported on this platform");
    return r;
#endif
}
