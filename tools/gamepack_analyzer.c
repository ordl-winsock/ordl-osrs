/*
 * tools/gamepack_analyzer.c - OSRS gamepack JAR analyzer
 *
 * Usage: gamepack_analyzer <gamepack.jar>
 *
 * Reads a gamepack JAR (ZIP), parses all .class files, and outputs:
 *   - Class hierarchy (superclasses, interfaces)
 *   - Method signatures (name, descriptor, access flags)
 *   - Field types (name, descriptor, access flags)
 *   - Identified key classes based on heuristics
 *
 * Build: make build/gamepack_analyzer
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

/* -------------------------------------------------------------------------- */
/* ZIP/JAR reader (PKZIP format subset)                                       */
/* -------------------------------------------------------------------------- */

typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} zip_t;

typedef struct {
  char *name;
  size_t offset;
  size_t compressed_size;
  size_t uncompressed_size;
  uint16_t compression;
} zip_entry_t;

#define ZIP_MAX_ENTRIES 4096

typedef struct {
  zip_entry_t entries[ZIP_MAX_ENTRIES];
  size_t count;
} zip_dir_t;

static uint16_t read_u16le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool zip_find_eocd(const uint8_t *data, size_t len, size_t *eocd_off) {
  /* End of central directory signature: 0x06054b50 */
  if (len < 22)
    return false;
  for (size_t i = len - 22; i + 22 <= len; i--) {
    if (read_u32le(data + i) == 0x06054b50) {
      *eocd_off = i;
      return true;
    }
  }
  return false;
}

static bool zip_read_dir(const uint8_t *data, size_t len, zip_dir_t *dir) {
  size_t eocd_off;
  if (!zip_find_eocd(data, len, &eocd_off))
    return false;

  uint16_t num_entries = read_u16le(data + eocd_off + 8);
  uint32_t cd_size = read_u32le(data + eocd_off + 12);
  uint32_t cd_off = read_u32le(data + eocd_off + 16);

  if (cd_off + cd_size > len)
    return false;

  dir->count = 0;
  size_t p = cd_off;
  for (size_t i = 0; i < num_entries && dir->count < ZIP_MAX_ENTRIES; i++) {
    if (p + 46 > len)
      break;
    if (read_u32le(data + p) != 0x02014b50)
      break;

    uint16_t name_len = read_u16le(data + p + 28);
    uint16_t extra_len = read_u16le(data + p + 30);
    uint16_t comment_len = read_u16le(data + p + 32);
    uint32_t local_off = read_u32le(data + p + 42);

    if (p + 46 + name_len > len)
      break;

    zip_entry_t *e = &dir->entries[dir->count++];
    char *name_buf = malloc(name_len + 1);
    if (name_buf) {
      memcpy(name_buf, data + p + 46, name_len);
      name_buf[name_len] = '\0';
    }
    e->name = name_buf;
    e->offset = local_off;
    e->compressed_size = read_u32le(data + p + 20);
    e->uncompressed_size = read_u32le(data + p + 24);
    e->compression = read_u16le(data + p + 10);

    p += 46 + name_len + extra_len + comment_len;
  }
  return true;
}

static bool zip_extract(const uint8_t *data, size_t len,
                        const zip_entry_t *entry, uint8_t *out,
                        size_t out_cap) {
  if (entry->offset + 30 > len)
    return false;
  uint16_t name_len = read_u16le(data + entry->offset + 26);
  uint16_t extra_len = read_u16le(data + entry->offset + 28);
  size_t data_off = entry->offset + 30 + name_len + extra_len;
  if (data_off + entry->compressed_size > len)
    return false;

  if (entry->compression == 0) {
    /* Stored */
    if (entry->uncompressed_size > out_cap)
      return false;
    memcpy(out, data + data_off, entry->uncompressed_size);
    return true;
  }

  if (entry->compression == 8) {
    /* DEFLATE */
    if (entry->uncompressed_size > out_cap)
      return false;
    z_stream zs = {0};
    if (inflateInit2(&zs, -15) != Z_OK)
      return false;
    zs.avail_in = (uInt)entry->compressed_size;
    zs.next_in = (Bytef *)(data + data_off);
    zs.avail_out = (uInt)entry->uncompressed_size;
    zs.next_out = out;
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
  }

  return false;
}

/* -------------------------------------------------------------------------- */
/* Java class file parser                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} class_reader_t;

static uint8_t cr_u8(class_reader_t *r) { return r->data[r->pos++]; }

static uint16_t cr_u16(class_reader_t *r) {
  uint16_t v = ((uint16_t)r->data[r->pos] << 8) | r->data[r->pos + 1];
  r->pos += 2;
  return v;
}

static uint32_t cr_u32(class_reader_t *r) {
  uint32_t v = ((uint32_t)r->data[r->pos] << 24) |
               ((uint32_t)r->data[r->pos + 1] << 16) |
               ((uint32_t)r->data[r->pos + 2] << 8) | r->data[r->pos + 3];
  r->pos += 4;
  return v;
}

static void cr_skip(class_reader_t *r, size_t n) { r->pos += n; }

/* Constant pool tags */
#define CP_UTF8 1
#define CP_INTEGER 3
#define CP_FLOAT 4
#define CP_LONG 5
#define CP_DOUBLE 6
#define CP_CLASS 7
#define CP_STRING 8
#define CP_FIELDREF 9
#define CP_METHODREF 10
#define CP_INTERFACE_METHODREF 11
#define CP_NAME_AND_TYPE 12
#define CP_METHOD_HANDLE 15
#define CP_METHOD_TYPE 16
#define CP_INVOKE_DYNAMIC 18

typedef struct {
  uint8_t tag;
  union {
    struct {
      const char *str;
      uint16_t len;
    } utf8;
    struct {
      uint16_t name_index;
    } class_info;
    struct {
      uint16_t class_index;
      uint16_t name_and_type_index;
    } ref;
    struct {
      uint16_t name_index;
      uint16_t desc_index;
    } name_and_type;
    struct {
      uint16_t string_index;
    } string;
    int32_t integer;
    int64_t long_val;
    float float_val;
    double double_val;
  } u;
} cp_entry_t;

typedef struct {
  cp_entry_t *entries;
  uint16_t count;
} cp_t;

static const char *cp_utf8(const cp_t *cp, uint16_t idx) {
  if (idx == 0 || idx >= cp->count)
    return "";
  cp_entry_t *e = &cp->entries[idx];
  if (e->tag == CP_UTF8)
    return e->u.utf8.str;
  return "";
}

static bool parse_constant_pool(class_reader_t *r, cp_t *cp) {
  uint16_t count = cr_u16(r);
  cp->count = count;
  if (count == 0)
    return true;
  cp->entries = calloc(count, sizeof(cp_entry_t));
  if (!cp->entries)
    return false;

  for (uint16_t i = 1; i < count; i++) {
    cp_entry_t *e = &cp->entries[i];
    e->tag = cr_u8(r);
    switch (e->tag) {
    case CP_UTF8: {
      uint16_t len = cr_u16(r);
      e->u.utf8.str = (const char *)(r->data + r->pos);
      e->u.utf8.len = len;
      cr_skip(r, len);
      break;
    }
    case CP_INTEGER:
      e->u.integer = (int32_t)cr_u32(r);
      break;
    case CP_FLOAT:
      cr_skip(r, 4);
      break;
    case CP_LONG:
      cr_skip(r, 8);
      i++; /* long/double take 2 slots */
      break;
    case CP_DOUBLE:
      cr_skip(r, 8);
      i++;
      break;
    case CP_CLASS:
    case CP_STRING:
      e->u.class_info.name_index = cr_u16(r);
      break;
    case CP_FIELDREF:
    case CP_METHODREF:
    case CP_INTERFACE_METHODREF:
      e->u.ref.class_index = cr_u16(r);
      e->u.ref.name_and_type_index = cr_u16(r);
      break;
    case CP_NAME_AND_TYPE:
      e->u.name_and_type.name_index = cr_u16(r);
      e->u.name_and_type.desc_index = cr_u16(r);
      break;
    case CP_METHOD_HANDLE:
      cr_skip(r, 3);
      break;
    case CP_METHOD_TYPE:
      cr_skip(r, 2);
      break;
    case CP_INVOKE_DYNAMIC:
      cr_skip(r, 4);
      break;
    default:
      fprintf(stderr, "Unknown CP tag %d at index %d\n", e->tag, i);
      return false;
    }
  }
  return true;
}

static void free_cp(cp_t *cp) {
  free(cp->entries);
  cp->entries = NULL;
  cp->count = 0;
}

typedef struct {
  uint16_t access;
  const char *name;
  const char *descriptor;
} field_info_t;

typedef struct {
  uint16_t access;
  const char *name;
  const char *descriptor;
  uint16_t attr_count;
} method_info_t;

typedef struct {
  const char *name;
  const char *super;
  const char **interfaces;
  uint16_t interface_count;
  field_info_t *fields;
  uint16_t field_count;
  method_info_t *methods;
  uint16_t method_count;
  uint16_t access;
} class_info_t;

static bool parse_class(class_reader_t *r, class_info_t *cls) {
  uint32_t magic = cr_u32(r);
  if (magic != 0xCAFEBABE)
    return false;
  cr_skip(r, 4); /* minor + major version */

  cp_t cp;
  if (!parse_constant_pool(r, &cp))
    return false;

  cls->access = cr_u16(r);
  uint16_t this_class = cr_u16(r);
  uint16_t super_class = cr_u16(r);
  cls->name = cp_utf8(&cp, cp.entries[this_class].u.class_info.name_index);
  cls->super =
      super_class
          ? cp_utf8(&cp, cp.entries[super_class].u.class_info.name_index)
          : "";

  cls->interface_count = cr_u16(r);
  cls->interfaces = calloc(cls->interface_count, sizeof(const char *));
  for (uint16_t i = 0; i < cls->interface_count; i++) {
    uint16_t iface = cr_u16(r);
    cls->interfaces[i] =
        cp_utf8(&cp, cp.entries[iface].u.class_info.name_index);
  }

  cls->field_count = cr_u16(r);
  cls->fields = calloc(cls->field_count, sizeof(field_info_t));
  for (uint16_t i = 0; i < cls->field_count; i++) {
    cls->fields[i].access = cr_u16(r);
    uint16_t name_idx = cr_u16(r);
    uint16_t desc_idx = cr_u16(r);
    cls->fields[i].name = cp_utf8(&cp, name_idx);
    cls->fields[i].descriptor = cp_utf8(&cp, desc_idx);
    uint16_t attr_count = cr_u16(r);
    for (uint16_t j = 0; j < attr_count; j++) {
      cr_skip(r, 2);
      uint32_t attr_len = cr_u32(r);
      cr_skip(r, attr_len);
    }
  }

  cls->method_count = cr_u16(r);
  cls->methods = calloc(cls->method_count, sizeof(method_info_t));
  for (uint16_t i = 0; i < cls->method_count; i++) {
    cls->methods[i].access = cr_u16(r);
    uint16_t name_idx = cr_u16(r);
    uint16_t desc_idx = cr_u16(r);
    cls->methods[i].name = cp_utf8(&cp, name_idx);
    cls->methods[i].descriptor = cp_utf8(&cp, desc_idx);
    cls->methods[i].attr_count = cr_u16(r);
    for (uint16_t j = 0; j < cls->methods[i].attr_count; j++) {
      cr_skip(r, 2);
      uint32_t attr_len = cr_u32(r);
      cr_skip(r, attr_len);
    }
  }

  free_cp(&cp);
  return true;
}

static void free_class(class_info_t *cls) {
  free(cls->interfaces);
  free(cls->fields);
  free(cls->methods);
  memset(cls, 0, sizeof(*cls));
}

/* -------------------------------------------------------------------------- */
/* Heuristic identifiers                                                      */
/* -------------------------------------------------------------------------- */

static bool is_likely_client(const class_info_t *cls) {
  /* Client class typically: extends GameEngine/Applet, has many methods/fields,
   * has getCanvasWidth/Height-like methods, game state getters */
  if (cls->method_count < 50 || cls->field_count < 50)
    return false;
  bool has_width = false, has_height = false;
  for (uint16_t i = 0; i < cls->method_count; i++) {
    const char *name = cls->methods[i].name;
    const char *desc = cls->methods[i].descriptor;
    if (strstr(name, "Width") && strcmp(desc, "()I") == 0)
      has_width = true;
    if (strstr(name, "Height") && strcmp(desc, "()I") == 0)
      has_height = true;
  }
  return has_width && has_height;
}

static bool is_likely_buffer(const class_info_t *cls) {
  /* Buffer/PacketBuffer: has read/write methods for byte/short/int/string */
  if (cls->method_count < 10)
    return false;
  int rw_count = 0;
  for (uint16_t i = 0; i < cls->method_count; i++) {
    const char *name = cls->methods[i].name;
    if (strstr(name, "read") || strstr(name, "write") || strstr(name, "put") ||
        strstr(name, "get"))
      rw_count++;
  }
  return rw_count >= 8;
}

static bool is_likely_isaac(const class_info_t *cls) {
  /* ISAAC: has nextInt/next method, few fields/methods */
  if (cls->method_count > 10 || cls->field_count > 10)
    return false;
  for (uint16_t i = 0; i < cls->method_count; i++) {
    const char *name = cls->methods[i].name;
    if (strstr(name, "next") || strstr(name, "Next"))
      return true;
  }
  return false;
}

static bool is_likely_actor(const class_info_t *cls) {
  /* Actor: has x/y coordinates, animation methods */
  if (cls->method_count < 10)
    return false;
  bool has_x = false, has_y = false;
  for (uint16_t i = 0; i < cls->field_count; i++) {
    const char *name = cls->fields[i].name;
    if (strcmp(name, "x") == 0 || strcmp(name, "X") == 0)
      has_x = true;
    if (strcmp(name, "y") == 0 || strcmp(name, "Y") == 0)
      has_y = true;
  }
  return has_x && has_y;
}

/* -------------------------------------------------------------------------- */
/* Output                                                                     */
/* -------------------------------------------------------------------------- */

static void print_class(const class_info_t *cls) {
  printf("\n========================================\n");
  printf("CLASS: %s\n", cls->name);
  printf("  Super: %s\n", cls->super[0] ? cls->super : "(none)");
  printf("  Interfaces: %d\n", cls->interface_count);
  for (uint16_t i = 0; i < cls->interface_count; i++)
    printf("    - %s\n", cls->interfaces[i]);
  printf("  Fields: %d\n", cls->field_count);
  for (uint16_t i = 0; i < cls->field_count && i < 20; i++) {
    printf("    %s %s\n", cls->fields[i].descriptor, cls->fields[i].name);
  }
  if (cls->field_count > 20)
    printf("    ... (%d more)\n", cls->field_count - 20);
  printf("  Methods: %d\n", cls->method_count);
  for (uint16_t i = 0; i < cls->method_count && i < 20; i++) {
    printf("    %s %s\n", cls->methods[i].descriptor, cls->methods[i].name);
  }
  if (cls->method_count > 20)
    printf("    ... (%d more)\n", cls->method_count - 20);

  if (is_likely_client(cls))
    printf("  *** HEURISTIC: Likely CLIENT class ***\n");
  if (is_likely_buffer(cls))
    printf("  *** HEURISTIC: Likely BUFFER class ***\n");
  if (is_likely_isaac(cls))
    printf("  *** HEURISTIC: Likely ISAAC class ***\n");
  if (is_likely_actor(cls))
    printf("  *** HEURISTIC: Likely ACTOR class ***\n");
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <gamepack.jar>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror("fopen");
    return 1;
  }

  fseek(f, 0, SEEK_END);
  long flen = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *data = malloc((size_t)flen);
  if (!data) {
    perror("malloc");
    fclose(f);
    return 1;
  }

  if (fread(data, 1, (size_t)flen, f) != (size_t)flen) {
    perror("fread");
    free(data);
    fclose(f);
    return 1;
  }
  fclose(f);

  zip_dir_t dir;
  if (!zip_read_dir(data, (size_t)flen, &dir)) {
    fprintf(stderr, "Error: not a valid ZIP/JAR file\n");
    free(data);
    return 1;
  }

  printf("Gamepack: %s\n", argv[1]);
  printf("Entries: %zu\n\n", dir.count);

  size_t classes_parsed = 0;
  for (size_t i = 0; i < dir.count; i++) {
    const zip_entry_t *e = &dir.entries[i];
    size_t name_len = strlen(e->name);
    if (name_len < 6 || strcmp(e->name + name_len - 6, ".class") != 0)
      continue;

    uint8_t class_data[65536];
    if (!zip_extract(data, (size_t)flen, e, class_data, sizeof(class_data)))
      continue;

    class_reader_t cr = {
        .data = class_data, .len = e->uncompressed_size, .pos = 0};
    class_info_t cls = {0};
    if (parse_class(&cr, &cls)) {
      print_class(&cls);
      classes_parsed++;
    }
    free_class(&cls);
  }

  printf("\n========================================\n");
  printf("Total classes parsed: %zu\n", classes_parsed);

  free(data);
  return 0;
}
