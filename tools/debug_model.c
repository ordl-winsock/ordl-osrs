/* tools/debug_model.c - dump Type3 model footer + section offsets */
#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

static uint16_t ru16(const uint8_t *d, size_t pos) {
  return (uint16_t)((d[pos] << 8) | d[pos + 1]);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <cache_dir> <model_id>\n", argv[0]);
    return 1;
  }
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  int id = atoi(argv[2]);
  size_t len = 0;
  uint8_t *data = osrs_cache_read_archive(cache, 7, id, &len);
  if (!data) {
    printf("archive %d not found\n", id);
    return 1;
  }
  osrs_container_t *c = osrs_container_decompress(data, len, NULL);
  free(data);
  if (!c) {
    printf("decompress fail\n");
    return 1;
  }
  const uint8_t *d = c->data;
  size_t n = c->data_len;
  printf("model %d: decompressed len=%zu, trailer bytes: %02x %02x\n", id, n,
         d[n - 2], d[n - 1]);

  size_t f = n - 26;
  int vc = ru16(d, f), fc = ru16(d, f + 2), tc = d[f + 4];
  int v12 = d[f + 5], v13 = d[f + 6], v14 = d[f + 7], v15 = d[f + 8],
      v16 = d[f + 9], v17 = d[f + 10], v18 = d[f + 11];
  int lenX = ru16(d, f + 12), lenY = ru16(d, f + 14), lenZ = ru16(d, f + 16);
  int lenFI = ru16(d, f + 18), lenTC = ru16(d, f + 20), lenVG = ru16(d, f + 22);
  printf("vc=%d fc=%d tc=%d | rt=%d prio=%d alpha=%d bones=%d tex=%d vbones=%d "
         "animaya=%d\n",
         vc, fc, tc, v12, v13, v14, v15, v16, v17, v18);
  printf(
      "lenX=%d lenY=%d lenZ=%d lenFaceIdx=%d lenTexCoords=%d lenVGroups=%d\n",
      lenX, lenY, lenZ, lenFI, lenTC, lenVG);

  /* Compute section end per our map */
  int off = tc;
  off += vc; /* vertex flags */
  if (v12 == 1)
    off += fc; /* render types */
  off += fc;   /* idx mode */
  if (v13 == 255)
    off += fc; /* priorities */
  if (v15 == 1)
    off += fc;  /* transparency groups */
  off += lenVG; /* vertex groups */
  if (v14 == 1)
    off += fc;  /* transparencies */
  off += lenFI; /* idx deltas */
  if (v16 == 1)
    off += fc * 2;           /* face textures */
  off += lenTC;              /* texture coords */
  off += fc * 2;             /* face colors */
  off += lenX + lenY + lenZ; /* vertices */

  /* count type0 texfaces */
  int t0 = 0;
  for (int i = 0; i < tc; i++)
    if (d[i] == 0)
      t0++;
  off += t0 * 6;
  printf("computed end-of-sections: %d (len-26 = %zu, slack = %zd)\n", off,
         n - 26, (ptrdiff_t)(n - 26) - off);

  osrs_container_free(c);
  osrs_cache_close(cache);
  return 0;
}
