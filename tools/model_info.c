#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <cache_dir> [model_id]\n", argv[0]);
    return 1;
  }
  int model_id = argc > 2 ? atoi(argv[2]) : 0;
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache) {
    fprintf(stderr, "cache open failed\n");
    return 1;
  }
  if (!osrs_cache_load_indexes(cache)) {
    fprintf(stderr, "index load failed\n");
    return 1;
  }

  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cache, 7, model_id, NULL);
  if (!files || files->count < 1) {
    fprintf(stderr, "model %d: no archive\n", model_id);
    return 1;
  }

  osrs_model_t *m =
      osrs_model_load(model_id, files->files[0].data, files->files[0].len);
  if (!m) {
    fprintf(stderr, "model %d: decode failed\n", model_id);
    return 1;
  }

  printf("model %d: verts=%d faces=%d\n", model_id, m->vertex_count,
         m->face_count);
  printf("  has_face_color=%d has_face_texture=%d has_uv=%d\n",
         m->face_color != NULL, m->face_texture != NULL, m->face_u != NULL);

  if (m->face_color) {
    int dark = 0, bright = 0, zero = 0;
    for (int i = 0; i < m->face_count; i++) {
      int lum = m->face_color[i] & 127;
      if (lum <= 5)
        zero++;
      else if (lum < 30)
        dark++;
      else
        bright++;
    }
    printf("  face_color luminance: zero=%d dark=%d bright=%d\n", zero, dark,
           bright);
  }

  if (m->face_texture) {
    int textured = 0;
    for (int i = 0; i < m->face_count; i++)
      if (m->face_texture[i] >= 0)
        textured++;
    printf("  textured faces: %d / %d\n", textured, m->face_count);
  }

  osrs_model_free(m);
  osrs_archive_files_free(files);
  osrs_cache_close(cache);
  return 0;
}
