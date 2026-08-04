#include "osrs/isaac.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  uint32_t seed[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4 && i + 1 < argc; i++)
    seed[i] = (uint32_t)strtoul(argv[i + 1], NULL, 0);
  int n = argc > 5 ? atoi(argv[5]) : 10;
  osrs_isaac_t ctx;
  osrs_isaac_init(&ctx, seed);
  for (int i = 0; i < n; i++)
    printf("%08X%s", osrs_isaac_next(&ctx), i + 1 == n ? "\n" : " ");
  return 0;
}
