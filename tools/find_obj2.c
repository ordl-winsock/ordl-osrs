/* tools/find_obj2.c - find objects with large model ids using real loader */
#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  int min_id = atoi(argv[2]);
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);
  osrs_config_t *config = osrs_config_init(cache);

  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cache, 2, 6, NULL);
  if (!files)
    return 1;

  int found = 0;
  for (int i = 0; i < files->count && found < 5; i++) {
    int id = files->files[i].id;
    osrs_object_def_t *def = osrs_config_load_object(config, id);
    if (!def)
      continue;
    for (int j = 0; j < def->model_count; j++) {
      if (def->model_ids[j] >= min_id) {
        printf("object %d: model_count=%d model_ids[%d]=%d (0x%x)\n", id,
               def->model_count, j, def->model_ids[j], def->model_ids[j]);
        printf("  raw bytes:");
        for (size_t k = 0; k < files->files[i].len && k < 48; k++)
          printf(" %02x", files->files[i].data[k]);
        printf("\n");
        found++;
        break;
      }
    }
    osrs_object_def_free(def);
  }
  printf("done (%d found)\n", found);
  osrs_archive_files_free(files);
  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}
