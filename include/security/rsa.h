/*
 * ORDL GovCon - Minimal Big Integer & RSA-PSS Signing
 * Just enough for TLS 1.3 CertificateVerify with RSA-PSS-SHA256
 * Pure C23, zero dependencies
 */

#ifndef GOVCON_RSA_H
#define GOVCON_RSA_H

#include "core/compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RSA-PSS-SHA256 sign. Returns 0 on success. */
GC_NODISCARD int gc_rsa_pss_sha256_sign(const uint8_t *tbs_hash,
                            const uint8_t *n, size_t n_len,
                            const uint8_t *d, size_t d_len,
                            uint8_t *sig_out, size_t sig_len);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_RSA_H */
