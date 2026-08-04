/*
 * osrs/gzip.c — GZip/DEFLATE decompressor implementation
 * Pure C23, zero external dependencies.
 *
 * Implements full DEFLATE decompression (RFC 1951) and GZip wrapper (RFC 1952):
 * - Bitstream reading
 * - Fixed and dynamic Huffman tables
 * - LZ77 window (32KB)
 * - Block decoding (stored, fixed, dynamic)
 *
 * Reference: RFC 1951 (DEFLATE), RFC 1952 (GZIP)
 */

#include "osrs/gzip.h"
#include <stdlib.h>
#include <string.h>

#define MAX_WINDOW_SIZE     32768
#define MAX_CODE_LEN        15
#define MAX_LIT_CODES       288
#define MAX_DIST_CODES      32
#define MAX_CL_CODES        19

/* Bit reader (LSB-first) */
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t bit_buf;
    int bit_cnt;
} bit_reader_t;

/* Huffman table */
typedef struct {
    uint16_t counts[MAX_CODE_LEN + 1];
    uint16_t symbols[MAX_LIT_CODES];
} huffman_t;

/* GZip context */
struct osrs_gzip_ctx {
    bit_reader_t br;
    uint8_t window[MAX_WINDOW_SIZE];
    int window_pos;
    huffman_t lit_table;
    huffman_t dist_table;
};

/* Initialize bit reader */
static void br_init(bit_reader_t *br, const uint8_t *data, size_t len)
{
    br->data = data;
    br->len = len;
    br->pos = 0;
    br->bit_buf = 0;
    br->bit_cnt = 0;
}

/* Read n bits (LSB-first) */
static uint32_t br_read_bits(bit_reader_t *br, int n)
{
    while (br->bit_cnt < n) {
        if (br->pos >= br->len) {
            br->bit_buf |= 0;
        } else {
            br->bit_buf |= (uint32_t)br->data[br->pos++] << br->bit_cnt;
        }
        br->bit_cnt += 8;
    }
    uint32_t result = br->bit_buf & ((1u << n) - 1);
    br->bit_buf >>= n;
    br->bit_cnt -= n;
    return result;
}

/* Build Huffman table from code lengths */
static bool huffman_build(huffman_t *h, const uint8_t *lengths, int n)
{
    /* Count code lengths */
    for (int i = 0; i <= MAX_CODE_LEN; i++) h->counts[i] = 0;
    for (int i = 0; i < n; i++) {
        if (lengths[i] > MAX_CODE_LEN) return false;
        h->counts[lengths[i]]++;
    }
    h->counts[0] = 0;

    /* Check for over-subscription */
    int left = 1;
    for (int i = 1; i <= MAX_CODE_LEN; i++) {
        left <<= 1;
        left -= h->counts[i];
        if (left < 0) return false;
    }

    /* Generate offsets */
    uint16_t offs[MAX_CODE_LEN + 1];
    offs[1] = 0;
    for (int i = 1; i < MAX_CODE_LEN; i++) {
        offs[i + 1] = (uint16_t)(offs[i] + h->counts[i]);
    }

    /* Assign symbols */
    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbols[offs[lengths[i]]++] = (uint16_t)i;
        }
    }

    return true;
}

/* Decode one Huffman symbol */
static int huffman_decode(bit_reader_t *br, const huffman_t *h)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= MAX_CODE_LEN; len++) {
        code |= (int)br_read_bits(br, 1);
        int count = h->counts[len];
        if (code - first < count) {
            return h->symbols[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Length codes for LZ77 */
static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

/* Distance codes for LZ77 */
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};

static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Code length order for dynamic Huffman */
static const uint8_t cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* Copy from window */
static void window_copy(osrs_gzip_t *ctx, uint8_t *output, size_t *out_pos,
                        int dist, int len)
{
    for (int i = 0; i < len; i++) {
        int src = (ctx->window_pos - dist + MAX_WINDOW_SIZE) % MAX_WINDOW_SIZE;
        uint8_t b = ctx->window[src];
        output[(*out_pos)++] = b;
        ctx->window[ctx->window_pos] = b;
        ctx->window_pos = (ctx->window_pos + 1) % MAX_WINDOW_SIZE;
    }
}

/* Decode stored block */
static bool decode_stored(osrs_gzip_t *ctx, uint8_t *output, size_t *out_pos, size_t out_cap)
{
    /* Skip to byte boundary */
    ctx->br.bit_cnt = 0;
    ctx->br.bit_buf = 0;

    if (ctx->br.pos + 4 > ctx->br.len) return false;
    int len = ctx->br.data[ctx->br.pos] | (ctx->br.data[ctx->br.pos + 1] << 8);
    int nlen = ctx->br.data[ctx->br.pos + 2] | (ctx->br.data[ctx->br.pos + 3] << 8);
    ctx->br.pos += 4;

    if (len != (~nlen & 0xFFFF)) return false;
    if (ctx->br.pos + (size_t)len > ctx->br.len) return false;
    if (*out_pos + (size_t)len > out_cap) return false;

    for (int i = 0; i < len; i++) {
        uint8_t b = ctx->br.data[ctx->br.pos++];
        output[(*out_pos)++] = b;
        ctx->window[ctx->window_pos] = b;
        ctx->window_pos = (ctx->window_pos + 1) % MAX_WINDOW_SIZE;
    }
    return true;
}

/* Decode block with Huffman tables */
static bool decode_block(osrs_gzip_t *ctx, uint8_t *output, size_t *out_pos, size_t out_cap)
{
    while (1) {
        int sym = huffman_decode(&ctx->br, &ctx->lit_table);
        if (sym < 0) return false;

        if (sym < 256) {
            /* Literal */
            if (*out_pos >= out_cap) return false;
            output[(*out_pos)++] = (uint8_t)sym;
            ctx->window[ctx->window_pos] = (uint8_t)sym;
            ctx->window_pos = (ctx->window_pos + 1) % MAX_WINDOW_SIZE;
        } else if (sym == 256) {
            /* End of block */
            return true;
        } else {
            /* Length/distance pair */
            sym -= 257;
            if (sym >= 29) return false;

            int len = length_base[sym] + (int)br_read_bits(&ctx->br, length_extra[sym]);

            sym = huffman_decode(&ctx->br, &ctx->dist_table);
            if (sym < 0 || sym >= 30) return false;

            int dist = dist_base[sym] + (int)br_read_bits(&ctx->br, dist_extra[sym]);

            if (*out_pos + (size_t)len > out_cap) return false;
            window_copy(ctx, output, out_pos, dist, len);
        }
    }
}

/* Build fixed Huffman tables */
static void build_fixed_tables(osrs_gzip_t *ctx)
{
    uint8_t lengths[MAX_LIT_CODES];

    /* Literal/length codes */
    for (int i = 0; i < 144; i++) lengths[i] = 8;
    for (int i = 144; i < 256; i++) lengths[i] = 9;
    for (int i = 256; i < 280; i++) lengths[i] = 7;
    for (int i = 280; i < 288; i++) lengths[i] = 8;
    huffman_build(&ctx->lit_table, lengths, 288);

    /* Distance codes */
    for (int i = 0; i < 30; i++) lengths[i] = 5;
    huffman_build(&ctx->dist_table, lengths, 30);
}

/* Read dynamic Huffman tables */
static bool read_dynamic_tables(osrs_gzip_t *ctx)
{
    int hlit = (int)br_read_bits(&ctx->br, 5) + 257;
    int hdist = (int)br_read_bits(&ctx->br, 5) + 1;
    int hclen = (int)br_read_bits(&ctx->br, 4) + 4;

    if (hlit > MAX_LIT_CODES || hdist > MAX_DIST_CODES) return false;

    /* Read code length code lengths */
    uint8_t cl_lengths[MAX_CL_CODES] = {0};
    for (int i = 0; i < hclen; i++) {
        cl_lengths[cl_order[i]] = (uint8_t)br_read_bits(&ctx->br, 3);
    }

    huffman_t cl_table;
    if (!huffman_build(&cl_table, cl_lengths, 19)) return false;

    /* Read literal/length and distance code lengths */
    uint8_t lengths[MAX_LIT_CODES + MAX_DIST_CODES];
    int n = 0;
    while (n < hlit + hdist) {
        int sym = huffman_decode(&ctx->br, &cl_table);
        if (sym < 0) return false;

        if (sym < 16) {
            lengths[n++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (n == 0) return false;
            int rep = (int)br_read_bits(&ctx->br, 2) + 3;
            uint8_t prev = lengths[n - 1];
            for (int i = 0; i < rep && n < hlit + hdist; i++) {
                lengths[n++] = prev;
            }
        } else if (sym == 17) {
            int rep = (int)br_read_bits(&ctx->br, 3) + 3;
            for (int i = 0; i < rep && n < hlit + hdist; i++) {
                lengths[n++] = 0;
            }
        } else if (sym == 18) {
            int rep = (int)br_read_bits(&ctx->br, 7) + 11;
            for (int i = 0; i < rep && n < hlit + hdist; i++) {
                lengths[n++] = 0;
            }
        } else {
            return false;
        }
    }

    if (!huffman_build(&ctx->lit_table, lengths, hlit)) return false;
    if (!huffman_build(&ctx->dist_table, lengths + hlit, hdist)) return false;
    return true;
}

osrs_gzip_t *osrs_gzip_create(void)
{
    osrs_gzip_t *ctx = calloc(1, sizeof(osrs_gzip_t));
    return ctx;
}

void osrs_gzip_destroy(osrs_gzip_t *ctx)
{
    free(ctx);
}

bool osrs_gzip_decompress(osrs_gzip_t *ctx,
                          const uint8_t *compressed, size_t compressed_len,
                          uint8_t *output, size_t *output_len)
{
    if (!ctx || !compressed || !output || !output_len) return false;
    if (compressed_len < 10) return false;

    /* Check GZip magic */
    if (compressed[0] != 0x1F || compressed[1] != 0x8B) return false;
    if (compressed[2] != 8) return false;  /* Deflate method */

    uint8_t flags = compressed[3];
    size_t pos = 10;

    /* Skip extra fields */
    if (flags & 0x04) {  /* FEXTRA */
        if (pos + 2 > compressed_len) return false;
        int xlen = compressed[pos] | (compressed[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flags & 0x08) {  /* FNAME */
        while (pos < compressed_len && compressed[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x10) {  /* FCOMMENT */
        while (pos < compressed_len && compressed[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x02) {  /* FHCRC */
        pos += 2;
    }

    if (pos >= compressed_len) return false;

    /* Initialize bit reader for deflate data */
    br_init(&ctx->br, compressed + pos, compressed_len - pos - 8);  /* Exclude CRC32 and ISIZE */
    ctx->window_pos = 0;

    size_t out_pos = 0;
    size_t out_cap = *output_len;
    bool final = false;

    while (!final) {
        final = br_read_bits(&ctx->br, 1) != 0;
        int type = (int)br_read_bits(&ctx->br, 2);

        switch (type) {
            case 0:  /* Stored */
                if (!decode_stored(ctx, output, &out_pos, out_cap)) return false;
                break;
            case 1:  /* Fixed Huffman */
                build_fixed_tables(ctx);
                if (!decode_block(ctx, output, &out_pos, out_cap)) return false;
                break;
            case 2:  /* Dynamic Huffman */
                if (!read_dynamic_tables(ctx)) return false;
                if (!decode_block(ctx, output, &out_pos, out_cap)) return false;
                break;
            default:
                return false;
        }
    }

    *output_len = out_pos;
    return true;
}

bool osrs_gzip_decompress_oneshot(const uint8_t *compressed, size_t compressed_len,
                                  uint8_t *output, size_t *output_len)
{
    osrs_gzip_t *ctx = osrs_gzip_create();
    if (!ctx) return false;
    bool result = osrs_gzip_decompress(ctx, compressed, compressed_len, output, output_len);
    osrs_gzip_destroy(ctx);
    return result;
}

size_t osrs_gzip_decompressed_size(const uint8_t *compressed, size_t compressed_len)
{
    if (compressed_len < 4) return 0;
    /* ISIZE is last 4 bytes of gzip stream */
    size_t pos = compressed_len - 4;
    return (size_t)compressed[pos] |
           ((size_t)compressed[pos + 1] << 8) |
           ((size_t)compressed[pos + 2] << 16) |
           ((size_t)compressed[pos + 3] << 24);
}
