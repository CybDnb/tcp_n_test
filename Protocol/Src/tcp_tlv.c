#include "tcp_tlv.h"
#include <string.h>
#include <arpa/inet.h>


uint64_t htonll_u64(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(x & 0xffffffffULL)) << 32) | htonl((uint32_t)(x >> 32));
#else
    return x;
#endif
}
uint64_t ntohll_u64(uint64_t x) { return htonll_u64(x); }

uint8_t* tlv_put(uint8_t *out, uint8_t type, const void *val, uint32_t len) {
    if (!out) return NULL;
    out[0] = type;
    uint32_t be_len = htonl(len);                          
    memcpy(out + TLV_TYPE_LEN, &be_len, TLV_LEN_LEN);      
    if (len && val) memcpy(out + TLV_HEADER_LEN, val, len);
    return out + TLV_HEADER_LEN + len;
}

uint8_t* tlv_put_u64(uint8_t *out, uint8_t type, uint64_t v) {
    uint64_t be = htonll_u64(v);
    return tlv_put(out, type, &be, TLV_U64_LEN); 
}

uint8_t* tlv_put_u32(uint8_t *out, uint8_t type, uint32_t v) {
    uint32_t be = htonl(v);
    return tlv_put(out, type, &be, TLV_U32_LEN); 
}

int tlv_walk(const uint8_t *p, uint32_t L,
             void (*cb)(uint8_t, const uint8_t*, uint32_t, void*),
             void *arg) {
    uint32_t off = 0;
    while (off + TLV_HEADER_LEN <= L) {
        uint8_t  t = p[off];
        uint32_t n;
        memcpy(&n, p + off + TLV_TYPE_LEN, TLV_LEN_LEN);   
        n = ntohl(n);                                      
        if (off + TLV_HEADER_LEN + n > L) return -1;
        if (cb) cb(t, p + off + TLV_HEADER_LEN, n, arg);
        off += TLV_HEADER_LEN + n;
    }
    return (off == L) ? 0 : -2;
}

int build_payload_file_start(const char *filename, uint64_t file_size,
                             uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len) {
    uint32_t name_len = (uint32_t)strlen(filename);
    uint32_t need = (uint32_t)(TLV_HEADER_LEN + name_len + TLV_HEADER_LEN + TLV_U64_LEN);
    if (out_cap < need) return -1;
    uint8_t *w = out_buf;
    w = tlv_put(w, TLV_FILENAME, filename, (uint16_t)name_len);
    if (!w) return -2;
    w = tlv_put_u64(w, TLV_FILESIZE, file_size);
    if (!w) return -3;
    *out_len = (uint32_t)(w - out_buf);
    return 0;
}

int build_payload_file_data(uint64_t offset, const uint8_t *data, uint32_t data_len,
                            uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len) {
    uint32_t need = TLV_HEADER_LEN + TLV_U64_LEN + TLV_HEADER_LEN + data_len;
    if (out_cap < need) return -1;
    uint8_t *w = out_buf;
    w = tlv_put_u64(w, TLV_OFFSET, offset);
    if (!w) return -2;
    w = tlv_put(w, TLV_DATA, data, data_len);
    if (!w) return -3;
    *out_len = (uint32_t)(w - out_buf);
    return 0;
}

typedef struct {
    char     *fname;
    uint32_t  fname_cap;
    uint64_t *fsize;
} _start_parse_ctx;

static void _cb_start(uint8_t t, const uint8_t *v, uint32_t n, void *arg) {
    _start_parse_ctx *ctx = (_start_parse_ctx*)arg;
    if (t == TLV_FILENAME) {
        uint32_t c = (n < ctx->fname_cap-1) ? n : ctx->fname_cap-1;
        memcpy(ctx->fname, v, c);
        ctx->fname[c] = '\0';
    } else if (t == TLV_FILESIZE) {
        // 8 -> TLV_U64_LEN
        if (n == TLV_U64_LEN && ctx->fsize) {
            uint64_t be; memcpy(&be, v, TLV_U64_LEN); 
            *ctx->fsize = ntohll_u64(be);
        }
    }
}

int parse_payload_file_start(const uint8_t *p, uint32_t L,
                             char *filename_buf, uint32_t fname_cap,
                             uint64_t *file_size) {
    _start_parse_ctx ctx = { .fname = filename_buf, .fname_cap = fname_cap, .fsize = file_size };
    int r = tlv_walk(p, L, _cb_start, &ctx);
    if (r < 0) return r;
    if (!filename_buf[0]) return -10; 
    if (file_size && *file_size == 0) {

    }
    return 0;
}

typedef struct {
    uint64_t     *off;
    const uint8_t**data;
    uint32_t     *len;
} _data_parse_ctx;

static void _cb_data(uint8_t t, const uint8_t *v, uint32_t n, void *arg) {
    _data_parse_ctx *ctx = (_data_parse_ctx*)arg;
    if (t == TLV_OFFSET && n == TLV_U64_LEN) {
        uint64_t be; memcpy(&be, v, TLV_U64_LEN); 
        if (ctx->off) *ctx->off = ntohll_u64(be);
    } else if (t == TLV_DATA) {
        if (ctx->data) *ctx->data = v;
        if (ctx->len)  *ctx->len  = n;
    }
}

int parse_payload_file_data(const uint8_t *p, uint32_t L,
                            uint64_t *offset,
                            const uint8_t **data_ptr, uint32_t *data_len) {
    _data_parse_ctx ctx = { .off = offset, .data = data_ptr, .len = data_len };
    int r = tlv_walk(p, L, _cb_data, &ctx);
    if (r < 0) return r;
    if (!data_ptr || !*data_ptr) return -10;
    if (data_len && *data_len == 0) return -11;
    return 0;
}