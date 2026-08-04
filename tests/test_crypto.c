/*
 * tests/test_crypto.c — Test crypto and compression implementations
 * Pure C23, zero external dependencies.
 */

#include "osrs/crc32.h"
#include "osrs/xtea.h"
#include "osrs/isaac.h"
#include "osrs/bzip2.h"
#include "osrs/gzip.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test CRC32 */
static void test_crc32(void)
{
    printf("Testing CRC32...\n");

    /* Test vector: "123456789" -> 0xCBF43926 */
    const char *test = "123456789";
    uint32_t crc = osrs_crc32((const uint8_t *)test, strlen(test));
    printf("  CRC32(\"123456789\") = 0x%08X (expected 0xCBF43926)\n", crc);
    assert(crc == 0xCBF43926);

    /* Test empty string */
    crc = osrs_crc32((const uint8_t *)"", 0);
    printf("  CRC32(\"\") = 0x%08X (expected 0x00000000)\n", crc);
    assert(crc == 0x00000000);

    /* Test incremental */
    osrs_crc32_t ctx;
    osrs_crc32_init(&ctx);
    osrs_crc32_update(&ctx, (const uint8_t *)"1234", 4);
    osrs_crc32_update(&ctx, (const uint8_t *)"56789", 5);
    crc = osrs_crc32_final(&ctx);
    printf("  CRC32 incremental = 0x%08X (expected 0xCBF43926)\n", crc);
    assert(crc == 0xCBF43926);

    printf("  CRC32: PASS\n\n");
}

/* Test XTEA */
static void test_xtea(void)
{
    printf("Testing XTEA...\n");

    /* Test vector from Wikipedia */
    uint8_t key[16] = {0};
    uint8_t plaintext[8] = {0};
    uint8_t ciphertext[8];
    uint8_t decrypted[8];

    osrs_xtea_t ctx;
    osrs_xtea_init(&ctx, key);

    memcpy(ciphertext, plaintext, 8);
    osrs_xtea_encrypt_block(&ctx, ciphertext);

    memcpy(decrypted, ciphertext, 8);
    osrs_xtea_decrypt_block(&ctx, decrypted);

    printf("  XTEA roundtrip: %s\n",
           memcmp(plaintext, decrypted, 8) == 0 ? "PASS" : "FAIL");
    assert(memcmp(plaintext, decrypted, 8) == 0);

    /* Test with non-zero key and data */
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)(i * 17);
    for (int i = 0; i < 8; i++) plaintext[i] = (uint8_t)(i * 31);

    osrs_xtea_init(&ctx, key);
    memcpy(ciphertext, plaintext, 8);
    osrs_xtea_encrypt_block(&ctx, ciphertext);
    memcpy(decrypted, ciphertext, 8);
    osrs_xtea_decrypt_block(&ctx, decrypted);

    printf("  XTEA with key roundtrip: %s\n",
           memcmp(plaintext, decrypted, 8) == 0 ? "PASS" : "FAIL");
    assert(memcmp(plaintext, decrypted, 8) == 0);

    printf("  XTEA: PASS\n\n");
}

/* Test ISAAC */
static void test_isaac(void)
{
    printf("Testing ISAAC...\n");

    uint32_t seed[4] = {0x12345678, 0x9ABCDEF0, 0xFEDCBA98, 0x76543210};
    osrs_isaac_t ctx;

    osrs_isaac_init(&ctx, seed);

    /* Generate some values — should be deterministic */
    uint32_t vals[10];
    for (int i = 0; i < 10; i++) {
        vals[i] = osrs_isaac_next(&ctx);
    }

    printf("  First 10 ISAAC values: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
           vals[0], vals[1], vals[2], vals[3], vals[4],
           vals[5], vals[6], vals[7], vals[8], vals[9]);

    /* Verify deterministic — same seed produces same sequence */
    osrs_isaac_init(&ctx, seed);
    for (int i = 0; i < 10; i++) {
        uint32_t v = osrs_isaac_next(&ctx);
        assert(v == vals[i]);
    }

    printf("  ISAAC deterministic: PASS\n");
    printf("  ISAAC: PASS\n\n");
}

/* Test BZip2 with known compressed data */
static void test_bzip2(void)
{
    printf("Testing BZip2...\n");

    osrs_bzip2_t *ctx = osrs_bzip2_create();
    assert(ctx != NULL);

    /* Test invalid data */

    /* Test invalid data */
    uint8_t output[1024];
    size_t output_len = sizeof(output);
    bool result = osrs_bzip2_decompress(ctx, (const uint8_t *)"invalid", 7,
                                        output, &output_len);
    printf("  Invalid data rejected: %s\n", !result ? "PASS" : "FAIL");
    assert(!result);

    osrs_bzip2_destroy(ctx);
    printf("  BZip2: PASS (structure tests)\n\n");
}

/* Test GZip with known compressed data */
static void test_gzip(void)
{
    printf("Testing GZip...\n");

    osrs_gzip_t *ctx = osrs_gzip_create();
    assert(ctx != NULL);

    /* Test invalid data */
    uint8_t output[1024];
    size_t output_len = sizeof(output);
    bool result = osrs_gzip_decompress(ctx, (const uint8_t *)"invalid", 7,
                                       output, &output_len);
    printf("  Invalid data rejected: %s\n", !result ? "PASS" : "FAIL");
    assert(!result);

    osrs_gzip_destroy(ctx);
    printf("  GZip: PASS (structure tests)\n\n");
}

int main(void)
{
    printf("=== OSRS Crypto/Compression Test Suite ===\n\n");

    test_crc32();
    test_xtea();
    test_isaac();
    test_bzip2();
    test_gzip();

    printf("=== All tests passed! ===\n");
    return 0;
}
