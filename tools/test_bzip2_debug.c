/*
 * tools/test_bzip2_debug.c — Debug BZip2 decompression step by step
 */

#include "osrs/bzip2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal forward declaration of internal struct for debugging */
struct osrs_bzip2_ctx;

int main(int argc, char **argv) {
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

  printf("Read %ld bytes\n", len);
  printf("Magic: %c%c%c%c\n", data[0], data[1], data[2], data[3]);

  uint8_t output[1024 * 1024];
  size_t output_len = sizeof(output);

  osrs_bzip2_t *ctx = osrs_bzip2_create();
  bool ok = osrs_bzip2_decompress(ctx, data, (size_t)len, output, &output_len);

  if (ok) {
    printf("SUCCESS: Decompressed %zu bytes\n", output_len);
    printf("First 64 bytes: ");
    for (size_t i = 0; i < 64 && i < output_len; i++)
      printf("%02X ", output[i]);
    printf("\n");
  } else {
    printf("FAILED\n");
  }

  osrs_bzip2_destroy(ctx);
  free(data);
  return ok ? 0 : 1;
}
