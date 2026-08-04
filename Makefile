# OSRS Client Makefile
# Pure C23, zero external dependencies

CC := $(shell which clang 2>/dev/null || which gcc 2>/dev/null || echo cc)

# Check for C23 support
C23_TEST := $(shell $(CC) -std=c23 -E - </dev/null >/dev/null 2>&1 && echo yes || echo no)
ifeq ($(C23_TEST),yes)
  CSTD := -std=c23
else
  CSTD := -std=c2x
endif

# FORGE engine
FORGE_DIR := /devops/projects/off-the-wall/ordl-game
FORGE_LIB := $(FORGE_DIR)/build/libforge.a
FORGE_INC := -I$(FORGE_DIR)/include

LDFLAGS := -lm

# Compiler-specific flags
CLANG_FLAGS := $(shell $(CC) --version 2>/dev/null | grep -qi clang && echo -Wno-gnu-zero-variadic-macro-arguments)

CFLAGS := $(CSTD) -Wall -Wextra -Wpedantic -Werror \
          -Wstrict-overflow=2 -Wformat=2 -Wshadow \
          -Wmissing-prototypes -Wmissing-declarations \
          -fno-strict-aliasing -fno-common \
          -Iinclude $(FORGE_INC) -D_DEFAULT_SOURCE -O2 -g $(CLANG_FLAGS)

# Source files
SRC_CRYPTO := src/crc32.c src/xtea.c src/isaac.c src/bignum.c src/rsa.c
# TLS 1.3 stack (vendored from ordl-govcon, pure C)
SRC_TLS := src/security/x25519.c src/security/chachapoly.c src/security/sha256.c src/security/crypto.c src/security/rsa.c src/security/tls13.c
SRC_COMPRESSION := src/bzip2.c src/gzip.c
SRC_CACHE := src/cache.c src/map.c src/xtea_keys.c
SRC_PROTOCOL := src/protocol.c src/net.c src/game.c src/playerinfo.c src/auth.c src/https.c src/jagex.c src/proofofwork.c src/log.c src/packet_parsers.c
SRC_CONFIG := src/config.c src/render_utils.c src/model.c src/item_sprite.c src/anim.c

SRC_ISO := src/iso_renderer.c src/gl_world.c

SRC_ALL := $(SRC_CRYPTO) $(SRC_COMPRESSION) $(SRC_CACHE) $(SRC_PROTOCOL) $(SRC_CONFIG) $(SRC_ISO) $(SRC_TLS)

# Object files
OBJ_DIR := build/obj
OBJ := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC_ALL))
DEPS := $(OBJ:.o=.d)

# Targets
TARGET_LIB := build/libosrs.a
TARGET_TEST := build/test_crypto
TARGET_CACHE_DUMP := build/cache_dump
TARGET_CONFIG_DUMP := build/config_dump
TARGET_LOGIN_TEST := build/login_test
TARGET_PI_TEST := build/test_playerinfo
TARGET_TLS_TEST := build/test_tls
TARGET_JAGEX_TEST := build/test_jagex
TARGET_CLIENT := build/osrs_client

TARGET_BZIP2_BULK := build/test_bzip2_bulk
TARGET_PACKET_DECODE := build/packet_decode
TARGET_GAMEPACK_ANALYZER := build/gamepack_analyzer

# Client needs FORGE (LTO required because libforge.a contains LLVM bitcode)
CLIENT_CFLAGS := $(CFLAGS) $(FORGE_INC) -flto
CLIENT_LDFLAGS := $(LDFLAGS) -flto -L$(FORGE_DIR)/build -lforge -lpthread -lrt -ldl

.PHONY: all clean test dirs tools client

all: dirs $(TARGET_LIB) $(TARGET_TEST) $(TARGET_PI_TEST) $(TARGET_CACHE_DUMP) $(TARGET_CONFIG_DUMP) $(TARGET_LOGIN_TEST) $(TARGET_TLS_TEST) $(TARGET_JAGEX_TEST) $(TARGET_BZIP2_BULK) $(TARGET_PACKET_DECODE) $(TARGET_GAMEPACK_ANALYZER) $(TARGET_CLIENT)

tools: $(TARGET_CACHE_DUMP) $(TARGET_CONFIG_DUMP) $(TARGET_LOGIN_TEST)

client: $(TARGET_CLIENT)

dirs:
	@mkdir -p $(OBJ_DIR)

$(TARGET_LIB): $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(TARGET_TEST): tests/test_crypto.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_CACHE_DUMP): tools/cache_dump.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_CONFIG_DUMP): tools/config_dump.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_LOGIN_TEST): tools/login_test.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_PI_TEST): tests/test_playerinfo.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_TLS_TEST): tools/test_tls.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_JAGEX_TEST): tools/test_jagex.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_BZIP2_BULK): tools/test_bzip2_bulk.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_PACKET_DECODE): tools/packet_decode.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(TARGET_LIB) $(LDFLAGS)

$(TARGET_GAMEPACK_ANALYZER): tools/gamepack_analyzer.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lz

$(TARGET_CLIENT): src/client.c $(TARGET_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CLIENT_CFLAGS) -o $@ $< $(TARGET_LIB) $(CLIENT_LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

test: $(TARGET_TEST) $(TARGET_PI_TEST)
	./$(TARGET_TEST)
	./$(TARGET_PI_TEST)

clean:
	rm -rf build

.PHONY: install
install: $(TARGET_LIB)
	cp $(TARGET_LIB) /usr/local/lib/
	cp -r include/osrs /usr/local/include/
