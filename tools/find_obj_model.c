/* tools/find_obj_model.c - find objects referencing a given model id + dump def
 * bytes */
#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  int target_model = atoi(argv[2]);
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);

  /* Scan all object defs for model_ids matching target */
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cache, 2, 6, NULL);
  if (!files)
    return 1;

  int found = 0;
  for (int i = 0; i < files->count && found < 3; i++) {
    osrs_archive_file_t *f = &files->files[i];
    if (f->len < 2)
      continue;
    /* crude scan: parse opcodes 1/6 model lists */
    osrs_stream_t s;
    osrs_stream_init(&s, f->data, f->len);
    while (osrs_stream_remaining(&s) > 0) {
      int opcode = osrs_stream_u8(&s);
      if (opcode == 0)
        break;
      if (opcode == 1 || opcode == 6) {
        int count = osrs_stream_u8(&s);
        for (int j = 0; j < count; j++) {
          int mid = (opcode == 1) ? (int)osrs_stream_u16(&s)
                                  : (int)osrs_stream_u32(&s);
          int type = osrs_stream_u8(&s);
          if (mid == target_model) {
            printf("object %d references model %d (type %d, def len=%zu)\n",
                   f->id, mid, type, f->len);
            printf("  def bytes:");
            for (size_t k = 0; k < f->len && k < 40; k++)
              printf(" %02x", f->data[k]);
            printf("\n");
            found++;
          }
        }
      } else if (opcode == 5 || opcode == 7) {
        int count = osrs_stream_u8(&s);
        for (int j = 0; j < count; j++) {
          int mid = (opcode == 5) ? (int)osrs_stream_u16(&s)
                                  : (int)osrs_stream_u32(&s);
          if (mid == target_model) {
            printf("object %d references model %d (no types, opcode %d)\n",
                   f->id, mid, opcode);
            found++;
          }
        }
      } else if (opcode == 2) {
        while (osrs_stream_u8(&s) != 0)
          ;
      } else if (opcode == 14 || opcode == 15 || opcode == 19 || opcode == 21 ||
                 opcode == 24 || opcode == 28 || opcode == 68 || opcode == 69 ||
                 opcode == 75) {
        osrs_stream_skip(&s, 1 + (opcode == 24 || opcode == 68));
      } else if (opcode == 29 || opcode == 39 || opcode == 61 || opcode == 70 ||
                 opcode == 71 || opcode == 72) {
        osrs_stream_skip(&s, opcode == 61 ? 2 : (opcode == 39 ? 1 : 2));
      } else if (opcode == 30 || opcode == 31 || opcode == 32 || opcode == 33 ||
                 opcode == 34) {
        while (osrs_stream_u8(&s) != 0)
          ;
      } else if (opcode == 40 || opcode == 41) {
        int c = osrs_stream_u8(&s);
        osrs_stream_skip(&s, (size_t)c * 4);
      } else if (opcode == 65 || opcode == 66 || opcode == 67) {
        osrs_stream_skip(&s, 2);
      } else if (opcode == 77 || opcode == 92) {
        osrs_stream_skip(&s, 4);
        if (opcode == 92)
          osrs_stream_skip(&s, 2);
        int c = osrs_stream_u8(&s);
        osrs_stream_skip(&s, (size_t)(c + 1) * 2);
      } else {
        /* unknown — stop scanning this def */
        goto next_def;
      }
    }
  next_def:;
  }
  osrs_archive_files_free(files);
  osrs_cache_close(cache);
  printf("done (%d found)\n", found);
  return 0;
}
