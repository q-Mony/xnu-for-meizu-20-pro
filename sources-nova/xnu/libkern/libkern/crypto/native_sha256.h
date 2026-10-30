/* Internal SHA-256 adapter for native signature preflight. */
#ifndef _LIBKERN_CRYPTO_NATIVE_SHA256_H_
#define _LIBKERN_CRYPTO_NATIVE_SHA256_H_

#include <sys/types.h>
#include <libkern/crypto/sha2.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Streaming interface used by the warm-Data loader.  The context is the
 * ordinary SHA256_CTX; every call builds its own compatible descriptor
 * locally and never stores a descriptor pointer in the context.  The public
 * SHA256_Init/Update/Final entry points and the provider descriptor are not
 * changed.
 */
void native_sha256_stream_init(SHA256_CTX *ctx);
void native_sha256_stream_update(SHA256_CTX *ctx, const void *data, size_t len);
void native_sha256_stream_final(void *digest, SHA256_CTX *ctx);

void native_sha256_digest(const void *data, size_t len, void *digest);

#ifdef __cplusplus
}
#endif

#endif /* _LIBKERN_CRYPTO_NATIVE_SHA256_H_ */
