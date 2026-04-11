/*
 * TLS wrapper for OscaWeb browser.
 * Provides TLS functions callable from Oscan via C-FFI.
 *
 * Windows: uses SChannel (built-in, no external deps)
 * Linux:   uses OpenSSL  (requires -lssl -lcrypto)
 */

#include "osc_runtime.h"
#include "tls_wrapper.h"
#include <string.h>
#include <stdint.h>

#define MAX_CONNECTIONS 8

/* ================================================================ */
/*  Windows: SChannel implementation                                 */
/* ================================================================ */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schnlsp.h>
#include <wincrypt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

typedef struct {
    SOCKET       sock;
    CredHandle   cred;
    CtxtHandle   ctx;
    int          in_use;
    int          has_cred;
    int          has_ctx;
    /* Decrypted data buffer for partial reads */
    char         decrypted[65536];
    int          dec_len;
    int          dec_pos;
    /* Raw recv buffer for TLS records */
    char         raw_buf[65536];
    int          raw_len;
} tls_conn_t;

static tls_conn_t g_conns[MAX_CONNECTIONS];
static int g_initialized = 0;

int32_t tls_init(void) {
    if (g_initialized) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    memset(g_conns, 0, sizeof(g_conns));
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        g_conns[i].sock = INVALID_SOCKET;
    }
    g_initialized = 1;
    return 0;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!g_conns[i].in_use) return i;
    }
    return -1;
}

/* Perform the SChannel TLS handshake on a connected socket */
static int do_handshake(int slot, const char *hostname) {
    tls_conn_t *c = &g_conns[slot];
    SECURITY_STATUS ss;

    /* Acquire credentials */
    SCHANNEL_CRED sc_cred;
    memset(&sc_cred, 0, sizeof(sc_cred));
    sc_cred.dwVersion = SCHANNEL_CRED_VERSION;
    sc_cred.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION |
                      SCH_CRED_NO_DEFAULT_CREDS |
                      SCH_USE_STRONG_CRYPTO;
    sc_cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;

    ss = AcquireCredentialsHandleA(NULL, (SEC_CHAR*)UNISP_NAME_A,
                                   SECPKG_CRED_OUTBOUND, NULL, &sc_cred,
                                   NULL, NULL, &c->cred, NULL);
    if (ss != SEC_E_OK) return -1;
    c->has_cred = 1;

    /* Initial handshake token */
    SecBuffer out_buf = { 0, SECBUFFER_TOKEN, NULL };
    SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };
    DWORD ctx_flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY |
                      ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
                      ISC_REQ_STREAM;
    DWORD out_flags = 0;

    ss = InitializeSecurityContextA(&c->cred, NULL, (SEC_CHAR*)hostname,
                                    ctx_flags, 0, 0, NULL, 0,
                                    &c->ctx, &out_desc, &out_flags, NULL);
    c->has_ctx = 1;

    if (ss != SEC_I_CONTINUE_NEEDED && ss != SEC_E_OK) return -1;

    /* Send initial token */
    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
        int sent = send(c->sock, (char*)out_buf.pvBuffer, out_buf.cbBuffer, 0);
        FreeContextBuffer(out_buf.pvBuffer);
        if (sent <= 0) return -1;
    }

    /* Handshake loop */
    char hs_buf[65536];
    int hs_len = 0;

    while (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) {
        if (ss != SEC_E_INCOMPLETE_MESSAGE) {
            int r = recv(c->sock, hs_buf + hs_len, (int)(sizeof(hs_buf) - hs_len), 0);
            if (r <= 0) return -1;
            hs_len += r;
        }

        SecBuffer in_bufs[2];
        in_bufs[0].BufferType = SECBUFFER_TOKEN;
        in_bufs[0].cbBuffer = hs_len;
        in_bufs[0].pvBuffer = hs_buf;
        in_bufs[1].BufferType = SECBUFFER_EMPTY;
        in_bufs[1].cbBuffer = 0;
        in_bufs[1].pvBuffer = NULL;
        SecBufferDesc in_desc = { SECBUFFER_VERSION, 2, in_bufs };

        SecBuffer out_buf2 = { 0, SECBUFFER_TOKEN, NULL };
        SecBufferDesc out_desc2 = { SECBUFFER_VERSION, 1, &out_buf2 };

        ss = InitializeSecurityContextA(&c->cred, &c->ctx, (SEC_CHAR*)hostname,
                                        ctx_flags, 0, 0, &in_desc, 0,
                                        NULL, &out_desc2, &out_flags, NULL);

        if (ss == SEC_E_OK || ss == SEC_I_CONTINUE_NEEDED) {
            /* Send any output token */
            if (out_buf2.cbBuffer > 0 && out_buf2.pvBuffer) {
                send(c->sock, (char*)out_buf2.pvBuffer, out_buf2.cbBuffer, 0);
                FreeContextBuffer(out_buf2.pvBuffer);
            }
            /* Handle extra data */
            if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
                memmove(hs_buf, hs_buf + (hs_len - in_bufs[1].cbBuffer), in_bufs[1].cbBuffer);
                hs_len = in_bufs[1].cbBuffer;
            } else {
                hs_len = 0;
            }
        } else if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            /* Need more data, continue reading */
        } else {
            if (out_buf2.pvBuffer) FreeContextBuffer(out_buf2.pvBuffer);
            return -1;
        }
    }

    /* Save any leftover data after handshake */
    if (hs_len > 0) {
        memcpy(c->raw_buf, hs_buf, hs_len);
        c->raw_len = hs_len;
    }

    return 0;
}

int32_t tls_connect_to(osc_str host, int32_t port) {
    if (!g_initialized) { if (tls_init() != 0) return -1; }

    int slot = find_free_slot();
    if (slot < 0) return -1;

    tls_conn_t *c = &g_conns[slot];
    memset(c, 0, sizeof(*c));
    c->sock = INVALID_SOCKET;

    /* Null-terminate hostname */
    char hostname[256];
    int hlen = host.len < 255 ? host.len : 255;
    memcpy(hostname, host.data, hlen);
    hostname[hlen] = '\0';

    char port_str[16];
    /* Manual int-to-string since _snprintf may not be available in freestanding */
    {
        int p = (int)port;
        int idx = 0;
        char tmp[16];
        if (p == 0) { tmp[idx++] = '0'; }
        else { while (p > 0) { tmp[idx++] = '0' + (p % 10); p /= 10; } }
        for (int j = 0; j < idx; j++) port_str[j] = tmp[idx - 1 - j];
        port_str[idx] = '\0';
    }

    /* Resolve and connect */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, port_str, &hints, &res) != 0 || !res) return -1;

    c->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (c->sock == INVALID_SOCKET) { freeaddrinfo(res); return -1; }

    if (connect(c->sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        closesocket(c->sock); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    /* TLS handshake */
    if (do_handshake(slot, hostname) != 0) {
        closesocket(c->sock);
        if (c->has_ctx)  DeleteSecurityContext(&c->ctx);
        if (c->has_cred) FreeCredentialsHandle(&c->cred);
        memset(c, 0, sizeof(*c));
        c->sock = INVALID_SOCKET;
        return -1;
    }

    c->in_use = 1;
    return (int32_t)slot;
}

int32_t tls_send_bytes(int32_t handle, osc_str data, int32_t data_len) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return -1;
    tls_conn_t *c = &g_conns[handle];

    SecPkgContext_StreamSizes sizes;
    if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_STREAM_SIZES, &sizes) != SEC_E_OK)
        return -1;

    int to_send = data_len < data.len ? data_len : data.len;
    int total_sent = 0;

    while (total_sent < to_send) {
        int chunk = to_send - total_sent;
        if ((DWORD)chunk > sizes.cbMaximumMessage) chunk = (int)sizes.cbMaximumMessage;

        int buf_size = (int)(sizes.cbHeader + chunk + sizes.cbTrailer);
        char *msg_buf = (char*)HeapAlloc(GetProcessHeap(), 0, buf_size);
        if (!msg_buf) return -1;

        memcpy(msg_buf + sizes.cbHeader, data.data + total_sent, chunk);

        SecBuffer bufs[4];
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        bufs[0].cbBuffer   = sizes.cbHeader;
        bufs[0].pvBuffer   = msg_buf;
        bufs[1].BufferType = SECBUFFER_DATA;
        bufs[1].cbBuffer   = chunk;
        bufs[1].pvBuffer   = msg_buf + sizes.cbHeader;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        bufs[2].cbBuffer   = sizes.cbTrailer;
        bufs[2].pvBuffer   = msg_buf + sizes.cbHeader + chunk;
        bufs[3].BufferType = SECBUFFER_EMPTY;
        bufs[3].cbBuffer   = 0;
        bufs[3].pvBuffer   = NULL;
        SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

        SECURITY_STATUS ss = EncryptMessage(&c->ctx, 0, &desc, 0);
        if (ss != SEC_E_OK) { HeapFree(GetProcessHeap(), 0, msg_buf); return -1; }

        int enc_len = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
        int s = send(c->sock, msg_buf, enc_len, 0);
        HeapFree(GetProcessHeap(), 0, msg_buf);
        if (s <= 0) return -1;

        total_sent += chunk;
    }
    return total_sent;
}

/* Decrypt buffered data; returns number of decrypted bytes available */
static int decrypt_data(tls_conn_t *c) {
    if (c->raw_len == 0) return 0;

    SecBuffer bufs[4];
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[0].cbBuffer   = c->raw_len;
    bufs[0].pvBuffer   = c->raw_buf;
    bufs[1].BufferType = SECBUFFER_EMPTY;
    bufs[1].cbBuffer = 0; bufs[1].pvBuffer = NULL;
    bufs[2].BufferType = SECBUFFER_EMPTY;
    bufs[2].cbBuffer = 0; bufs[2].pvBuffer = NULL;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    bufs[3].cbBuffer = 0; bufs[3].pvBuffer = NULL;
    SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

    SECURITY_STATUS ss = DecryptMessage(&c->ctx, &desc, 0, NULL);

    if (ss == SEC_E_OK) {
        /* Find the DATA and EXTRA buffers */
        for (int i = 0; i < 4; i++) {
            if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0) {
                int space = (int)sizeof(c->decrypted) - c->dec_len;
                int copy = (int)bufs[i].cbBuffer;
                if (copy > space) copy = space;
                memcpy(c->decrypted + c->dec_len, bufs[i].pvBuffer, copy);
                c->dec_len += copy;
            }
        }
        /* Keep extra data */
        c->raw_len = 0;
        for (int i = 0; i < 4; i++) {
            if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].cbBuffer > 0) {
                memmove(c->raw_buf, bufs[i].pvBuffer, bufs[i].cbBuffer);
                c->raw_len = (int)bufs[i].cbBuffer;
            }
        }
        return 1;
    } else if (ss == SEC_E_INCOMPLETE_MESSAGE) {
        return 0; /* Need more data */
    } else {
        return -1; /* Connection closed or error */
    }
}

int32_t tls_recv_byte(int32_t handle) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return -1;
    tls_conn_t *c = &g_conns[handle];

    /* Return from decrypted buffer if available */
    if (c->dec_pos < c->dec_len) {
        return (int32_t)(unsigned char)c->decrypted[c->dec_pos++];
    }

    /* Reset buffer positions */
    c->dec_pos = 0;
    c->dec_len = 0;

    /* Try to decrypt existing raw data first */
    int dr = decrypt_data(c);
    if (dr > 0 && c->dec_len > 0) {
        return (int32_t)(unsigned char)c->decrypted[c->dec_pos++];
    }
    if (dr < 0) return -1;

    /* Read more raw data and try to decrypt */
    for (int attempts = 0; attempts < 100; attempts++) {
        int space = (int)sizeof(c->raw_buf) - c->raw_len;
        if (space <= 0) return -1;

        int r = recv(c->sock, c->raw_buf + c->raw_len, space, 0);
        if (r <= 0) return -1;
        c->raw_len += r;

        dr = decrypt_data(c);
        if (dr > 0 && c->dec_len > 0) {
            return (int32_t)(unsigned char)c->decrypted[c->dec_pos++];
        }
        if (dr < 0) return -1;
        /* dr == 0 means incomplete, need more data */
    }
    return -1;
}

void tls_close_conn(int32_t handle) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return;
    tls_conn_t *c = &g_conns[handle];

    /* Send shutdown notification */
    DWORD shutdown_token = SCHANNEL_SHUTDOWN;
    SecBuffer shut_buf = { sizeof(shutdown_token), SECBUFFER_TOKEN, &shutdown_token };
    SecBufferDesc shut_desc = { SECBUFFER_VERSION, 1, &shut_buf };
    ApplyControlToken(&c->ctx, &shut_desc);

    SecBuffer out_buf = { 0, SECBUFFER_TOKEN, NULL };
    SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };
    DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    DWORD out_flags = 0;
    InitializeSecurityContextA(&c->cred, &c->ctx, NULL, flags, 0, 0,
                               &shut_desc, 0, NULL, &out_desc, &out_flags, NULL);
    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
        send(c->sock, (char*)out_buf.pvBuffer, out_buf.cbBuffer, 0);
        FreeContextBuffer(out_buf.pvBuffer);
    }

    if (c->has_ctx)  DeleteSecurityContext(&c->ctx);
    if (c->has_cred) FreeCredentialsHandle(&c->cred);
    closesocket(c->sock);

    memset(c, 0, sizeof(*c));
    c->sock = INVALID_SOCKET;
}

void tls_cleanup(void) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (g_conns[i].in_use) tls_close_conn((int32_t)i);
    }
    g_initialized = 0;
    WSACleanup();
}

/* ================================================================ */
/*  Linux / macOS: OpenSSL implementation                            */
/* ================================================================ */
#else

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

typedef struct {
    SSL *ssl;
    int  sock;
    int  in_use;
} tls_conn_t;

static SSL_CTX    *g_ctx = NULL;
static tls_conn_t  g_conns[MAX_CONNECTIONS];
static int          g_initialized = 0;

int32_t tls_init(void) {
    if (g_initialized) return 0;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    g_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ctx) return -1;
    SSL_CTX_set_default_verify_paths(g_ctx);
    memset(g_conns, 0, sizeof(g_conns));
    g_initialized = 1;
    return 0;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!g_conns[i].in_use) return i;
    }
    return -1;
}

int32_t tls_connect_to(osc_str host, int32_t port) {
    if (!g_initialized) { if (tls_init() != 0) return -1; }
    int slot = find_free_slot();
    if (slot < 0) return -1;

    char hostname[256];
    int len = host.len < 255 ? host.len : 255;
    memcpy(hostname, host.data, len);
    hostname[len] = '\0';

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, port_str, &hints, &res) != 0 || !res) return -1;

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return -1; }
    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        close(sock); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    SSL *ssl = SSL_new(g_ctx);
    if (!ssl) { close(sock); return -1; }
    SSL_set_tlsext_host_name(ssl, hostname);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); close(sock); return -1; }

    g_conns[slot].ssl = ssl;
    g_conns[slot].sock = sock;
    g_conns[slot].in_use = 1;
    return (int32_t)slot;
}

int32_t tls_send_bytes(int32_t handle, osc_str data, int32_t data_len) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return -1;
    int to_send = data_len < data.len ? data_len : data.len;
    int w = SSL_write(g_conns[handle].ssl, data.data, to_send);
    return w > 0 ? (int32_t)w : -1;
}

int32_t tls_recv_byte(int32_t handle) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return -1;
    unsigned char b;
    int n = SSL_read(g_conns[handle].ssl, &b, 1);
    return n > 0 ? (int32_t)b : -1;
}

void tls_close_conn(int32_t handle) {
    if (handle < 0 || handle >= MAX_CONNECTIONS || !g_conns[handle].in_use) return;
    SSL_shutdown(g_conns[handle].ssl);
    SSL_free(g_conns[handle].ssl);
    close(g_conns[handle].sock);
    memset(&g_conns[handle], 0, sizeof(tls_conn_t));
}

void tls_cleanup(void) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (g_conns[i].in_use) tls_close_conn((int32_t)i);
    }
    if (g_ctx) { SSL_CTX_free(g_ctx); g_ctx = NULL; }
    g_initialized = 0;
}

#endif /* _WIN32 */
