/*
 * tools/test_bzip2_real.c — Test BZip2 with real data
 */

#include "osrs/bzip2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.bz2>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc((size_t)len);
    fread(data, 1, (size_t)len, f);
    fclose(f);

    printf("Read %ld bytes from %s\n", len, argv[1]);
    printf("First 16 bytes: ");
    for (int i = 0; i < 16 && i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    /* Check magic */
    if (len >= 4 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') {
        printf("Valid BZip2 magic: BZh%c\n", data[3]);
    } else {
        printf("Invalid BZip2 magic\n");
        free(data);
        return 1;
    }

    /* Try to decompress */
    uint8_t output[1024 * 1024];
    size_t output_len = sizeof(output);

    osrs_bzip2_t *ctx = osrs_bzip2_create();
    bool ok = osrs_bzip2_decompress(ctx, data, (size_t)len, output, &output_len);
    osrs_bzip2_destroy(ctx);

    if (ok) {
        printf("Decompressed %zu bytes:\n", output_len);
        printf("%.*s\n", (int)output_len, output);
    } else {
        printf("Decompression failed\n");
    }

    free(data);
    return ok ? 0 : 1;
}
