/*
 * ORDL GovCon - TLS 1.3 Implementation
 * RFC 8446. X25519 + AES-256-GCM + ChaCha20-Poly1305 + SHA-384 + SHA-256
 * Pure C23, zero dependencies
 */

#define _POSIX_C_SOURCE 200809L

#include "security/tls13.h"
#include "security/chachapoly.h"
#include "security/crypto.h"
#include "security/rsa.h"
#include "security/sha256.h"
#include "security/x25519.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef explicit_bzero
#define explicit_bzero(ptr, len) memset(ptr, 0, len)
#endif

#define CONTENT_TYPE_HANDSHAKE 22
#define CONTENT_TYPE_APPDATA 23
#define CONTENT_TYPE_CHANGE_CIPHER_SPEC 20
#define TLS_MAX_PLAINTEXT 16384

static const uint8_t tls13_supported_versions_ext[7] = {0x00, 0x2b, 0x00, 0x03,
                                                        0x02, 0x03, 0x04};

static const uint8_t tls13_key_share_ext_header[4] = {0x00, 0x33, 0x00, 0x26};

static const uint8_t tls13_x25519_group[2] = {0x00, 0x1d};

/* -------------------------------------------------------------------------- */
/* Loaded cert/key overrides (filesystem)                                     */
/* -------------------------------------------------------------------------- */

static uint8_t g_loaded_cert[2048];
static size_t g_loaded_cert_len = 0;
static uint8_t g_loaded_n[512];
static size_t g_loaded_n_len = 0;
static uint8_t g_loaded_d[512];
static size_t g_loaded_d_len = 0;
static bool g_has_loaded_cert = false;
static bool g_has_loaded_key = false;

/* -------------------------------------------------------------------------- */
/* Derive-Secret helper                                                       */
/* -------------------------------------------------------------------------- */

static void tls13_derive_secret(const uint8_t secret[32], const char *label,
                                const uint8_t *ctx_hash, size_t ctx_hash_len,
                                uint8_t *out, size_t out_len) {
  uint8_t info[128];
  size_t label_len = strlen(label);
  size_t info_len = 0;
  info[info_len++] = 0;
  info[info_len++] = (uint8_t)out_len;
  info[info_len++] = (uint8_t)(6 + label_len);
  memcpy(info + info_len, "tls13 ", 6);
  info_len += 6;
  memcpy(info + info_len, label, label_len);
  info_len += label_len;
  info[info_len++] = (uint8_t)ctx_hash_len;
  if (ctx_hash_len > 0) {
    memcpy(info + info_len, ctx_hash, ctx_hash_len);
    info_len += ctx_hash_len;
  }
  gc_hkdf_sha256_expand(secret, info, info_len, out, out_len);
}

static void tls13_derive_secret384(const uint8_t secret[48], const char *label,
                                   const uint8_t *ctx_hash, size_t ctx_hash_len,
                                   uint8_t *out, size_t out_len) {
  uint8_t info[128];
  size_t label_len = strlen(label);
  size_t info_len = 0;
  info[info_len++] = 0;
  info[info_len++] = (uint8_t)out_len;
  info[info_len++] = (uint8_t)(6 + label_len);
  memcpy(info + info_len, "tls13 ", 6);
  info_len += 6;
  memcpy(info + info_len, label, label_len);
  info_len += label_len;
  info[info_len++] = (uint8_t)ctx_hash_len;
  if (ctx_hash_len > 0) {
    memcpy(info + info_len, ctx_hash, ctx_hash_len);
    info_len += ctx_hash_len;
  }
  gc_hkdf_sha384_expand(secret, info, info_len, out, out_len);
}

/* -------------------------------------------------------------------------- */
/* Generic record encrypt/decrypt (key/iv/seq explicit)                       */
/* -------------------------------------------------------------------------- */

static void tls13_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
  memcpy(nonce, iv, 12);
  nonce[4] ^= (uint8_t)(seq >> 56);
  nonce[5] ^= (uint8_t)(seq >> 48);
  nonce[6] ^= (uint8_t)(seq >> 40);
  nonce[7] ^= (uint8_t)(seq >> 32);
  nonce[8] ^= (uint8_t)(seq >> 24);
  nonce[9] ^= (uint8_t)(seq >> 16);
  nonce[10] ^= (uint8_t)(seq >> 8);
  nonce[11] ^= (uint8_t)(seq);
}

static size_t tls13_encrypt_record(const uint8_t key[32], const uint8_t iv[12],
                                   uint64_t *seq, uint16_t cipher_suite,
                                   uint8_t content_type,
                                   const uint8_t *plaintext, size_t pt_len,
                                   uint8_t *out, size_t out_cap) {
  if (pt_len > TLS_MAX_PLAINTEXT)
    return 0;
  if (out_cap < 5 + pt_len + 1 + 16)
    return 0;

  uint8_t nonce[12];
  tls13_nonce(iv, *seq, nonce);

  uint8_t aad[5];
  aad[0] = CONTENT_TYPE_APPDATA;
  aad[1] = 0x03;
  aad[2] = 0x03;
  uint16_t payload_len = (uint16_t)(pt_len + 1 + 16);
  aad[3] = (uint8_t)(payload_len >> 8);
  aad[4] = (uint8_t)(payload_len);

  uint8_t inner[TLS_MAX_PLAINTEXT + 1];
  memcpy(inner, plaintext, pt_len);
  inner[pt_len] = content_type;

  uint8_t tag[16];
  if (cipher_suite == 0x1302) {
    gc_aes256_gcm_ctx_t aes;
    gc_aes256_gcm_init(&aes, key, nonce);
    gc_aes256_gcm_encrypt(&aes, inner, pt_len + 1, aad, 5, out + 5, tag);
  } else {
    gc_chachapoly_seal(key, nonce, aad, 5, inner, pt_len + 1, out + 5, tag);
  }
  explicit_bzero(inner, pt_len + 1);

  memcpy(out, aad, 5);
  memcpy(out + 5 + pt_len + 1, tag, 16);
  (*seq)++;
  return 5 + pt_len + 1 + 16;
}

static size_t tls13_decrypt_record(const uint8_t key[32], const uint8_t iv[12],
                                   uint64_t *seq, uint16_t cipher_suite,
                                   const uint8_t *record, size_t record_len,
                                   uint8_t *out, size_t out_cap,
                                   uint8_t *out_content_type) {
  if (record_len < 5 + 16 + 1)
    return (size_t)-1;

  uint8_t nonce[12];
  tls13_nonce(iv, *seq, nonce);

  uint8_t aad[5];
  memcpy(aad, record, 5);
  size_t inner_len = record_len - 5 - 16;
  if (inner_len > TLS_MAX_PLAINTEXT + 1)
    return (size_t)-1;
  const uint8_t *ct = record + 5;
  const uint8_t *tag = record + record_len - 16;

  uint8_t pt[TLS_MAX_PLAINTEXT + 1];
  int ok;

  if (cipher_suite == 0x1302) {
    gc_aes256_gcm_ctx_t aes;
    gc_aes256_gcm_init(&aes, key, nonce);
    ok = gc_aes256_gcm_decrypt(&aes, ct, inner_len, aad, 5, tag, pt);
  } else {
    ok = gc_chachapoly_open(key, nonce, aad, 5, ct, inner_len, tag, pt);
  }
  if (ok != 0) {
    return (size_t)-1;
  }

  if (inner_len < 1)
    return (size_t)-1;
  size_t actual = inner_len - 1;
  while (actual > 0 && pt[actual] == 0)
    actual--;
  uint8_t content_type = pt[actual];
  if (actual > out_cap) {
    explicit_bzero(pt, inner_len);
    return (size_t)-1;
  }
  memcpy(out, pt, actual);
  explicit_bzero(pt, inner_len);
  if (out_content_type)
    *out_content_type = content_type;
  (*seq)++;
  return actual;
}

/* -------------------------------------------------------------------------- */
/* Server record encrypt/decrypt (wrappers)                                   */
/* -------------------------------------------------------------------------- */

size_t gc_tls13_server_encrypt(gc_tls13_server_t *srv, uint8_t content_type,
                               const uint8_t *plaintext, size_t pt_len,
                               uint8_t *out, size_t out_cap) {
  return tls13_encrypt_record(srv->server_write_key, srv->server_write_iv,
                              &srv->server_seq, srv->cipher_suite, content_type,
                              plaintext, pt_len, out, out_cap);
}

size_t gc_tls13_server_decrypt(gc_tls13_server_t *srv, const uint8_t *record,
                               size_t record_len, uint8_t *out, size_t out_cap,
                               uint8_t *out_content_type) {
  return tls13_decrypt_record(srv->client_write_key, srv->client_write_iv,
                              &srv->client_seq, srv->cipher_suite, record,
                              record_len, out, out_cap, out_content_type);
}

/* -------------------------------------------------------------------------- */
/* Socket helpers                                                             */
/* -------------------------------------------------------------------------- */

static bool send_all(int sock, const uint8_t *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(sock, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR)
        continue;
      return false;
    }
    sent += (size_t)n;
  }
  return true;
}

static bool recv_exact(int sock, uint8_t *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = recv(sock, buf + off, len - off, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    off += (size_t)n;
  }
  return true;
}

static bool read_tls_record(int sock, uint8_t **record, size_t *rlen) {
  uint8_t hdr[5];
  if (!recv_exact(sock, hdr, 5))
    return false;
  uint16_t payload_len = ((uint16_t)hdr[3] << 8) | hdr[4];
  if (payload_len > TLS_MAX_PLAINTEXT + 256)
    return false;
  uint8_t *r = malloc(5 + payload_len);
  if (!r)
    return false;
  memcpy(r, hdr, 5);
  if (!recv_exact(sock, r + 5, payload_len)) {
    free(r);
    return false;
  }
  *record = r;
  *rlen = 5 + payload_len;
  return true;
}

/* -------------------------------------------------------------------------- */
/* Handshake                                                                  */
/* -------------------------------------------------------------------------- */

bool gc_tls13_server_handshake(gc_tls13_server_t *srv, int fd) {
  memset(srv, 0, sizeof(*srv));

  /* Require filesystem-loaded cert/key — no hardcoded fallback */
  if (!g_has_loaded_cert || !g_has_loaded_key) {
    fprintf(stderr, "[tls13] handshake failed: cert/key not loaded\n");
    return false;
  }
  const uint8_t *cert = g_loaded_cert;
  size_t cert_len = g_loaded_cert_len;
  const uint8_t *rsa_n = g_loaded_n;
  size_t rsa_n_len = g_loaded_n_len;
  const uint8_t *rsa_d = g_loaded_d;
  size_t rsa_d_len = g_loaded_d_len;

  fprintf(stderr,
          "[tls13] handshake start: cert=%p len=%zu has_cert=%d has_key=%d\n",
          (const void *)cert, cert_len, g_has_loaded_cert, g_has_loaded_key);

  /* 1. Read ClientHello */
  uint8_t *ch_record = NULL;
  size_t ch_rlen = 0;
  if (!read_tls_record(fd, &ch_record, &ch_rlen)) {
    fprintf(stderr, "[tls13] read ClientHello failed\n");
    return false;
  }
  if (ch_rlen < 5 || ch_record[0] != CONTENT_TYPE_HANDSHAKE) {
    free(ch_record);
    return false;
  }
  const uint8_t *ch = ch_record + 5;

  /* Parse ClientHello to extract client_pub */
  if (ch[0] != 0x01) {
    free(ch_record);
    fprintf(stderr, "[tls13] not ClientHello\n");
    return false;
  }
  size_t ch_body_len = ((size_t)ch[1] << 16) | ((size_t)ch[2] << 8) | ch[3];
  if (ch_rlen < 9 + ch_body_len) {
    free(ch_record);
    fprintf(stderr, "[tls13] CH len mismatch\n");
    return false;
  }

  /* Scan extensions for key_share */
  const uint8_t *p = ch + 4;
  p += 2 + 32; /* version + random */
  size_t sid_len = *p++;
  p += sid_len;
  size_t cs_len = ((size_t)p[0] << 8) | p[1];
  p += 2 + cs_len;
  size_t cm_len = *p++;
  p += cm_len;
  size_t ext_len = ((size_t)p[0] << 8) | p[1];
  p += 2;
  const uint8_t *ext_end = p + ext_len;
  fprintf(stderr, "[tls13] sid_len=%zu cs_len=%zu cm_len=%zu ext_len=%zu\n",
          sid_len, cs_len, cm_len, ext_len);
  bool found_key = false;
  bool alpn_h2 = false;
  while (p + 4 <= ext_end) {
    uint16_t etype = ((uint16_t)p[0] << 8) | p[1];
    uint16_t elen = ((uint16_t)p[2] << 8) | p[3];
    p += 4;
    fprintf(stderr, "[tls13] ext type=0x%04x len=%u\n", etype, elen);
    if (etype == 0x0033 && elen >= 6) { /* key_share */
      uint16_t shares_len = ((uint16_t)p[0] << 8) | p[1];
      const uint8_t *sp = p + 2;
      const uint8_t *send = p + 2 + shares_len;
      while (sp + 4 <= send) {
        uint16_t group = ((uint16_t)sp[0] << 8) | sp[1];
        uint16_t kex_len = ((uint16_t)sp[2] << 8) | sp[3];
        sp += 4;
        if (sp + kex_len > send)
          break;
        if (group == 0x001d && kex_len == 32) {
          memcpy(srv->client_pub, sp, 32);
          found_key = true;
          break;
        }
        sp += kex_len;
      }
    } else if (etype == 0x0010 && elen >= 5) { /* ALPN */
      uint16_t list_len = ((uint16_t)p[0] << 8) | p[1];
      const uint8_t *ap = p + 2;
      const uint8_t *aend = p + 2 + list_len;
      while (ap + 1 <= aend) {
        uint8_t plen = ap[0];
        ap++;
        if (ap + plen > aend)
          break;
        if (plen == 2 && ap[0] == 'h' && ap[1] == '2') {
          alpn_h2 = true;
          break;
        }
        ap += plen;
      }
    }
    p += elen;
  }
  if (!found_key) {
    free(ch_record);
    fprintf(stderr, "[tls13] no key_share found\n");
    return false;
  }

  /* 2. Transcript hash after ClientHello */
  gc_sha256_t ctx;
  gc_sha256_init(&ctx);
  gc_sha256_update(&ctx, ch, 4 + ch_body_len);
  gc_sha256_t tmp_ctx = ctx;
  gc_sha256_final(&tmp_ctx, srv->transcript_hash);

  /* 3. Generate server ephemeral keypair */
  uint8_t entropy[32];
  ssize_t n = getrandom(entropy, sizeof(entropy), 0);
  if (n != (ssize_t)sizeof(entropy)) {
    int fd_r = open("/dev/urandom", O_RDONLY);
    if (fd_r >= 0) {
      ssize_t _r = read(fd_r, entropy, 32);
      (void)_r;
      close(fd_r);
    }
  }
  gc_x25519_gen_private(srv->server_priv, entropy);
  gc_x25519_public_from_private(srv->server_pub, srv->server_priv);
  explicit_bzero(entropy, sizeof(entropy));

  /* 4. Compute shared secret */
  gc_x25519_shared_secret(srv->shared_secret, srv->server_priv,
                          srv->client_pub);

  /* 5. Build ServerHello */
  uint8_t sh[128];
  uint8_t *sp = sh;
  *sp++ = 0x02; /* ServerHello */
  uint8_t *sh_len_ptr = sp;
  sp += 3;
  uint8_t *sh_start = sp;
  *sp++ = 0x03;
  *sp++ = 0x03; /* TLS 1.2 compat */
  for (int i = 0; i < 32; i++)
    *sp++ = 0x00; /* random */
  *sp++ = 0x00;   /* session id length */
  *sp++ = 0x13;
  *sp++ = 0x03; /* cipher suite */
  *sp++ = 0x00; /* compression */
  uint8_t *sh_ext_len_ptr = sp;
  sp += 2;
  uint8_t *sh_ext_start = sp;
  memcpy(sp, tls13_supported_versions_ext,
         sizeof(tls13_supported_versions_ext));
  sp += sizeof(tls13_supported_versions_ext);
  *sp++ = 0x00;
  *sp++ = 0x33; /* extension type: key_share */
  *sp++ = 0x00;
  *sp++ = 0x24; /* extension length: 36 */
  memcpy(sp, tls13_x25519_group, 2);
  sp += 2;
  *sp++ = 0x00;
  *sp++ = 0x20;
  memcpy(sp, srv->server_pub, 32);
  sp += 32;
  if (alpn_h2) {
    *sp++ = 0x00;
    *sp++ = 0x10; /* extension type: ALPN */
    *sp++ = 0x00;
    *sp++ = 0x05; /* extension length: 5 */
    *sp++ = 0x00;
    *sp++ = 0x03; /* protocol list length: 3 */
    *sp++ = 0x02; /* protocol name length: 2 */
    *sp++ = 'h';
    *sp++ = '2'; /* protocol name: h2 */
  }
  size_t sh_ext_len = (size_t)(sp - sh_ext_start);
  sh_ext_len_ptr[0] = (uint8_t)(sh_ext_len >> 8);
  sh_ext_len_ptr[1] = (uint8_t)sh_ext_len;
  size_t sh_body_len = (size_t)(sp - sh_start);
  sh_len_ptr[0] = (uint8_t)(sh_body_len >> 16);
  sh_len_ptr[1] = (uint8_t)(sh_body_len >> 8);
  sh_len_ptr[2] = (uint8_t)sh_body_len;

  /* 6. Send ServerHello */
  uint8_t sh_record[5 + 128];
  sh_record[0] = CONTENT_TYPE_HANDSHAKE;
  sh_record[1] = 0x03;
  sh_record[2] = 0x03;
  sh_record[3] = (uint8_t)((sh_body_len + 4) >> 8);
  sh_record[4] = (uint8_t)(sh_body_len + 4);
  memcpy(sh_record + 5, sh, 4 + sh_body_len);
  if (!send_all(fd, sh_record, 5 + 4 + sh_body_len)) {
    free(ch_record);
    fprintf(stderr, "[tls13] send ServerHello failed\n");
    return false;
  }

  /* Update transcript */
  gc_sha256_update(&ctx, sh, 4 + sh_body_len);
  tmp_ctx = ctx;
  gc_sha256_final(&tmp_ctx, srv->transcript_hash);

  /* 7. Derive handshake keys */
  uint8_t zeros[32] = {0};
  uint8_t early_secret[32];
  gc_hkdf_sha256_extract(NULL, 0, zeros, 32, early_secret);
  uint8_t empty_hash[32];
  gc_sha256((const uint8_t *)"", 0, empty_hash);
  uint8_t derived_secret[32];
  tls13_derive_secret(early_secret, "derived", empty_hash, 32, derived_secret,
                      32);
  gc_hkdf_sha256_extract(derived_secret, 32, srv->shared_secret, 32,
                         srv->handshake_secret);
  tls13_derive_secret(srv->handshake_secret, "s hs traffic",
                      srv->transcript_hash, 32,
                      srv->server_handshake_traffic_secret, 32);
  tls13_derive_secret(srv->handshake_secret, "c hs traffic",
                      srv->transcript_hash, 32,
                      srv->client_handshake_traffic_secret, 32);
  tls13_derive_secret(srv->server_handshake_traffic_secret, "key", NULL, 0,
                      srv->server_write_key, 32);
  tls13_derive_secret(srv->server_handshake_traffic_secret, "iv", NULL, 0,
                      srv->server_write_iv, 12);
  tls13_derive_secret(srv->client_handshake_traffic_secret, "key", NULL, 0,
                      srv->client_write_key, 32);
  tls13_derive_secret(srv->client_handshake_traffic_secret, "iv", NULL, 0,
                      srv->client_write_iv, 12);

  /* 8. Build and send encrypted server flight */
  /* EncryptedExtensions (empty) */
  uint8_t ee[8] = {0x08, 0x00, 0x00, 0x02, 0x00, 0x00};
  size_t ee_len = 6;

  /* Certificate message */
  uint8_t cert_msg[2048];
  uint8_t *cp = cert_msg;
  *cp++ = 0x0b; /* Certificate */
  uint8_t *cert_len_ptr = cp;
  cp += 3;
  uint8_t *cert_start = cp;
  *cp++ = 0x00; /* request_context length */
  uint8_t *list_len_ptr = cp;
  cp += 3;
  uint8_t *list_start = cp;
  uint8_t *entry_len_ptr = cp;
  cp += 3;
  uint8_t *entry_start = cp;
  uint8_t *cert_data_len_ptr = cp;
  cp += 3;
  memcpy(cp, cert, cert_len);
  cp += cert_len;
  size_t cert_data_len = (size_t)(cp - entry_start - 3);
  cert_data_len_ptr[0] = (uint8_t)(cert_data_len >> 16);
  cert_data_len_ptr[1] = (uint8_t)(cert_data_len >> 8);
  cert_data_len_ptr[2] = (uint8_t)cert_data_len;
  *cp++ = 0x00;
  *cp++ = 0x00; /* extensions length */
  size_t entry_len = (size_t)(cp - entry_start);
  entry_len_ptr[0] = (uint8_t)(entry_len >> 16);
  entry_len_ptr[1] = (uint8_t)(entry_len >> 8);
  entry_len_ptr[2] = (uint8_t)entry_len;
  size_t list_len = (size_t)(cp - list_start);
  list_len_ptr[0] = (uint8_t)(list_len >> 16);
  list_len_ptr[1] = (uint8_t)(list_len >> 8);
  list_len_ptr[2] = (uint8_t)list_len;
  size_t cert_msg_len = (size_t)(cp - cert_start);
  cert_len_ptr[0] = (uint8_t)(cert_msg_len >> 16);
  cert_len_ptr[1] = (uint8_t)(cert_msg_len >> 8);
  cert_len_ptr[2] = (uint8_t)cert_msg_len;

  /* Build CertificateVerify */
  /* transcript hash before CertificateVerify */
  gc_sha256_update(&ctx, ee, ee_len);
  gc_sha256_update(&ctx, cert_msg, 4 + cert_msg_len);
  tmp_ctx = ctx;
  uint8_t cv_hash[32];
  gc_sha256_final(&tmp_ctx, cv_hash);

  uint8_t cv_content[130] = {0x20};
  memset(cv_content + 1, 0x20, 64);
  memcpy(cv_content + 65, "TLS 1.3, server CertificateVerify", 33);
  cv_content[98] = 0x00;
  gc_sha256_t cv_ctx;
  gc_sha256_init(&cv_ctx);
  gc_sha256_update(&cv_ctx, cv_content, 98 + 1);
  gc_sha256_update(&cv_ctx, cv_hash, 32);
  uint8_t cv_tbs_hash[32];
  gc_sha256_final(&cv_ctx, cv_tbs_hash);

  uint8_t cv_sig[256];
  if (gc_rsa_pss_sha256_sign(cv_tbs_hash, rsa_n, rsa_n_len, rsa_d, rsa_d_len,
                             cv_sig, 256) != 0) {
    free(ch_record);
    fprintf(stderr, "[tls13] RSA sign failed\n");
    return false;
  }

  uint8_t cv[512];
  uint8_t *cvp = cv;
  *cvp++ = 0x0f; /* CertificateVerify */
  uint8_t *cv_len_ptr = cvp;
  cvp += 3;
  uint8_t *cv_start = cvp;
  *cvp++ = 0x08;
  *cvp++ = 0x04; /* rsa_pss_rsae_sha256 */
  uint8_t *sig_len_ptr = cvp;
  cvp += 2;
  memcpy(cvp, cv_sig, 256);
  cvp += 256;
  sig_len_ptr[0] = (uint8_t)(256 >> 8);
  sig_len_ptr[1] = (uint8_t)256;
  size_t cv_body_len = (size_t)(cvp - cv_start);
  cv_len_ptr[0] = (uint8_t)(cv_body_len >> 16);
  cv_len_ptr[1] = (uint8_t)(cv_body_len >> 8);
  cv_len_ptr[2] = (uint8_t)cv_body_len;

  /* Update transcript with EE, Certificate, and CertificateVerify
   * BEFORE computing ServerFinished.  ctx already contains EE and
   * Certificate from the CertificateVerify signing step above. */
  gc_sha256_update(&ctx, cv, 4 + cv_body_len);
  tmp_ctx = ctx;
  gc_sha256_final(&tmp_ctx, srv->transcript_hash);

  /* Server Finished */
  uint8_t finished_key[32];
  tls13_derive_secret(srv->server_handshake_traffic_secret, "finished", NULL, 0,
                      finished_key, 32);
  uint8_t sf_verify[32];
  gc_hmac_sha256(finished_key, 32, srv->transcript_hash, 32, sf_verify);
  explicit_bzero(finished_key, sizeof(finished_key));

  uint8_t sf[40];
  sf[0] = 0x14; /* Finished */
  sf[1] = 0x00;
  sf[2] = 0x00;
  sf[3] = 0x20;
  memcpy(sf + 4, sf_verify, 32);

  /* Combine flight */
  uint8_t flight[4096];
  size_t flen = 0;
  memcpy(flight + flen, ee, ee_len);
  flen += ee_len;
  memcpy(flight + flen, cert_msg, 4 + cert_msg_len);
  flen += 4 + cert_msg_len;
  memcpy(flight + flen, cv, 4 + cv_body_len);
  flen += 4 + cv_body_len;
  memcpy(flight + flen, sf, 36);
  flen += 36;

  /* Encrypt and send flight */
  uint8_t enc_record[5 + TLS_MAX_PLAINTEXT];
  size_t enc_len =
      gc_tls13_server_encrypt(srv, CONTENT_TYPE_HANDSHAKE, flight, flen,
                              enc_record, sizeof(enc_record));
  if (enc_len == 0 || !send_all(fd, enc_record, enc_len)) {
    free(ch_record);
    fprintf(stderr, "[tls13] send encrypted flight failed\n");
    return false;
  }

  /* Update transcript with Finished (for ClientFinished verify and app keys) */
  gc_sha256_update(&ctx, sf, 36);
  tmp_ctx = ctx;
  gc_sha256_final(&tmp_ctx, srv->transcript_hash);

  /* 9. Read Client Finished */
  uint8_t *cf_record = NULL;
  size_t cf_rlen = 0;
  if (!read_tls_record(fd, &cf_record, &cf_rlen)) {
    free(ch_record);
    fprintf(stderr, "[tls13] read ClientFinished failed\n");
    return false;
  }
  if (cf_rlen < 5 || cf_record[0] != CONTENT_TYPE_APPDATA) {
    free(cf_record);
    free(ch_record);
    fprintf(stderr, "[tls13] ClientFinished not appdata\n");
    return false;
  }

  uint8_t cf_plain[256];
  size_t cf_plen = gc_tls13_server_decrypt(srv, cf_record, cf_rlen, cf_plain,
                                           sizeof(cf_plain), NULL);
  free(cf_record);
  if (cf_plen == (size_t)-1 || cf_plen < 36 || cf_plain[0] != 0x14) {
    free(ch_record);
    fprintf(stderr, "[tls13] ClientFinished decrypt/verify failed\n");
    return false;
  }

  /* Verify client finished */
  uint8_t cf_verify[32];
  memcpy(cf_verify, cf_plain + 4, 32);
  uint8_t cf_finished_key[32];
  tls13_derive_secret(srv->client_handshake_traffic_secret, "finished", NULL, 0,
                      cf_finished_key, 32);
  uint8_t cf_expected[32];
  gc_hmac_sha256(cf_finished_key, 32, srv->transcript_hash, 32, cf_expected);
  explicit_bzero(cf_finished_key, sizeof(cf_finished_key));
  if (memcmp(cf_verify, cf_expected, 32) != 0) {
    free(ch_record);
    fprintf(stderr, "[tls13] ClientFinished HMAC mismatch\n");
    return false;
  }

  /* Derive application keys BEFORE adding ClientFinished to transcript.
   * RFC 8446: application secrets use
   * Transcript-Hash(ClientHello..ServerFinished). The Client Finished is NOT
   * included in this hash. */
  tls13_derive_secret(srv->handshake_secret, "derived", empty_hash, 32,
                      derived_secret, 32);
  uint8_t master_secret[32];
  gc_hkdf_sha256_extract(derived_secret, 32, zeros, 32, master_secret);
  tls13_derive_secret(master_secret, "c ap traffic", srv->transcript_hash, 32,
                      srv->client_app_traffic_secret, 32);
  tls13_derive_secret(master_secret, "s ap traffic", srv->transcript_hash, 32,
                      srv->server_app_traffic_secret, 32);
  tls13_derive_secret(srv->client_app_traffic_secret, "key", NULL, 0,
                      srv->client_write_key, 32);
  tls13_derive_secret(srv->client_app_traffic_secret, "iv", NULL, 0,
                      srv->client_write_iv, 12);
  tls13_derive_secret(srv->server_app_traffic_secret, "key", NULL, 0,
                      srv->server_write_key, 32);
  tls13_derive_secret(srv->server_app_traffic_secret, "iv", NULL, 0,
                      srv->server_write_iv, 12);
  srv->client_seq = 0;
  srv->server_seq = 0;
  srv->handshake_complete = true;

  /* NOW add ClientFinished to transcript for future use (resumption, etc.) */
  gc_sha256_update(&ctx, cf_plain, 36);
  tmp_ctx = ctx;
  gc_sha256_final(&tmp_ctx, srv->transcript_hash);

  free(ch_record);
  fprintf(stderr, "[tls13] handshake complete\n");
  return true;
}

/* -------------------------------------------------------------------------- */
/* TLS 1.3 Client Handshake                                                   */
/* -------------------------------------------------------------------------- */

static size_t client_build_hello(gc_tls13_client_t *cli, const char *sni,
                                 uint8_t *out, size_t out_cap) {
  if (out_cap < 1024)
    return 0;
  uint8_t *p = out;
  *p++ = 0x01; /* ClientHello */
  uint8_t *len_ptr = p;
  p += 3;
  uint8_t *payload_start = p;

  *p++ = 0x03;
  *p++ = 0x03; /* TLS 1.2 compat version */

  /* Random */
  if (getrandom(p, 32, 0) != 32) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (size_t i = 0; i < 32; i++)
      p[i] = (uint8_t)(cli->client_pub[i] ^ (size_t)ts.tv_nsec ^ i);
  }
  p += 32;

  /* Session ID */
  *p++ = 0x00;

  /* Cipher suites: ChaCha20-Poly1305-SHA256 (verified working with Kimi API) */
  *p++ = 0x00;
  *p++ = 0x02;
  *p++ = 0x13;
  *p++ = 0x03;

  /* Compression: null */
  *p++ = 0x01;
  *p++ = 0x00;

  /* Extensions */
  uint8_t *ext_len_ptr = p;
  p += 2;
  uint8_t *ext_start = p;

  /* supported_versions */
  memcpy(p, tls13_supported_versions_ext, sizeof(tls13_supported_versions_ext));
  p += sizeof(tls13_supported_versions_ext);

  /* key_share (X25519) */
  memcpy(p, tls13_key_share_ext_header, 4);
  p += 4;
  uint8_t *ks_len_ptr = p;
  p += 2;
  uint8_t *ks_start = p;
  memcpy(p, tls13_x25519_group, 2);
  p += 2;
  *p++ = 0x00;
  *p++ = 0x20;
  memcpy(p, cli->client_pub, 32);
  p += 32;
  size_t ks_len = (size_t)(p - ks_start);
  ks_len_ptr[0] = (uint8_t)(ks_len >> 8);
  ks_len_ptr[1] = (uint8_t)ks_len;

  /* signature_algorithms */
  *p++ = 0x00;
  *p++ = 0x0d;
  *p++ = 0x00;
  *p++ = 0x08;
  *p++ = 0x00;
  *p++ = 0x06;
  *p++ = 0x08;
  *p++ = 0x04; /* rsa_pss_rsae_sha256 */
  *p++ = 0x04;
  *p++ = 0x03; /* ecdsa_secp256r1_sha256 */
  *p++ = 0x08;
  *p++ = 0x07; /* ed25519 */

  /* supported_groups */
  *p++ = 0x00;
  *p++ = 0x0a;
  *p++ = 0x00;
  *p++ = 0x04;
  *p++ = 0x00;
  *p++ = 0x02;
  *p++ = 0x00;
  *p++ = 0x1d; /* X25519 */

  /* SNI (server_name) */
  if (sni && sni[0]) {
    size_t sni_len = strlen(sni);
    if (sni_len > 255)
      sni_len = 255;
    *p++ = 0x00;
    *p++ = 0x00; /* extension type: server_name */
    uint8_t *sni_ext_len_ptr = p;
    p += 2;
    uint8_t *sni_ext_start = p;
    uint8_t *sni_list_len_ptr = p;
    p += 2;
    *p++ = 0x00; /* name_type: host_name */
    *p++ = (uint8_t)(sni_len >> 8);
    *p++ = (uint8_t)sni_len;
    memcpy(p, sni, sni_len);
    p += sni_len;
    size_t sni_list_len = (size_t)(p - sni_list_len_ptr - 2);
    sni_list_len_ptr[0] = (uint8_t)(sni_list_len >> 8);
    sni_list_len_ptr[1] = (uint8_t)sni_list_len;
    size_t sni_ext_len = (size_t)(p - sni_ext_start);
    sni_ext_len_ptr[0] = (uint8_t)(sni_ext_len >> 8);
    sni_ext_len_ptr[1] = (uint8_t)sni_ext_len;
  }

  /* ALPN: offer http/1.1 (account.jagex.com also supports h2, but the
   * simple HTTPS helper speaks HTTP/1.1) */
  *p++ = 0x00;
  *p++ = 0x10; /* extension type: application_layer_protocol_negotiation */
  *p++ = 0x00;
  *p++ = 0x0b; /* extension length */
  *p++ = 0x00;
  *p++ = 0x09; /* protocol list length */
  *p++ = 0x08; /* protocol name length */
  memcpy(p, "http/1.1", 8);
  p += 8;

  size_t ext_len = (size_t)(p - ext_start);
  ext_len_ptr[0] = (uint8_t)(ext_len >> 8);
  ext_len_ptr[1] = (uint8_t)ext_len;

  size_t payload_len = (size_t)(p - payload_start);
  len_ptr[0] = (uint8_t)(payload_len >> 16);
  len_ptr[1] = (uint8_t)(payload_len >> 8);
  len_ptr[2] = (uint8_t)payload_len;

  return (size_t)(p - out);
}

static bool client_parse_server_hello(gc_tls13_client_t *cli,
                                      const uint8_t *data, size_t len) {
  if (len < 6)
    return false;
  if (data[0] != 0x02)
    return false;
  size_t payload_len =
      ((size_t)data[1] << 16) | ((size_t)data[2] << 8) | data[3];
  if (len < 4 + payload_len)
    return false;
  const uint8_t *p = data + 4;
  const uint8_t *end = p + payload_len;

  if (end - p < 2)
    return false;
  p += 2; /* version */
  if (end - p < 32)
    return false;
  p += 32; /* random */
  if (end - p < 1)
    return false;
  size_t sid_len = *p++;
  if ((size_t)(end - p) < sid_len)
    return false;
  p += sid_len;
  if (end - p < 2)
    return false;
  uint16_t cipher = ((uint16_t)p[0] << 8) | p[1];
  p += 2;
  if (cipher != 0x1302 && cipher != 0x1303)
    return false;
  cli->cipher_suite = cipher;
  if (end - p < 1)
    return false;
  p++; /* compression */
  if (end - p < 2)
    return false;
  size_t ext_len = ((size_t)p[0] << 8) | p[1];
  p += 2;
  if ((size_t)(end - p) < ext_len)
    return false;

  const uint8_t *ext_end = p + ext_len;
  bool found_key = false;
  bool alpn_ok = false;
  while (p + 4 <= ext_end) {
    uint16_t etype = ((uint16_t)p[0] << 8) | p[1];
    uint16_t elen = ((uint16_t)p[2] << 8) | p[3];
    p += 4;
    if (p + elen > ext_end)
      return false;
    if (etype == 0x0033 && elen >= 34) {
      uint16_t group = ((uint16_t)p[0] << 8) | p[1];
      if (group == 0x001d && elen >= 36) {
        uint16_t kex_len = ((uint16_t)p[2] << 8) | p[3];
        if (kex_len == 32) {
          memcpy(cli->server_pub, p + 4, 32);
          found_key = true;
        }
      }
    } else if (etype == 0x0010 && elen >= 5) { /* ALPN */
      uint16_t list_len = ((uint16_t)p[0] << 8) | p[1];
      if (list_len >= 3 && p[2] == 0x02 && p[3] == 'h' && p[4] == '2') {
        alpn_ok = true;
      }
    }
    p += elen;
  }
  if (!found_key)
    return false;
  (void)alpn_ok; /* h2 negotiated if true */

  gc_x25519_shared_secret(cli->shared_secret, cli->client_priv,
                          cli->server_pub);
  return true;
}

static void client_derive_handshake_keys(gc_tls13_client_t *cli) {
  if (cli->cipher_suite == 0x1302) {
    uint8_t zeros[48] = {0};
    uint8_t early_secret[48];
    gc_hkdf_sha384_extract(NULL, 0, zeros, 48, early_secret);
    uint8_t empty_hash[48];
    gc_sha384((const uint8_t *)"", 0, empty_hash);
    uint8_t derived_secret[48];
    tls13_derive_secret384(early_secret, "derived", empty_hash, 48,
                           derived_secret, 48);
    gc_hkdf_sha384_extract(derived_secret, 48, cli->shared_secret, 32,
                           cli->handshake_secret);
    tls13_derive_secret384(cli->handshake_secret, "c hs traffic",
                           cli->transcript_hash, 48,
                           cli->client_handshake_traffic_secret, 48);
    tls13_derive_secret384(cli->handshake_secret, "s hs traffic",
                           cli->transcript_hash, 48,
                           cli->server_handshake_traffic_secret, 48);
    tls13_derive_secret384(cli->client_handshake_traffic_secret, "key", NULL, 0,
                           cli->client_write_key, 32);
    tls13_derive_secret384(cli->client_handshake_traffic_secret, "iv", NULL, 0,
                           cli->client_write_iv, 12);
    tls13_derive_secret384(cli->server_handshake_traffic_secret, "key", NULL, 0,
                           cli->server_write_key, 32);
    tls13_derive_secret384(cli->server_handshake_traffic_secret, "iv", NULL, 0,
                           cli->server_write_iv, 12);
  } else {
    uint8_t zeros[32] = {0};
    uint8_t early_secret[32];
    gc_hkdf_sha256_extract(NULL, 0, zeros, 32, early_secret);
    uint8_t empty_hash[32];
    gc_sha256((const uint8_t *)"", 0, empty_hash);
    uint8_t derived_secret[32];
    tls13_derive_secret(early_secret, "derived", empty_hash, 32, derived_secret,
                        32);
    gc_hkdf_sha256_extract(derived_secret, 32, cli->shared_secret, 32,
                           cli->handshake_secret);
    tls13_derive_secret(cli->handshake_secret, "c hs traffic",
                        cli->transcript_hash, 32,
                        cli->client_handshake_traffic_secret, 32);
    tls13_derive_secret(cli->handshake_secret, "s hs traffic",
                        cli->transcript_hash, 32,
                        cli->server_handshake_traffic_secret, 32);
    tls13_derive_secret(cli->client_handshake_traffic_secret, "key", NULL, 0,
                        cli->client_write_key, 32);
    tls13_derive_secret(cli->client_handshake_traffic_secret, "iv", NULL, 0,
                        cli->client_write_iv, 12);
    tls13_derive_secret(cli->server_handshake_traffic_secret, "key", NULL, 0,
                        cli->server_write_key, 32);
    tls13_derive_secret(cli->server_handshake_traffic_secret, "iv", NULL, 0,
                        cli->server_write_iv, 12);
  }
  cli->client_seq = 0;
  cli->server_seq = 0;
  cli->handshake_complete = false;
}

static void client_derive_app_keys(gc_tls13_client_t *cli) {
  if (cli->cipher_suite == 0x1302) {
    uint8_t empty_hash[48];
    gc_sha384((const uint8_t *)"", 0, empty_hash);
    uint8_t derived_secret[48];
    tls13_derive_secret384(cli->handshake_secret, "derived", empty_hash, 48,
                           derived_secret, 48);
    uint8_t zeros[48] = {0};
    uint8_t master_secret[48];
    gc_hkdf_sha384_extract(derived_secret, 48, zeros, 48, master_secret);
    tls13_derive_secret384(master_secret, "c ap traffic", cli->transcript_hash,
                           48, cli->client_app_traffic_secret, 48);
    tls13_derive_secret384(master_secret, "s ap traffic", cli->transcript_hash,
                           48, cli->server_app_traffic_secret, 48);
    tls13_derive_secret384(cli->client_app_traffic_secret, "key", NULL, 0,
                           cli->client_write_key, 32);
    tls13_derive_secret384(cli->client_app_traffic_secret, "iv", NULL, 0,
                           cli->client_write_iv, 12);
    tls13_derive_secret384(cli->server_app_traffic_secret, "key", NULL, 0,
                           cli->server_write_key, 32);
    tls13_derive_secret384(cli->server_app_traffic_secret, "iv", NULL, 0,
                           cli->server_write_iv, 12);
  } else {
    uint8_t empty_hash[32];
    gc_sha256((const uint8_t *)"", 0, empty_hash);
    uint8_t derived_secret[32];
    tls13_derive_secret(cli->handshake_secret, "derived", empty_hash, 32,
                        derived_secret, 32);
    uint8_t zeros[32] = {0};
    uint8_t master_secret[32];
    gc_hkdf_sha256_extract(derived_secret, 32, zeros, 32, master_secret);
    tls13_derive_secret(master_secret, "c ap traffic", cli->transcript_hash, 32,
                        cli->client_app_traffic_secret, 32);
    tls13_derive_secret(master_secret, "s ap traffic", cli->transcript_hash, 32,
                        cli->server_app_traffic_secret, 32);
    tls13_derive_secret(cli->client_app_traffic_secret, "key", NULL, 0,
                        cli->client_write_key, 32);
    tls13_derive_secret(cli->client_app_traffic_secret, "iv", NULL, 0,
                        cli->client_write_iv, 12);
    tls13_derive_secret(cli->server_app_traffic_secret, "key", NULL, 0,
                        cli->server_write_key, 32);
    tls13_derive_secret(cli->server_app_traffic_secret, "iv", NULL, 0,
                        cli->server_write_iv, 12);
  }
  cli->client_seq = 0;
  cli->server_seq = 0;
  cli->handshake_complete = true;
}

static bool client_verify_server_finished(gc_tls13_client_t *cli,
                                          const uint8_t *verify_data,
                                          size_t len) {
  if (cli->cipher_suite == 0x1302) {
    if (len != 48)
      return false;
    uint8_t finished_key[48];
    tls13_derive_secret384(cli->server_handshake_traffic_secret, "finished",
                           NULL, 0, finished_key, 48);
    uint8_t mac[48];
    gc_hmac_sha384(finished_key, 48, cli->transcript_hash, 48, mac);
    explicit_bzero(finished_key, sizeof(finished_key));
    return (memcmp(mac, verify_data, 48) == 0);
  } else {
    if (len != 32)
      return false;
    uint8_t finished_key[32];
    tls13_derive_secret(cli->server_handshake_traffic_secret, "finished", NULL,
                        0, finished_key, 32);
    uint8_t mac[32];
    gc_hmac_sha256(finished_key, 32, cli->transcript_hash, 32, mac);
    explicit_bzero(finished_key, sizeof(finished_key));
    return (memcmp(mac, verify_data, 32) == 0);
  }
}

static void client_build_finished(gc_tls13_client_t *cli, uint8_t *out) {
  if (cli->cipher_suite == 0x1302) {
    uint8_t finished_key[48];
    tls13_derive_secret384(cli->client_handshake_traffic_secret, "finished",
                           NULL, 0, finished_key, 48);
    gc_hmac_sha384(finished_key, 48, cli->transcript_hash, 48, out);
    explicit_bzero(finished_key, sizeof(finished_key));
  } else {
    uint8_t finished_key[32];
    tls13_derive_secret(cli->client_handshake_traffic_secret, "finished", NULL,
                        0, finished_key, 32);
    gc_hmac_sha256(finished_key, 32, cli->transcript_hash, 32, out);
    explicit_bzero(finished_key, sizeof(finished_key));
  }
}

bool gc_tls13_client_handshake(gc_tls13_client_t *cli, int fd,
                               const char *sni) {
  memset(cli, 0, sizeof(*cli));

  /* 1. Generate ephemeral keypair */
  uint8_t entropy[32];
  ssize_t n = getrandom(entropy, sizeof(entropy), 0);
  if (n != (ssize_t)sizeof(entropy)) {
    int fd_r = open("/dev/urandom", O_RDONLY);
    if (fd_r >= 0) {
      ssize_t _r = read(fd_r, entropy, 32);
      (void)_r;
      close(fd_r);
    }
  }
  gc_x25519_gen_private(cli->client_priv, entropy);
  gc_x25519_public_from_private(cli->client_pub, cli->client_priv);
  explicit_bzero(entropy, sizeof(entropy));

  /* 2. Build ClientHello */
  uint8_t ch[1024];
  size_t ch_len = client_build_hello(cli, sni, ch, sizeof(ch));
  if (ch_len == 0) {
    fprintf(stderr, "[tls13-client] build ClientHello failed\n");
    return false;
  }

  /* 3. Send ClientHello */
  uint8_t ch_record[5 + 1024];
  ch_record[0] = CONTENT_TYPE_HANDSHAKE;
  ch_record[1] = 0x03;
  ch_record[2] = 0x03;
  ch_record[3] = (uint8_t)(ch_len >> 8);
  ch_record[4] = (uint8_t)ch_len;
  memcpy(ch_record + 5, ch, ch_len);
  if (!send_all(fd, ch_record, 5 + ch_len)) {
    fprintf(stderr, "[tls13-client] send CH failed\n");
    return false;
  }

  /* 4. Read ServerHello */
  uint8_t *sh_record = NULL;
  size_t sh_rlen = 0;
  if (!read_tls_record(fd, &sh_record, &sh_rlen)) {
    fprintf(stderr, "[tls13-client] read SH failed\n");
    return false;
  }
  if (sh_rlen < 5 || sh_record[0] != CONTENT_TYPE_HANDSHAKE) {
    free(sh_record);
    fprintf(stderr, "[tls13-client] SH not handshake\n");
    return false;
  }
  uint16_t sh_payload_len = ((uint16_t)sh_record[3] << 8) | sh_record[4];
  if (!client_parse_server_hello(cli, sh_record + 5, sh_payload_len)) {
    free(sh_record);
    fprintf(stderr, "[tls13-client] parse SH failed\n");
    return false;
  }

  /* Determine hash function from cipher suite */
  bool use_sha384 = (cli->cipher_suite == 0x1302);
  size_t hash_len = use_sha384 ? 48 : 32;

  /* Initialize transcript hash with correct function and hash CH + SH */
  uint8_t transcript_buf[2048];
  size_t transcript_len = 0;
  memcpy(transcript_buf + transcript_len, ch, ch_len);
  transcript_len += ch_len;
  memcpy(transcript_buf + transcript_len, sh_record + 5, sh_payload_len);
  transcript_len += sh_payload_len;
  free(sh_record);

  gc_sha256_t ctx256;
  gc_sha384_t ctx384;
  if (use_sha384) {
    gc_sha384_init(&ctx384);
    gc_sha384_update(&ctx384, transcript_buf, transcript_len);
    gc_sha384_t tmp = ctx384;
    gc_sha384_final(&tmp, cli->transcript_hash);
  } else {
    gc_sha256_init(&ctx256);
    gc_sha256_update(&ctx256, transcript_buf, transcript_len);
    gc_sha256_t tmp = ctx256;
    gc_sha256_final(&tmp, cli->transcript_hash);
  }

  /* 5. Derive handshake keys */
  client_derive_handshake_keys(cli);

  /* 6. Read encrypted server flight */
  uint8_t flight_plain[TLS_MAX_PLAINTEXT * 4];
  size_t flight_len = 0;
  bool saw_finished = false;

  for (int rec = 0; rec < 16 && !saw_finished; rec++) {
    uint8_t *record = NULL;
    size_t rlen = 0;
    if (!read_tls_record(fd, &record, &rlen)) {
      fprintf(stderr, "[tls13-client] read encrypted record %d failed\n", rec);
      break;
    }

    if (rlen >= 5 && record[0] == CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
      fprintf(stderr, "[tls13-client] rec=%d ChangeCipherSpec, skipping\n",
              rec);
      free(record);
      continue;
    }
    if (rlen < 5 || record[0] != CONTENT_TYPE_APPDATA) {
      fprintf(stderr, "[tls13-client] unexpected record type %u at rec %d\n",
              record[0], rec);
      free(record);
      break;
    }

    fprintf(stderr, "[tls13-client] rec=%d type=%u len=%zu\n", rec, record[0],
            rlen);

    uint8_t plain[TLS_MAX_PLAINTEXT];
    size_t plen = tls13_decrypt_record(
        cli->server_write_key, cli->server_write_iv, &cli->server_seq,
        cli->cipher_suite, record, rlen, plain, sizeof(plain), NULL);
    free(record);
    if (plen == (size_t)-1) {
      fprintf(stderr, "[tls13-client] decrypt failed at rec %d (seq=%lu)\n",
              rec, (unsigned long)(cli->server_seq));
      return false;
    }

    if (flight_len + plen > sizeof(flight_plain))
      return false;
    memcpy(flight_plain + flight_len, plain, plen);
    flight_len += plen;

    /* Scan for Finished */
    size_t scan = 0;
    while (scan + 4 <= flight_len) {
      uint8_t msg_type = flight_plain[scan];
      size_t msg_len = ((size_t)flight_plain[scan + 1] << 16) |
                       ((size_t)flight_plain[scan + 2] << 8) |
                       flight_plain[scan + 3];
      size_t msg_total = 4 + msg_len;
      if (scan + msg_total > flight_len)
        break;
      if (msg_type == 0x14 && msg_len == hash_len) {
        saw_finished = true;
        break;
      }
      scan += msg_total;
    }
  }

  if (flight_len == 0 || !saw_finished) {
    fprintf(stderr, "[tls13-client] no Finished seen\n");
    return false;
  }

  /* 7. Parse flight, update transcript, verify Finished */
  size_t off = 0;
  while (off < flight_len) {
    if (off + 4 > flight_len)
      break;
    uint8_t msg_type = flight_plain[off];
    size_t msg_len = ((size_t)flight_plain[off + 1] << 16) |
                     ((size_t)flight_plain[off + 2] << 8) |
                     flight_plain[off + 3];
    size_t msg_total = 4 + msg_len;
    if (off + msg_total > flight_len)
      break;

    if (msg_type == 0x14 && msg_len == hash_len) {
      uint8_t verify_data[48];
      memcpy(verify_data, flight_plain + off + 4, hash_len);
      if (!client_verify_server_finished(cli, verify_data, hash_len)) {
        fprintf(stderr, "[tls13-client] verify_server_finished failed\n");
        explicit_bzero(verify_data, sizeof(verify_data));
        return false;
      }
      explicit_bzero(verify_data, sizeof(verify_data));
      if (use_sha384) {
        gc_sha384_update(&ctx384, flight_plain + off, msg_total);
        gc_sha384_t tmp = ctx384;
        gc_sha384_final(&tmp, cli->transcript_hash);
      } else {
        gc_sha256_update(&ctx256, flight_plain + off, msg_total);
        gc_sha256_t tmp = ctx256;
        gc_sha256_final(&tmp, cli->transcript_hash);
      }
      break;
    } else {
      if (use_sha384) {
        gc_sha384_update(&ctx384, flight_plain + off, msg_total);
        gc_sha384_t tmp = ctx384;
        gc_sha384_final(&tmp, cli->transcript_hash);
      } else {
        gc_sha256_update(&ctx256, flight_plain + off, msg_total);
        gc_sha256_t tmp = ctx256;
        gc_sha256_final(&tmp, cli->transcript_hash);
      }
    }
    off += msg_total;
  }

  /* 8. Build ClientFinished */
  uint8_t finished_data[48];
  client_build_finished(cli, finished_data);

  uint8_t finished_hs[52];
  finished_hs[0] = 0x14;
  finished_hs[1] = 0x00;
  finished_hs[2] = 0x00;
  finished_hs[3] = (uint8_t)hash_len;
  memcpy(finished_hs + 4, finished_data, hash_len);
  explicit_bzero(finished_data, sizeof(finished_data));

  /* Save handshake keys — ClientFinished must be encrypted with them */
  uint8_t hs_client_key[32];
  uint8_t hs_client_iv[12];
  memcpy(hs_client_key, cli->client_write_key, 32);
  memcpy(hs_client_iv, cli->client_write_iv, 12);
  uint64_t hs_client_seq = cli->client_seq;

  /* 9. Derive application keys BEFORE adding ClientFinished to transcript */
  client_derive_app_keys(cli);

  /* 10. Send ClientFinished encrypted with HANDSHAKE keys */
  uint8_t cf_record[5 + TLS_MAX_PLAINTEXT];
  size_t cf_rlen = tls13_encrypt_record(
      hs_client_key, hs_client_iv, &hs_client_seq, cli->cipher_suite,
      CONTENT_TYPE_HANDSHAKE, finished_hs, 4 + hash_len, cf_record,
      sizeof(cf_record));
  explicit_bzero(hs_client_key, sizeof(hs_client_key));
  explicit_bzero(hs_client_iv, sizeof(hs_client_iv));
  if (cf_rlen == 0 || !send_all(fd, cf_record, cf_rlen)) {
    fprintf(stderr, "[tls13-client] send CF failed\n");
    return false;
  }

  /* NOW add ClientFinished to transcript */
  if (use_sha384) {
    gc_sha384_update(&ctx384, finished_hs, 4 + hash_len);
    gc_sha384_t tmp = ctx384;
    gc_sha384_final(&tmp, cli->transcript_hash);
  } else {
    gc_sha256_update(&ctx256, finished_hs, 4 + hash_len);
    gc_sha256_t tmp = ctx256;
    gc_sha256_final(&tmp, cli->transcript_hash);
  }
  explicit_bzero(finished_hs, sizeof(finished_hs));

  fprintf(stderr, "[tls13-client] handshake complete\n");
  return true;
}

/* -------------------------------------------------------------------------- */
/* Client blocking send/recv helpers                                          */
/* -------------------------------------------------------------------------- */

bool gc_tls13_client_send(gc_tls13_client_t *cli, int fd, const uint8_t *data,
                          size_t len) {
  if (!cli || !cli->handshake_complete)
    return false;
  size_t off = 0;
  while (off < len) {
    size_t chunk = len - off;
    if (chunk > TLS_MAX_PLAINTEXT)
      chunk = TLS_MAX_PLAINTEXT;
    uint8_t record[5 + TLS_MAX_PLAINTEXT + 1 + 16];
    size_t rlen = tls13_encrypt_record(
        cli->client_write_key, cli->client_write_iv, &cli->client_seq,
        cli->cipher_suite, CONTENT_TYPE_APPDATA, data + off, chunk, record,
        sizeof(record));
    if (rlen == 0)
      return false;
    if (!send_all(fd, record, rlen))
      return false;
    off += chunk;
  }
  return true;
}

ssize_t gc_tls13_client_recv(gc_tls13_client_t *cli, int fd, uint8_t *out,
                             size_t out_cap) {
  if (!cli || !cli->handshake_complete)
    return -1;

  for (;;) {
    uint8_t hdr[5];
    if (!recv_exact(fd, hdr, 5))
      return 0; /* treat as EOF */
    uint16_t payload_len = ((uint16_t)hdr[3] << 8) | hdr[4];
    if (payload_len > TLS_MAX_PLAINTEXT + 256)
      return -1;

    /* Skip unencrypted ChangeCipherSpec records (TLS 1.3 middlebox compat) */
    if (hdr[0] == CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
      uint8_t skip[1];
      if (!recv_exact(fd, skip, 1))
        return -1;
      continue;
    }

    uint8_t *record = malloc(5 + payload_len);
    if (!record)
      return -1;
    memcpy(record, hdr, 5);
    if (!recv_exact(fd, record + 5, payload_len)) {
      free(record);
      return -1;
    }

    uint8_t content_type = 0;
    size_t plen =
        tls13_decrypt_record(cli->server_write_key, cli->server_write_iv,
                             &cli->server_seq, cli->cipher_suite, record,
                             5 + payload_len, out, out_cap, &content_type);
    free(record);
    if (plen == (size_t)-1)
      return -1;
    if (content_type == 21)
      return 0; /* close_notify = EOF */
    if (content_type == 22)
      continue; /* skip post-handshake messages */
    return (ssize_t)plen;
  }
}

/* -------------------------------------------------------------------------- */
/* Base64 decoder                                                             */
/* -------------------------------------------------------------------------- */

static int base64_decode_char(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

static size_t base64_decode(const char *in, size_t in_len, uint8_t *out,
                            size_t out_cap) {
  size_t out_len = 0;
  uint32_t buf = 0;
  int bits = 0;
  for (size_t i = 0; i < in_len; i++) {
    char c = in[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      continue;
    if (c == '=')
      break;
    int v = base64_decode_char(c);
    if (v < 0)
      return (size_t)-1;
    buf = (buf << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out_len >= out_cap)
        return (size_t)-1;
      out[out_len++] = (uint8_t)(buf >> bits);
      buf &= ((uint32_t)1 << bits) - 1;
    }
  }
  return out_len;
}

/* -------------------------------------------------------------------------- */
/* PEM parser                                                                 */
/* -------------------------------------------------------------------------- */

static bool parse_pem_block(const char *data, size_t len,
                            const char *begin_marker, const char *end_marker,
                            uint8_t *out, size_t out_cap, size_t *out_len) {
  (void)len;
  const char *begin = strstr(data, begin_marker);
  if (!begin)
    return false;
  begin += strlen(begin_marker);
  const char *end = strstr(begin, end_marker);
  if (!end)
    return false;
  size_t b64_len = (size_t)(end - begin);
  size_t decoded = base64_decode(begin, b64_len, out, out_cap);
  if (decoded == (size_t)-1)
    return false;
  *out_len = decoded;
  return true;
}

/* -------------------------------------------------------------------------- */
/* Cert/Key loaders                                                           */
/* -------------------------------------------------------------------------- */

bool gc_tls13_load_cert_pem(gc_tls13_server_t *srv, const char *path) {
  (void)srv; /* cert is module-global, used by all handshakes */
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  char pem[4096];
  ssize_t n = read(fd, pem, sizeof(pem) - 1);
  close(fd);
  if (n <= 0)
    return false;
  pem[n] = '\0';
  size_t decoded_len = 0;
  if (!parse_pem_block(pem, (size_t)n, "-----BEGIN CERTIFICATE-----",
                       "-----END CERTIFICATE-----", g_loaded_cert,
                       sizeof(g_loaded_cert), &decoded_len))
    return false;
  g_loaded_cert_len = decoded_len;
  g_has_loaded_cert = true;
  return true;
}

bool gc_tls13_load_key_raw(gc_tls13_server_t *srv, const char *path) {
  (void)srv;
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  uint8_t buf[1024];
  ssize_t n = read(fd, buf, sizeof(buf));
  close(fd);
  if (n < 4)
    return false;
  size_t n_len = ((size_t)buf[0] << 8) | buf[1];
  size_t d_len = (size_t)n - 2 - n_len;
  if (n_len > sizeof(g_loaded_n) || d_len > sizeof(g_loaded_d) || d_len == 0)
    return false;
  memcpy(g_loaded_n, buf + 2, n_len);
  g_loaded_n_len = n_len;
  memcpy(g_loaded_d, buf + 2 + n_len, d_len);
  g_loaded_d_len = d_len;
  g_has_loaded_key = true;
  return true;
}

bool gc_tls13_has_cert(const gc_tls13_server_t *srv) {
  (void)srv;
  return g_has_loaded_cert;
}

bool gc_tls13_has_key(const gc_tls13_server_t *srv) {
  (void)srv;
  return g_has_loaded_key;
}

/* -------------------------------------------------------------------------- */
/* TLS connection wrappers (non-blocking I/O)                                 */
/* -------------------------------------------------------------------------- */

bool gc_tls13_conn_handshake(gc_tls13_conn_t *conn, int fd) {
  if (!conn)
    return false;
  memset(conn, 0, sizeof(*conn));
  conn->fd = fd;
  if (!gc_tls13_server_handshake(&conn->srv, fd))
    return false;
  conn->handshake_done = true;
  return true;
}

ssize_t gc_tls13_conn_recv(gc_tls13_conn_t *conn, uint8_t *out,
                           size_t out_cap) {
  if (!conn || !conn->handshake_done)
    return -1;

  /* Read raw bytes into TLS read buffer */
  if (conn->read_len < sizeof(conn->read_buf)) {
    ssize_t n = recv(conn->fd, conn->read_buf + conn->read_len,
                     sizeof(conn->read_buf) - conn->read_len, 0);
    if (n > 0)
      conn->read_len += (size_t)n;
  }

  /* Try to parse a complete TLS record */
  if (conn->read_len < 5)
    return 0; /* need more header */

  uint16_t payload_len = ((uint16_t)conn->read_buf[3] << 8) | conn->read_buf[4];
  size_t record_len = 5 + payload_len;
  if (record_len > sizeof(conn->read_buf))
    return -1; /* record too big */
  if (conn->read_len < record_len)
    return 0; /* need more payload */

  /* Skip unencrypted ChangeCipherSpec records (TLS 1.3 middlebox compat) */
  if (conn->read_buf[0] == CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
    size_t remaining = conn->read_len - record_len;
    if (remaining > 0)
      memmove(conn->read_buf, conn->read_buf + record_len, remaining);
    conn->read_len = remaining;
    return 0; /* no application data yet */
  }

  /* Decrypt the record */
  uint8_t plain[TLS_MAX_PLAINTEXT];
  uint8_t content_type = 0;
  size_t plen = gc_tls13_server_decrypt(&conn->srv, conn->read_buf, record_len,
                                        plain, sizeof(plain), &content_type);
  if (plen == (size_t)-1)
    return -1;

  /* Shift remaining read buffer */
  size_t remaining = conn->read_len - record_len;
  if (remaining > 0)
    memmove(conn->read_buf, conn->read_buf + record_len, remaining);
  conn->read_len = remaining;

  if (content_type == 21) {
    /* Alert - treat as error/EOF */
    return -1;
  }
  if (content_type != 23 && content_type != 22) {
    /* Not application data or handshake - skip but return 0 for now */
    return 0;
  }

  size_t to_copy = plen < out_cap ? plen : out_cap;
  memcpy(out, plain, to_copy);
  return (ssize_t)to_copy;
}

ssize_t gc_tls13_conn_send(gc_tls13_conn_t *conn, const uint8_t *plaintext,
                           size_t pt_len) {
  if (!conn || !conn->handshake_done)
    return -1;

  /* If there are pending TLS records from a previous call, flush first */
  if (conn->write_len > 0) {
    ssize_t f = gc_tls13_conn_flush(conn);
    if (f < 0)
      return -1;
    if (conn->write_len > 0)
      return 0; /* still pending, can't take new plaintext */
  }

  /* Encrypt plaintext into write buffer */
  size_t off = 0;
  while (off < pt_len && conn->write_len < sizeof(conn->write_buf)) {
    size_t chunk = pt_len - off;
    if (chunk > TLS_MAX_PLAINTEXT)
      chunk = TLS_MAX_PLAINTEXT;
    size_t space = sizeof(conn->write_buf) - conn->write_len;
    if (space < 5 + chunk + 1 + 16)
      break; /* not enough space for record */

    size_t rlen = gc_tls13_server_encrypt(
        &conn->srv, CONTENT_TYPE_APPDATA, plaintext + off, chunk,
        conn->write_buf + conn->write_len, space);
    if (rlen == 0)
      break;
    conn->write_len += rlen;
    off += chunk;
  }

  /* Try to send encrypted data */
  while (conn->write_sent < conn->write_len) {
    ssize_t n = send(conn->fd, conn->write_buf + conn->write_sent,
                     conn->write_len - conn->write_sent, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR)
        break;
      return -1;
    }
    if (n == 0)
      break;
    conn->write_sent += (size_t)n;
  }

  /* If all sent, reset write buffer */
  if (conn->write_sent >= conn->write_len) {
    conn->write_len = 0;
    conn->write_sent = 0;
  } else if (conn->write_sent > 0) {
    /* Compact buffer */
    size_t remaining = conn->write_len - conn->write_sent;
    memmove(conn->write_buf, conn->write_buf + conn->write_sent, remaining);
    conn->write_len = remaining;
    conn->write_sent = 0;
  }

  return (ssize_t)off; /* return plaintext bytes encrypted */
}

ssize_t gc_tls13_conn_flush(gc_tls13_conn_t *conn) {
  if (!conn || !conn->handshake_done)
    return -1;
  if (conn->write_len == 0)
    return 0;

  ssize_t total = 0;
  while (conn->write_sent < conn->write_len) {
    ssize_t n = send(conn->fd, conn->write_buf + conn->write_sent,
                     conn->write_len - conn->write_sent, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR)
        break;
      return -1;
    }
    if (n == 0)
      break;
    conn->write_sent += (size_t)n;
    total += n;
  }

  if (conn->write_sent >= conn->write_len) {
    conn->write_len = 0;
    conn->write_sent = 0;
  } else if (conn->write_sent > 0) {
    size_t remaining = conn->write_len - conn->write_sent;
    memmove(conn->write_buf, conn->write_buf + conn->write_sent, remaining);
    conn->write_len = remaining;
    conn->write_sent = 0;
  }

  return total;
}

void gc_tls13_conn_close(gc_tls13_conn_t *conn) {
  if (!conn)
    return;
  if (conn->fd >= 0) {
    /* Try to send close_notify if handshake done */
    if (conn->handshake_done) {
      uint8_t alert[2] = {0x01, 0x00}; /* close_notify */
      uint8_t record[32];
      size_t rlen = gc_tls13_server_encrypt(&conn->srv, 21, alert, 2, record,
                                            sizeof(record));
      if (rlen > 0) {
        send(conn->fd, record, rlen, 0);
      }
    }
    close(conn->fd);
    conn->fd = -1;
  }
  explicit_bzero(conn, sizeof(*conn));
}
