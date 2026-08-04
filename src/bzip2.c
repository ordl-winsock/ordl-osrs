/*
 * osrs/bzip2.c — BZip2 decompressor implementation
 * Pure C23, zero external dependencies.
 *
 * Implements full BZip2 decompression:
 * - Bitstream reading
 * - Huffman decoding (with multiple Huffman tables)
 * - Move-to-front decoding
 * - Run-length decoding (RLE1 and RLE2)
 * - Inverse Burrows-Wheeler transform
 *
 * Reference: bzip2 1.0.8 source (public domain algorithm description)
 */

#include "osrs/bzip2.h"
#include <stdlib.h>
#include <string.h>

#define BZ_MAX_GROUPS 6
#define BZ_MAX_ALPHA_SIZE 258
#define BZ_MAX_CODE_LEN 23
#define BZ_GROUP_SIZE 50
#define BZ_MAX_BLOCK_SIZE 900000

/* Bit reader state — tracks exact bit position */
typedef struct {
  const uint8_t *data;
  size_t len;
  size_t byte_pos;
  int bit_pos; /* 0-7, MSB-first */
} bit_reader_t;

/* Huffman decoding table */
typedef struct {
  int min_len;
  int max_len;
  int limit[BZ_MAX_CODE_LEN + 1];
  int base[BZ_MAX_CODE_LEN + 1];
  int perm[BZ_MAX_ALPHA_SIZE];
  uint8_t len[BZ_MAX_ALPHA_SIZE];
} huffman_table_t;

/* BZip2 context */
struct osrs_bzip2_ctx {
  bit_reader_t br;
  huffman_table_t tables[BZ_MAX_GROUPS];
  int n_groups;
  int group_pos;
  int group_no;
  uint8_t selectors[18002]; /* Max possible selectors */
  int n_selectors;
  uint8_t mtf_value[256];
  uint8_t block[BZ_MAX_BLOCK_SIZE];
  int block_size;
  uint32_t block_crc;
  uint32_t combined_crc;
  int orig_ptr;
  int n_in_use;
  bool in_use[256];
  uint8_t seq_to_unseq[256];
  uint32_t tt[BZ_MAX_BLOCK_SIZE]; /* For inverse BWT */
};

/* Initialize bit reader */
static void br_init(bit_reader_t *br, const uint8_t *data, size_t len) {
  br->data = data;
  br->len = len;
  br->byte_pos = 0;
  br->bit_pos = 0;
}

/* Read n bits (1-32), MSB-first */
static uint32_t br_read_bits(bit_reader_t *br, int n) {
  uint32_t result = 0;
  for (int i = 0; i < n; i++) {
    if (br->byte_pos >= br->len) {
      result <<= 1;
      continue;
    }
    uint8_t byte = br->data[br->byte_pos];
    int bit = (byte >> (7 - br->bit_pos)) & 1;
    result = (result << 1) | (uint32_t)bit;
    br->bit_pos++;
    if (br->bit_pos == 8) {
      br->bit_pos = 0;
      br->byte_pos++;
    }
  }
  return result;
}

/* Read single bit */
static int br_read_bit(bit_reader_t *br) { return (int)br_read_bits(br, 1); }

/* Create Huffman table from code lengths (libbzip2 reference algorithm) */
static void huffman_create_table(huffman_table_t *t, const uint8_t *lengths,
                                 int alpha_size) {
  int min_len = 32;
  int max_len = 0;

  for (int i = 0; i < alpha_size; i++) {
    t->len[i] = lengths[i];
    if (lengths[i] < min_len)
      min_len = lengths[i];
    if (lengths[i] > max_len)
      max_len = lengths[i];
  }
  t->min_len = min_len;
  t->max_len = max_len;

  /* Build perm: sort by length, then by symbol */
  int pp = 0;
  for (int i = min_len; i <= max_len; i++) {
    for (int j = 0; j < alpha_size; j++) {
      if (lengths[j] == i) {
        t->perm[pp] = j;
        pp++;
      }
    }
  }

  /* Build base and limit arrays */
  for (int i = 0; i < BZ_MAX_CODE_LEN + 1; i++)
    t->base[i] = 0;
  for (int i = 0; i < alpha_size; i++)
    t->base[lengths[i] + 1]++;

  for (int i = 1; i < BZ_MAX_CODE_LEN + 1; i++)
    t->base[i] += t->base[i - 1];

  for (int i = 0; i < BZ_MAX_CODE_LEN + 1; i++)
    t->limit[i] = 0;
  int vec = 0;

  for (int i = min_len; i <= max_len; i++) {
    vec += (t->base[i + 1] - t->base[i]);
    t->limit[i] = vec - 1;
    vec <<= 1;
  }
  for (int i = min_len + 1; i <= max_len; i++)
    t->base[i] = ((t->limit[i - 1] + 1) << 1) - t->base[i];
}

/* Decode one Huffman symbol */
static int huffman_decode_symbol(osrs_bzip2_t *ctx, huffman_table_t *t) {
  int zn = t->min_len;
  int zvec = (int)br_read_bits(&ctx->br, zn);

  while (zvec > t->limit[zn]) {
    zn++;
    zvec = (zvec << 1) | br_read_bit(&ctx->br);
  }

  if (zn > t->max_len)
    return -1;
  int sym = t->perm[zvec - t->base[zn]];
  return sym;
}

/* Get MTF value */
static int get_mtf_val(osrs_bzip2_t *ctx) {
  if (ctx->group_pos == 0) {
    ctx->group_no++;
    if (ctx->group_no >= ctx->n_selectors)
      return -1;
    ctx->group_pos = BZ_GROUP_SIZE;
  }
  ctx->group_pos--;
  return huffman_decode_symbol(ctx,
                               &ctx->tables[ctx->selectors[ctx->group_no]]);
}

/* Inverse Burrows-Wheeler transform */
static void inverse_bwt(osrs_bzip2_t *ctx, uint8_t *output,
                        size_t *output_len) {
  int n = ctx->block_size;

  /* Count occurrences of each byte */
  uint32_t count[256] = {0};
  for (int i = 0; i < n; i++) {
    count[ctx->block[i]]++;
  }

  /* Calculate starting position of each byte in sorted order */
  uint32_t pos[256];
  uint32_t cum = 0;
  for (int i = 0; i < 256; i++) {
    pos[i] = cum;
    cum += count[i];
  }

  /* Build TT array: TT[i] = position in BWT of the byte that follows position i
   * in original */
  for (int i = 0; i < n; i++) {
    uint8_t ch = ctx->block[i];
    ctx->tt[pos[ch]] = (uint32_t)i;
    pos[ch]++;
  }

  /* Reconstruct original data */
  int t_pos = ctx->orig_ptr;
  for (int i = 0; i < n; i++) {
    t_pos = ctx->tt[t_pos];
    output[i] = ctx->block[t_pos];
  }
  *output_len = (size_t)n;
}

/* Expand RLE-preprocessed block data.
 * In bzip2, runs of 4+ identical bytes are encoded during compression as:
 *   4 copies of the byte, followed by (run_length - 4)
 * This function expands them back to the original run lengths.
 */
static bool expand_rle(const uint8_t *input, size_t n, uint8_t *output,
                       size_t *output_len, size_t max_len) {
  size_t out_pos = 0;
  size_t i = 0;

  while (i < n) {
    uint8_t ch = input[i++];
    int run_len = 1;

    /* Count consecutive identical bytes (up to 4) */
    while (i < n && input[i] == ch && run_len < 4) {
      run_len++;
      i++;
    }

    /* If we saw 4 identical bytes, the next byte is run_length - 4 */
    if (run_len == 4 && i < n) {
      run_len = input[i] + 4;
      i++;
    }

    if (out_pos + (size_t)run_len > max_len)
      return false;

    for (int j = 0; j < run_len; j++) {
      output[out_pos++] = ch;
    }
  }

  *output_len = out_pos;
  return true;
}

/* Read block header and decode */
static bool decode_block(osrs_bzip2_t *ctx) {
  /* Read block CRC */
  ctx->block_crc = br_read_bits(&ctx->br, 32);

  /* Read randomized flag (should be 0 for modern bzip2) */
  int randomized = br_read_bit(&ctx->br);
  if (randomized)
    return false;

  /* Read original pointer */
  ctx->orig_ptr = (int)br_read_bits(&ctx->br, 24);
  if (ctx->orig_ptr < 0 || ctx->orig_ptr >= BZ_MAX_BLOCK_SIZE)
    return false;

  /* Read mapping table */
  ctx->n_in_use = 0;
  int in_use_16 = (int)br_read_bits(&ctx->br, 16);
  for (int i = 0; i < 16; i++) {
    if (in_use_16 & (1 << (15 - i))) {
      int in_use = (int)br_read_bits(&ctx->br, 16);
      for (int j = 0; j < 16; j++) {
        if (in_use & (1 << (15 - j))) {
          ctx->seq_to_unseq[ctx->n_in_use++] = (uint8_t)(i * 16 + j);
        }
      }
    }
  }
  if (ctx->n_in_use == 0)
    return false;

  /* Read number of Huffman groups */
  ctx->n_groups = (int)br_read_bits(&ctx->br, 3);
  if (ctx->n_groups < 2 || ctx->n_groups > BZ_MAX_GROUPS)
    return false;

  /* Read selectors */
  ctx->n_selectors = (int)br_read_bits(&ctx->br, 15);
  if (ctx->n_selectors < 1 || ctx->n_selectors > 18002)
    return false;

  for (int i = 0; i < ctx->n_selectors; i++) {
    int j = 0;
    while (br_read_bit(&ctx->br)) {
      j++;
      if (j >= ctx->n_groups)
        return false;
    }
    ctx->selectors[i] = (uint8_t)j;
  }

  /* Undo MTF on selectors */
  for (int i = 0; i < ctx->n_groups; i++)
    ctx->mtf_value[i] = (uint8_t)i;
  for (int i = 0; i < ctx->n_selectors; i++) {
    int v = ctx->selectors[i];
    uint8_t tmp = ctx->mtf_value[v];
    while (v > 0) {
      ctx->mtf_value[v] = ctx->mtf_value[v - 1];
      v--;
    }
    ctx->mtf_value[0] = tmp;
    ctx->selectors[i] = tmp;
  }

  /* Read Huffman tables */
  int alpha_size = ctx->n_in_use + 2;
  for (int t = 0; t < ctx->n_groups; t++) {
    uint8_t lengths[BZ_MAX_ALPHA_SIZE];
    int curr_len = (int)br_read_bits(&ctx->br, 5);
    for (int i = 0; i < alpha_size; i++) {
      while (1) {
        if (curr_len < 1 || curr_len > 20)
          return false;
        if (!br_read_bit(&ctx->br))
          break;
        if (!br_read_bit(&ctx->br))
          curr_len++;
        else
          curr_len--;
      }
      lengths[i] = (uint8_t)curr_len;
    }
    huffman_create_table(&ctx->tables[t], lengths, alpha_size);
  }

  /* Decode data */
  ctx->group_pos = 0;
  ctx->group_no = -1;
  ctx->block_size = 0;

  /* Initialize MTF */
  for (int i = 0; i < 256; i++)
    ctx->mtf_value[i] = (uint8_t)i;

  int eob = ctx->n_in_use + 1;
  int nblock = 0;

  int next_sym = get_mtf_val(ctx);
  if (next_sym < 0)
    return false;

  while (1) {
    if (next_sym == eob)
      break;

    if (next_sym == 0 || next_sym == 1) {
      /* RLE: run of identical bytes */
      int es = -1;
      int N = 1;
      do {
        if (N >= 2 * 1024 * 1024)
          return false;
        if (next_sym == 0)
          es += N;
        else
          es += 2 * N;
        N <<= 1;
        next_sym = get_mtf_val(ctx);
        if (next_sym < 0)
          return false;
      } while (next_sym == 0 || next_sym == 1);
      es++; /* run length */

      uint8_t ch = ctx->seq_to_unseq[ctx->mtf_value[0]];
      for (int i = 0; i < es; i++) {
        if (nblock >= BZ_MAX_BLOCK_SIZE)
          return false;
        ctx->block[nblock++] = ch;
      }
      continue; /* next_sym is the terminating symbol */
    }

    /* Regular symbol */
    next_sym--;
    uint8_t mtf_idx = ctx->mtf_value[next_sym];
    uint8_t ch = ctx->seq_to_unseq[mtf_idx];

    /* Move-to-front */
    uint8_t tmp = ctx->mtf_value[next_sym];
    for (int i = next_sym; i > 0; i--) {
      ctx->mtf_value[i] = ctx->mtf_value[i - 1];
    }
    ctx->mtf_value[0] = tmp;

    if (nblock >= BZ_MAX_BLOCK_SIZE)
      return false;
    ctx->block[nblock++] = ch;

    next_sym = get_mtf_val(ctx);
    if (next_sym < 0)
      return false;
  }

  ctx->block_size = nblock;
  return true;
}

osrs_bzip2_t *osrs_bzip2_create(void) {
  osrs_bzip2_t *ctx = calloc(1, sizeof(osrs_bzip2_t));
  return ctx;
}

void osrs_bzip2_destroy(osrs_bzip2_t *ctx) { free(ctx); }

bool osrs_bzip2_decompress(osrs_bzip2_t *ctx, const uint8_t *compressed,
                           size_t compressed_len, uint8_t *output,
                           size_t *output_len) {
  if (!ctx || !compressed || !output || !output_len)
    return false;
  if (compressed_len < 4)
    return false;

  /* Check header magic "BZh" */
  if (compressed[0] != 'B' || compressed[1] != 'Z' || compressed[2] != 'h')
    return false;

  /* Check block size digit (1-9) */
  if (compressed[3] < '1' || compressed[3] > '9')
    return false;

  /* Initialize bit reader (skip 4-byte header) */
  br_init(&ctx->br, compressed + 4, compressed_len - 4);
  ctx->combined_crc = 0;

  size_t out_pos = 0;
  size_t out_cap = *output_len;

  while (1) {
    /* Read block header magic (24 bits) */
    uint32_t magic = br_read_bits(&ctx->br, 24);

    if (magic == 0x314159) { /* Block magic: 0x314159265359 */
      magic = br_read_bits(&ctx->br, 24);
      if (magic != 0x265359)
        return false;

      if (!decode_block(ctx))
        return false;

      /* Update combined CRC */
      ctx->combined_crc = (ctx->combined_crc << 1) | (ctx->combined_crc >> 31);
      ctx->combined_crc ^= ctx->block_crc;

      /* Inverse BWT */
      uint8_t block_out[BZ_MAX_BLOCK_SIZE];
      size_t block_len;
      inverse_bwt(ctx, block_out, &block_len);

      /* Expand output RLE */
      size_t expanded_len;
      if (!expand_rle(block_out, block_len, output + out_pos, &expanded_len,
                      out_cap - out_pos))
        return false;
      out_pos += expanded_len;

    } else if (magic == 0x177245) { /* End of stream: 0x177245385090 */
      magic = br_read_bits(&ctx->br, 24);
      if (magic != 0x385090)
        return false;

      uint32_t stored_crc = br_read_bits(&ctx->br, 32);
      if (stored_crc != ctx->combined_crc)
        return false;
      break;
    } else {
      return false;
    }
  }

  *output_len = out_pos;
  return true;
}

bool osrs_bzip2_decompress_oneshot(const uint8_t *compressed,
                                   size_t compressed_len, uint8_t *output,
                                   size_t *output_len) {
  osrs_bzip2_t *ctx = osrs_bzip2_create();
  if (!ctx)
    return false;
  bool result = osrs_bzip2_decompress(ctx, compressed, compressed_len, output,
                                      output_len);
  osrs_bzip2_destroy(ctx);
  return result;
}

size_t osrs_bzip2_decompressed_size(const uint8_t *compressed,
                                    size_t compressed_len) {
  (void)compressed;
  (void)compressed_len;
  /* BZip2 doesn't store decompressed size in header — must decompress to find
   * out */
  /* OSRS cache prepends decompressed size before bzip2 stream */
  return 0;
}
