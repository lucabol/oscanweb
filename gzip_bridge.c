/*
 * gzip_bridge.c — gzip/deflate decompression for OscaWeb HTTP layer.
 *
 * Exposes a single function to Oscan:
 *   gzip_decode(input: str) -> str
 * Returns the decompressed bytes on success, empty string on failure.
 *
 * Handles three Content-Encoding wire formats:
 *   - gzip:    RFC 1952 (10-byte header + raw deflate + 8-byte trailer)
 *   - deflate: RFC 1950 zlib wrapper (zlib header + raw deflate + adler32)
 *   - raw deflate (some buggy servers send Content-Encoding: deflate as raw)
 *
 * Built via build.ps1's --extra-c hook, alongside js_bridge.c and quickjs.c.
 */

#include "libs/miniz/miniz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* osc_str layout must match js_bridge.c (Oscan ABI). */
typedef struct { const char *data; int32_t len; } osc_str;

extern osc_str osc_str_concat(void *arena, osc_str a, osc_str b);
extern void *osc_global_arena;

static osc_str gzip_empty(void) {
    osc_str e;
    e.data = "";
    e.len = 0;
    return e;
}

/* Skip the RFC 1952 gzip header. Returns the offset of the raw deflate
 * stream, or -1 on malformed input. */
static int32_t gzip_skip_header(const uint8_t *src, int32_t len) {
    if (len < 10) return -1;
    if (src[0] != 0x1f || src[1] != 0x8b) return -1;  /* magic */
    if (src[2] != 8) return -1;                       /* CM = deflate */
    uint8_t flg = src[3];
    int32_t off = 10;
    if (flg & 0x04) {                                 /* FEXTRA */
        if (off + 2 > len) return -1;
        int32_t xlen = src[off] | (src[off + 1] << 8);
        off += 2 + xlen;
        if (off > len) return -1;
    }
    if (flg & 0x08) {                                 /* FNAME */
        while (off < len && src[off] != 0) off++;
        if (off >= len) return -1;
        off++;
    }
    if (flg & 0x10) {                                 /* FCOMMENT */
        while (off < len && src[off] != 0) off++;
        if (off >= len) return -1;
        off++;
    }
    if (flg & 0x02) {                                 /* FHCRC */
        off += 2;
        if (off > len) return -1;
    }
    return off;
}

/* Decode a Content-Encoding payload. Auto-detects gzip vs zlib/deflate. */
osc_str gzip_decode(osc_str input) {
    if (input.len <= 0 || input.data == NULL) return gzip_empty();

    const uint8_t *src = (const uint8_t *)input.data;
    int32_t src_len = input.len;
    int32_t off = 0;
    int32_t flags = 0;

    if (src_len >= 2 && src[0] == 0x1f && src[1] == 0x8b) {
        /* gzip wrapper */
        int32_t h = gzip_skip_header(src, src_len);
        if (h < 0) return gzip_empty();
        off = h;
        /* Trim 8-byte trailer (CRC32 + ISIZE) so tinfl doesn't see junk. */
        if (src_len - off < 8) return gzip_empty();
        src_len -= 8;
        /* raw deflate, no zlib header */
    } else if (src_len >= 2 && (src[0] & 0x0f) == 0x08
                            && (((uint16_t)src[0] << 8) | src[1]) % 31 == 0) {
        /* zlib header detected — let tinfl parse it. */
        flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
    } else {
        /* Assume raw deflate. */
    }

    if (src_len - off <= 0) return gzip_empty();

    size_t out_len = 0;
    void *out = tinfl_decompress_mem_to_heap(
        src + off, (size_t)(src_len - off), &out_len, flags);
    if (out == NULL) return gzip_empty();

    /* Copy into Oscan arena so the runtime owns the bytes. */
    osc_str result_view;
    result_view.data = (const char *)out;
    result_view.len = (int32_t)out_len;
    osc_str copied = osc_str_concat(osc_global_arena, result_view, gzip_empty());
    free(out);
    return copied;
}
