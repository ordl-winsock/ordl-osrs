/* tools/test_models.c - validate the Type3 model decoder against live cache */
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/model.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <cache_dir> [model_id...]\n", argv[0]);
    return 1;
  }

  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache) {
    fprintf(stderr, "failed to open cache\n");
    return 1;
  }

  /* Default probe set: some common model ids + object models */
  int ids[16] = {0,    1,    2,     3,     4,     5,     100,   500,
                 1000, 5000, 10000, 20000, 30000, 40000, 50000, 60000};
  int nids = 16;
  if (argc > 2) {
    nids = 0;
    for (int i = 2; i < argc && nids < 16; i++)
      ids[nids++] = atoi(argv[i]);
  }

  int ok = 0, fail = 0;
  for (int i = 0; i < nids; i++) {
    int id = ids[i];
    size_t len = 0;
    uint8_t *data = osrs_cache_read_archive(cache, 7, id, &len);
    if (!data) {
      printf("model %5d: NOT IN CACHE\n", id);
      fail++;
      continue;
    }
    osrs_container_t *container = osrs_container_decompress(data, len, NULL);
    free(data);
    if (!container) {
      printf("model %5d: DECOMPRESS FAIL\n", id);
      fail++;
      continue;
    }
    osrs_model_t *m = osrs_model_load(id, container->data, container->data_len);
    osrs_container_free(container);
    if (!m) {
      printf("model %5d: DECODE FAIL\n", id);
      fail++;
      continue;
    }

    /* Validate: face indices within range, vertex bounds sane */
    int min_x = 1 << 30, max_x = -(1 << 30);
    int min_y = 1 << 30, max_y = -(1 << 30);
    int min_z = 1 << 30, max_z = -(1 << 30);
    for (int v = 0; v < m->vertex_count; v++) {
      if (m->vertex_x[v] < min_x)
        min_x = m->vertex_x[v];
      if (m->vertex_x[v] > max_x)
        max_x = m->vertex_x[v];
      if (m->vertex_y[v] < min_y)
        min_y = m->vertex_y[v];
      if (m->vertex_y[v] > max_y)
        max_y = m->vertex_y[v];
      if (m->vertex_z[v] < min_z)
        min_z = m->vertex_z[v];
      if (m->vertex_z[v] > max_z)
        max_z = m->vertex_z[v];
    }
    int bad_idx = 0;
    int first_bad = -1, last_bad = -1;
    for (int f = 0; f < m->face_count; f++) {
      if (m->face_a[f] >= m->vertex_count || m->face_b[f] >= m->vertex_count ||
          m->face_c[f] >= m->vertex_count) {
        bad_idx++;
        if (first_bad < 0)
          first_bad = f;
        last_bad = f;
      }
    }

    osrs_model_compute_normals(m);
    int has_rt = m->face_render_type != NULL;
    int has_tex = m->face_texture != NULL;
    int has_alpha = m->face_transparency != NULL;
    int tex_faces = 0;
    if (m->face_texture)
      for (int f = 0; f < m->face_count; f++)
        if (m->face_texture[f] >= 0)
          tex_faces++;

    printf("model %5d: verts=%4d faces=%4d rt=%d tex=%d(%d) alpha=%d | "
           "x[%5d..%5d] y[%5d..%5d] z[%5d..%5d] bad_idx=%d normals=%d\n",
           id, m->vertex_count, m->face_count, has_rt, has_tex, tex_faces,
           has_alpha, min_x, max_x, min_y, max_y, min_z, max_z, bad_idx,
           m->normals_computed);
    if (bad_idx > 0)
      printf("           bad faces: first=%d last=%d of %d\n", first_bad,
             last_bad, m->face_count);

    /* Sample lighting: first face corner colors */
    if (m->face_count > 0 && bad_idx == 0) {
      uint32_t c0 = osrs_model_light_vertex(m, 0, 0, 64, 768);
      uint32_t c1 = osrs_model_light_vertex(m, 0, 1, 64, 768);
      uint32_t c2 = osrs_model_light_vertex(m, 0, 2, 64, 768);
      printf("           face0 lit colors: #%06x #%06x #%06x (hsl=%04x)\n",
             c0 & 0xFFFFFF, c1 & 0xFFFFFF, c2 & 0xFFFFFF, m->face_color[0]);
    }

    if (bad_idx > 0)
      fail++;
    else
      ok++;
    osrs_model_free(m);
  }

  printf("=== %d ok, %d fail ===\n", ok, fail);
  osrs_cache_close(cache);
  return fail > 0 ? 1 : 0;
}
