/*
 * ORDL GovCon - SHA-256 Implementation
 * FIPS 180-4 compliant
 */

#include "core/compat.h"
#include "security/sha256.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* SHA-256 core                                                               */
/* -------------------------------------------------------------------------- */

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(gc_sha256_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, m[64];

    for (i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) |
               ((uint32_t)data[j+2] << 8) | ((uint32_t)data[j+3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t t1, t2;
        t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void gc_sha256_init(gc_sha256_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void gc_sha256_update(gc_sha256_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bit_count += 512;
            ctx->buflen = 0;
        }
    }
}

void gc_sha256_final(gc_sha256_t *ctx, uint8_t digest[32]) {
    size_t i = ctx->buflen;
    ctx->buffer[i++] = 0x80;
    if (ctx->buflen >= 56) {
        while (i < 64) ctx->buffer[i++] = 0;
        sha256_transform(ctx, ctx->buffer);
        i = 0;
    }
    while (i < 56) ctx->buffer[i++] = 0;

    ctx->bit_count += ctx->buflen * 8;
    ctx->buffer[63] = (uint8_t)(ctx->bit_count);
    ctx->buffer[62] = (uint8_t)(ctx->bit_count >> 8);
    ctx->buffer[61] = (uint8_t)(ctx->bit_count >> 16);
    ctx->buffer[60] = (uint8_t)(ctx->bit_count >> 24);
    ctx->buffer[59] = (uint8_t)(ctx->bit_count >> 32);
    ctx->buffer[58] = (uint8_t)(ctx->bit_count >> 40);
    ctx->buffer[57] = (uint8_t)(ctx->bit_count >> 48);
    ctx->buffer[56] = (uint8_t)(ctx->bit_count >> 56);
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 4; i++) {
        digest[i]      = (uint8_t)(ctx->state[0] >> (24 - i * 8));
        digest[i + 4]  = (uint8_t)(ctx->state[1] >> (24 - i * 8));
        digest[i + 8]  = (uint8_t)(ctx->state[2] >> (24 - i * 8));
        digest[i + 12] = (uint8_t)(ctx->state[3] >> (24 - i * 8));
        digest[i + 16] = (uint8_t)(ctx->state[4] >> (24 - i * 8));
        digest[i + 20] = (uint8_t)(ctx->state[5] >> (24 - i * 8));
        digest[i + 24] = (uint8_t)(ctx->state[6] >> (24 - i * 8));
        digest[i + 28] = (uint8_t)(ctx->state[7] >> (24 - i * 8));
    }
}

void gc_sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    gc_sha256_t ctx;
    gc_sha256_init(&ctx);
    gc_sha256_update(&ctx, data, len);
    gc_sha256_final(&ctx, digest);
}

/* ==========================================================================
 * SHA-384 / SHA-512 (FIPS 180-4)
 * ========================================================================== */

#define ROTRIGHT64(a,b) (((a) >> (b)) | ((a) << (64-(b))))

#define CH64(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ64(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0_64(x) (ROTRIGHT64(x,28) ^ ROTRIGHT64(x,34) ^ ROTRIGHT64(x,39))
#define EP1_64(x) (ROTRIGHT64(x,14) ^ ROTRIGHT64(x,18) ^ ROTRIGHT64(x,41))
#define SIG0_64(x) (ROTRIGHT64(x,1) ^ ROTRIGHT64(x,8) ^ ((x) >> 7))
#define SIG1_64(x) (ROTRIGHT64(x,19) ^ ROTRIGHT64(x,61) ^ ((x) >> 6))

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2d43210cULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static void sha512_transform(gc_sha384_t *ctx, const uint8_t data[128]) {
    uint64_t a, b, c, d, e, f, g, h, i, j, m[80];

    for (i = 0, j = 0; i < 16; i++, j += 8) {
        m[i] = ((uint64_t)data[j] << 56) | ((uint64_t)data[j+1] << 48) |
               ((uint64_t)data[j+2] << 40) | ((uint64_t)data[j+3] << 32) |
               ((uint64_t)data[j+4] << 24) | ((uint64_t)data[j+5] << 16) |
               ((uint64_t)data[j+6] << 8)  | ((uint64_t)data[j+7]);
    }
    for (; i < 80; i++) {
        m[i] = SIG1_64(m[i-2]) + m[i-7] + SIG0_64(m[i-15]) + m[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 80; i++) {
        uint64_t t1;
        uint64_t t2;
        t1 = h + EP1_64(e) + CH64(e,f,g) + sha512_k[i] + m[i];
        t2 = EP0_64(a) + MAJ64(a,b,c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void gc_sha384_init(gc_sha384_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
}

void gc_sha384_update(gc_sha384_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 128) {
            sha512_transform(ctx, ctx->buffer);
            ctx->bit_count += 1024;
            ctx->buflen = 0;
        }
    }
}

void gc_sha384_final(gc_sha384_t *ctx, uint8_t digest[48]) {
    size_t i = ctx->buflen;
    ctx->buffer[i++] = 0x80;
    if (ctx->buflen >= 112) {
        while (i < 128) ctx->buffer[i++] = 0;
        sha512_transform(ctx, ctx->buffer);
        i = 0;
    }
    while (i < 112) ctx->buffer[i++] = 0;

    ctx->bit_count += ctx->buflen * 8;
    ctx->buffer[127] = (uint8_t)(ctx->bit_count);
    ctx->buffer[126] = (uint8_t)(ctx->bit_count >> 8);
    ctx->buffer[125] = (uint8_t)(ctx->bit_count >> 16);
    ctx->buffer[124] = (uint8_t)(ctx->bit_count >> 24);
    ctx->buffer[123] = (uint8_t)(ctx->bit_count >> 32);
    ctx->buffer[122] = (uint8_t)(ctx->bit_count >> 40);
    ctx->buffer[121] = (uint8_t)(ctx->bit_count >> 48);
    ctx->buffer[120] = (uint8_t)(ctx->bit_count >> 56);
    sha512_transform(ctx, ctx->buffer);

    for (i = 0; i < 6; i++) {
        digest[i*8+0] = (uint8_t)(ctx->state[i] >> 56);
        digest[i*8+1] = (uint8_t)(ctx->state[i] >> 48);
        digest[i*8+2] = (uint8_t)(ctx->state[i] >> 40);
        digest[i*8+3] = (uint8_t)(ctx->state[i] >> 32);
        digest[i*8+4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*8+5] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*8+6] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*8+7] = (uint8_t)(ctx->state[i]);
    }
}

void gc_sha384(const uint8_t *data, size_t len, uint8_t digest[48]) {
    gc_sha384_t ctx;
    gc_sha384_init(&ctx);
    gc_sha384_update(&ctx, data, len);
    gc_sha384_final(&ctx, digest);
}

/* ==========================================================================
 * SHA-1 (RFC 3174) - Required for WebSocket handshake RFC 6455
 * ========================================================================== */

#define SHA1_ROTL32(x,n) (((x) << (n)) | ((x) >> (32-(n))))

static void sha1_transform(gc_sha1_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, k;
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i*4] << 24) | ((uint32_t)data[i*4+1] << 16) |
               ((uint32_t)data[i*4+2] << 8) | ((uint32_t)data[i*4+3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROTL32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4];
    for (int i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp;
        temp = SHA1_ROTL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = SHA1_ROTL32(b, 30); b = a; a = temp;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e;
}

void gc_sha1_init(gc_sha1_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

void gc_sha1_update(gc_sha1_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha1_transform(ctx, ctx->buffer);
            ctx->bit_count += 512;
            ctx->buflen = 0;
        }
    }
}

void gc_sha1_final(gc_sha1_t *ctx, uint8_t digest[20]) {
    size_t i = ctx->buflen;
    ctx->buffer[i++] = 0x80;
    if (ctx->buflen >= 56) {
        while (i < 64) ctx->buffer[i++] = 0;
        sha1_transform(ctx, ctx->buffer);
        i = 0;
    }
    while (i < 56) ctx->buffer[i++] = 0;
    ctx->bit_count += ctx->buflen * 8;
    ctx->buffer[63] = (uint8_t)(ctx->bit_count);
    ctx->buffer[62] = (uint8_t)(ctx->bit_count >> 8);
    ctx->buffer[61] = (uint8_t)(ctx->bit_count >> 16);
    ctx->buffer[60] = (uint8_t)(ctx->bit_count >> 24);
    ctx->buffer[59] = (uint8_t)(ctx->bit_count >> 32);
    ctx->buffer[58] = (uint8_t)(ctx->bit_count >> 40);
    ctx->buffer[57] = (uint8_t)(ctx->bit_count >> 48);
    ctx->buffer[56] = (uint8_t)(ctx->bit_count >> 56);
    sha1_transform(ctx, ctx->buffer);
    for (i = 0; i < 4; i++) {
        digest[i]      = (uint8_t)(ctx->state[0] >> (24 - i * 8));
        digest[i + 4]  = (uint8_t)(ctx->state[1] >> (24 - i * 8));
        digest[i + 8]  = (uint8_t)(ctx->state[2] >> (24 - i * 8));
        digest[i + 12] = (uint8_t)(ctx->state[3] >> (24 - i * 8));
        digest[i + 16] = (uint8_t)(ctx->state[4] >> (24 - i * 8));
    }
}

void gc_sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    gc_sha1_t ctx;
    gc_sha1_init(&ctx);
    gc_sha1_update(&ctx, data, len);
    gc_sha1_final(&ctx, digest);
}

/* HMAC-SHA384 */
void gc_hmac_sha384(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t mac[48]) {
    uint8_t k[128] = {0};
    if (key_len <= 128) {
        memcpy(k, key, key_len);
    } else {
        gc_sha384(key, key_len, k);
    }

    uint8_t opad[128], ipad[128];
    for (int i = 0; i < 128; i++) {
        opad[i] = k[i] ^ 0x5c;
        ipad[i] = k[i] ^ 0x36;
    }

    gc_sha384_t ctx;
    gc_sha384_init(&ctx);
    gc_sha384_update(&ctx, ipad, 128);
    gc_sha384_update(&ctx, msg, msg_len);
    uint8_t inner[48];
    gc_sha384_final(&ctx, inner);

    gc_sha384_init(&ctx);
    gc_sha384_update(&ctx, opad, 128);
    gc_sha384_update(&ctx, inner, 48);
    gc_sha384_final(&ctx, mac);
}

/* HKDF-SHA384 */
void gc_hkdf_sha384_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[48]) {
    gc_hmac_sha384(salt ? salt : (const uint8_t *)"", salt_len,
                   ikm, ikm_len, prk);
}

void gc_hkdf_sha384_expand(const uint8_t prk[48],
                           const uint8_t *info, size_t info_len,
                           uint8_t *okm, size_t okm_len) {
    uint8_t t[48] = {0};
    size_t done = 0;
    uint8_t counter = 1;
    while (done < okm_len) {
        uint8_t msg[48 + 256 + 1];
        size_t msg_len = 0;
        if (done > 0) {
            memcpy(msg + msg_len, t, 48);
            msg_len += 48;
        }
        if (info_len > 0 && info != nullptr) {
            memcpy(msg + msg_len, info, info_len);
            msg_len += info_len;
        }
        msg[msg_len++] = counter;
        gc_hmac_sha384(prk, 48, msg, msg_len, t);
        size_t copy = okm_len - done < 48 ? okm_len - done : 48;
        memcpy(okm + done, t, copy);
        done += copy;
        counter++;
    }
}

/* ==========================================================================
 * HMAC-SHA256 + HKDF-SHA256
 * ========================================================================== */

void gc_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t mac[32]) {
    uint8_t k[64] = {0};
    if (key_len <= 64) {
        memcpy(k, key, key_len);
    } else {
        gc_sha256(key, key_len, k);
    }

    uint8_t opad[64], ipad[64];
    for (int i = 0; i < 64; i++) {
        opad[i] = k[i] ^ 0x5c;
        ipad[i] = k[i] ^ 0x36;
    }

    gc_sha256_t ctx;
    gc_sha256_init(&ctx);
    gc_sha256_update(&ctx, ipad, 64);
    gc_sha256_update(&ctx, msg, msg_len);
    uint8_t inner[32];
    gc_sha256_final(&ctx, inner);

    gc_sha256_init(&ctx);
    gc_sha256_update(&ctx, opad, 64);
    gc_sha256_update(&ctx, inner, 32);
    gc_sha256_final(&ctx, mac);
}

/* -------------------------------------------------------------------------- */
/* HKDF-SHA256 (RFC 5869)                                                     */
/* -------------------------------------------------------------------------- */

void gc_hkdf_sha256_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[32]) {
    gc_hmac_sha256(salt ? salt : (const uint8_t *)"", salt_len,
                   ikm, ikm_len, prk);
}

void gc_hkdf_sha256_expand(const uint8_t prk[32],
                           const uint8_t *info, size_t info_len,
                           uint8_t *okm, size_t okm_len) {
    uint8_t t[32] = {0};
    size_t done = 0;
    uint8_t counter = 1;
    while (done < okm_len) {
        uint8_t msg[32 + 256 + 1];
        size_t msg_len = 0;
        if (done > 0) {
            memcpy(msg + msg_len, t, 32);
            msg_len += 32;
        }
        if (info_len > 0 && info != nullptr) {
            memcpy(msg + msg_len, info, info_len);
            msg_len += info_len;
        }
        msg[msg_len++] = counter;
        gc_hmac_sha256(prk, 32, msg, msg_len, t);
        size_t copy = okm_len - done < 32 ? okm_len - done : 32;
        memcpy(okm + done, t, copy);
        done += copy;
        counter++;
    }
}
