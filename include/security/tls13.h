/*
 * ORDL GovCon - TLS 1.3 Server
 * RFC 8446. X25519 + ChaCha20-Poly1305 + SHA-256
 * Pure C23, zero dependencies
 */

#ifndef GOVCON_TLS13_H
#define GOVCON_TLS13_H

#include "core/compat.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_TLS13_KEY_LEN 32
#define GC_TLS13_IV_LEN  12

/* Cipher suites */
#define GC_TLS13_CIPHER_AES_256_GCM_SHA384 0x1302
#define GC_TLS13_CIPHER_CHACHA20_POLY1305_SHA256 0x1303

typedef struct {
    /* Key exchange */
    uint8_t server_priv[32];
    uint8_t server_pub[32];
    uint8_t client_pub[32];
    uint8_t shared_secret[32];

    /* Handshake secrets */
    uint8_t handshake_secret[48];  /* Max size for SHA-384 */
    uint8_t server_handshake_traffic_secret[48];
    uint8_t client_handshake_traffic_secret[48];

    /* Application secrets */
    uint8_t client_app_traffic_secret[48];
    uint8_t server_app_traffic_secret[48];

    /* Write keys and IVs */
    uint8_t client_write_key[32];
    uint8_t client_write_iv[12];
    uint8_t server_write_key[32];
    uint8_t server_write_iv[12];

    /* Sequence numbers */
    uint64_t client_seq;
    uint64_t server_seq;

    /* Transcript */
    uint8_t transcript_hash[48];  /* Max size for SHA-384 */

    /* State */
    bool handshake_complete;
    uint16_t cipher_suite;
} gc_tls13_server_t;

/* Server-side handshake. Receives ClientHello on fd, completes handshake. */
GC_NODISCARD bool gc_tls13_server_handshake(gc_tls13_server_t *srv, int fd);

/* Encrypt/decrypt TLS records */
GC_NODISCARD size_t gc_tls13_server_encrypt(gc_tls13_server_t *srv,
                                uint8_t content_type,
                                const uint8_t *plaintext, size_t pt_len,
                                uint8_t *out, size_t out_cap);

GC_NODISCARD size_t gc_tls13_server_decrypt(gc_tls13_server_t *srv,
                                const uint8_t *record, size_t record_len,
                                uint8_t *out, size_t out_cap,
                                uint8_t *out_content_type);

/* -------------------------------------------------------------------------- */
/* TLS 1.3 Client                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    /* Key exchange */
    uint8_t client_priv[32];
    uint8_t client_pub[32];
    uint8_t server_pub[32];
    uint8_t shared_secret[32];

    /* Handshake secrets */
    uint8_t handshake_secret[48];
    uint8_t server_handshake_traffic_secret[48];
    uint8_t client_handshake_traffic_secret[48];

    /* Application secrets */
    uint8_t client_app_traffic_secret[48];
    uint8_t server_app_traffic_secret[48];

    /* Write keys and IVs */
    uint8_t client_write_key[32];
    uint8_t client_write_iv[12];
    uint8_t server_write_key[32];
    uint8_t server_write_iv[12];

    /* Sequence numbers */
    uint64_t client_seq;
    uint64_t server_seq;

    /* Transcript */
    uint8_t transcript_hash[48];

    /* State */
    bool handshake_complete;
    uint16_t cipher_suite;
} gc_tls13_client_t;

/* Client-side handshake. Sends ClientHello, completes handshake.
 * If `sni` is non-null, includes Server Name Indication extension.
 */
GC_NODISCARD bool gc_tls13_client_handshake(gc_tls13_client_t *cli, int fd,
                                               const char *sni);

/* Blocking send of plaintext as TLS application data records. */
GC_NODISCARD bool gc_tls13_client_send(gc_tls13_client_t *cli, int fd,
                          const uint8_t *data, size_t len);

/* Blocking recv of decrypted application data.
 * Returns: >0 bytes read, 0 on EOF/close_notify, -1 on error. */
GC_NODISCARD ssize_t gc_tls13_client_recv(gc_tls13_client_t *cli, int fd,
                              uint8_t *out, size_t out_cap);

/* -------------------------------------------------------------------------- */
/* Certificate and key loading                                                */
/* -------------------------------------------------------------------------- */

/* Load certificate from PEM file. Returns true on success. */
GC_NODISCARD bool gc_tls13_load_cert_pem(gc_tls13_server_t *srv, const char *path);

/* Load RSA private key from raw binary file (n_len BE 2 bytes + n + d). */
GC_NODISCARD bool gc_tls13_load_key_raw(gc_tls13_server_t *srv, const char *path);

/* Get loaded cert/key for signing (used internally by handshake) */
GC_NODISCARD bool gc_tls13_has_cert(const gc_tls13_server_t *srv);
GC_NODISCARD bool gc_tls13_has_key(const gc_tls13_server_t *srv);

/* -------------------------------------------------------------------------- */
/* TLS connection wrappers (for non-blocking I/O integration)                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    gc_tls13_server_t srv;
    int fd;
    bool handshake_done;

    /* Read buffering for non-blocking TLS record reassembly */
    uint8_t read_buf[65536];
    size_t read_len;

    /* Write buffering for partial TLS record sends */
    uint8_t write_buf[65536];
    size_t write_len;
    size_t write_sent;
} gc_tls13_conn_t;

/* Perform blocking TLS handshake on connected fd. Returns true on success. */
GC_NODISCARD bool gc_tls13_conn_handshake(gc_tls13_conn_t *conn, int fd);

/* Read decrypted application data (non-blocking).
 * Returns: >0 bytes read, 0 if no complete record available, -1 on error.
 */
GC_NODISCARD ssize_t gc_tls13_conn_recv(gc_tls13_conn_t *conn, uint8_t *out, size_t out_cap);

/* Write plaintext as encrypted TLS records (non-blocking).
 * Returns: >0 bytes written from plaintext, -1 on error.
 * Note: encrypts all plaintext into write_buf, then sends as much as possible.
 */
GC_NODISCARD ssize_t gc_tls13_conn_send(gc_tls13_conn_t *conn,
                            const uint8_t *plaintext, size_t pt_len);

/* Continue sending buffered TLS records (call after gc_tls13_conn_send).
 * Returns number of TLS record bytes sent, or -1 on error.
 */
GC_NODISCARD ssize_t gc_tls13_conn_flush(gc_tls13_conn_t *conn);

/* Close TLS connection (sends close_notify if possible). */
void gc_tls13_conn_close(gc_tls13_conn_t *conn);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_TLS13_H */
