/*
 * Copyright (c) 2012 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <libkern/crypto/crypto_internal.h>
#include <libkern/crypto/sha2.h>
#include <libkern/crypto/native_sha256.h>
#include <libkern/libkern.h>
#include <kern/debug.h>
#include <corecrypto/ccdigest.h>

/*
 * With the source corecrypto archive linked, the digest implementations are
 * present in the kernel itself. The original g_crypto_funcs table is only
 * populated when the prelinked corecrypto kext starts, which is later than
 * entropy_init on some targets; use the in-kernel provider so early callers
 * never dereference a NULL table.
 */
#if defined(XNU_SOURCE_CORECRYPTO14)
#include <corecrypto/ccsha2.h>
#define SHA2_DI(name) (name())
#define SHA2_INIT(di, ctx) ccdigest_init((di), (ctx))
#define SHA2_UPDATE(di, ctx, len, data) ccdigest_update((di), (ctx), (len), (data))
#else
#define SHA2_DI(name) (g_crypto_funcs->name)
#define SHA2_INIT(di, ctx) g_crypto_funcs->ccdigest_init_fn((di), (ctx))
#define SHA2_UPDATE(di, ctx, len, data) g_crypto_funcs->ccdigest_update_fn((di), (ctx), (len), (data))
#endif

#if defined(CRYPTO_SHA2)

void
SHA256_Init(SHA256_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha256_di);

	/* Make sure the context size for the digest info fits in the one we have */
	if (ccdigest_di_size(di) > sizeof(SHA256_CTX)) {
		panic("%s: inconsistent size for SHA256 context", __FUNCTION__);
	}

	SHA2_INIT(di, ctx->ctx);
}

void
SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha256_di);

	SHA2_UPDATE(di, ctx->ctx, len, data);
}

void
SHA256_Final(void *digest, SHA256_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha256_di);

	ccdigest_final(di, ctx->ctx, digest);
}

void
SHA384_Init(SHA384_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha384_di);

	/* Make sure the context size for the digest info fits in the one we have */
	if (ccdigest_di_size(di) > sizeof(SHA384_CTX)) {
		panic("%s: inconsistent size for SHA384 context", __FUNCTION__);
	}

	SHA2_INIT(di, ctx->ctx);
}

void
SHA384_Update(SHA384_CTX *ctx, const void *data, size_t len)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha384_di);

	SHA2_UPDATE(di, ctx->ctx, len, data);
}


void
SHA384_Final(void *digest, SHA384_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha384_di);

	ccdigest_final(di, ctx->ctx, digest);
}

void
SHA512_Init(SHA512_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha512_di);

	/* Make sure the context size for the digest info fits in the one we have */
	if (ccdigest_di_size(di) > sizeof(SHA512_CTX)) {
		panic("%s: inconsistent size for SHA512 context", __FUNCTION__);
	}

	SHA2_INIT(di, ctx->ctx);
}

void
SHA512_Update(SHA512_CTX *ctx, const void *data, size_t len)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha512_di);

	SHA2_UPDATE(di, ctx->ctx, len, data);
}

void
SHA512_Final(void *digest, SHA512_CTX *ctx)
{
	const struct ccdigest_info *di;
	di = SHA2_DI(ccsha512_di);

	ccdigest_final(di, ctx->ctx, digest);
}

#else

/* As these are part of the KPI, we need to stub them out for any kernel configuration that does not support SHA2. */

void UNSUPPORTED_API(SHA256_Init, SHA256_CTX *ctx);
void UNSUPPORTED_API(SHA384_Init, SHA384_CTX *ctx);
void UNSUPPORTED_API(SHA512_Init, SHA512_CTX *ctx);
void UNSUPPORTED_API(SHA256_Update, SHA256_CTX *ctx, const void *data, size_t len);
void UNSUPPORTED_API(SHA384_Update, SHA384_CTX *ctx, const void *data, size_t len);
void UNSUPPORTED_API(SHA512_Update, SHA512_CTX *ctx, const void *data, size_t len);
void UNSUPPORTED_API(SHA256_Final, void *digest, SHA256_CTX *ctx);
void UNSUPPORTED_API(SHA384_Final, void *digest, SHA384_CTX *ctx);
void UNSUPPORTED_API(SHA512_Final, void *digest, SHA512_CTX *ctx);

#endif

/* Keep the hardware path local to native signature preflight. The provider's
 * other SHA-256 users retain their existing descriptor and implementation. */
#if defined(XNU_SOURCE_CORECRYPTO14) && defined(CRYPTO_SHA2) && \
    defined(__arm64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA2)
#include <kern/cpu_data.h>

extern void native_sha256_compress_arm64(ccdigest_state_t state,
    size_t nblocks, const void *data);

static void
native_sha256_compress(ccdigest_state_t state, size_t nblocks, const void *data)
{
	const unsigned char *input = data;

	while (nblocks != 0) {
		/* Bound each interval with preemption disabled to 16 KiB of input. */
		size_t batch = nblocks > 256 ? 256 : nblocks;
		uint64_t isar0;

		disable_preemption();
		/* Read the CPU on which this batch will actually execute. */
		__asm__ volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r" (isar0));
		unsigned int sha2 = (unsigned int)((isar0 >> 12) & 0xf);
		if (sha2 == 1 || sha2 == 2) {
			native_sha256_compress_arm64(state, batch, input);
			enable_preemption();
		} else {
			enable_preemption();
			/* Reserved feature values also use the original provider. */
			ccsha256_di()->compress(state, batch, input);
		}

		input += batch * CCSHA256_BLOCK_SIZE;
		nblocks -= batch;
	}
}

/*
 * Build the local compatible descriptor used for one streaming call.  The
 * caller owns the storage; no pointer to it is retained in the SHA256_CTX.
 * Only the compression callback differs from the provider descriptor.
 */
static void
native_sha256_stream_descriptor(struct ccdigest_info *di)
{
	*di = *ccsha256_di();
	di->compress = native_sha256_compress;
}
#endif

void
native_sha256_stream_init(SHA256_CTX *ctx)
{
#if defined(XNU_SOURCE_CORECRYPTO14) && defined(CRYPTO_SHA2) && \
    defined(__arm64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA2)
	struct ccdigest_info di;

	native_sha256_stream_descriptor(&di);
	if (ccdigest_di_size(&di) > sizeof(*ctx)) {
		panic("%s: inconsistent size for SHA256 context", __FUNCTION__);
	}
	ccdigest_init(&di, ctx->ctx);
#else
	SHA256_Init(ctx);
#endif
}

void
native_sha256_stream_update(SHA256_CTX *ctx, const void *data, size_t len)
{
#if defined(XNU_SOURCE_CORECRYPTO14) && defined(CRYPTO_SHA2) && \
    defined(__arm64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA2)
	struct ccdigest_info di;

	native_sha256_stream_descriptor(&di);
	ccdigest_update(&di, ctx->ctx, len, data);
#else
	SHA256_Update(ctx, data, len);
#endif
}

void
native_sha256_stream_final(void *digest, SHA256_CTX *ctx)
{
#if defined(XNU_SOURCE_CORECRYPTO14) && defined(CRYPTO_SHA2) && \
    defined(__arm64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA2)
	struct ccdigest_info di;

	native_sha256_stream_descriptor(&di);
	ccdigest_final(&di, ctx->ctx, digest);
#else
	SHA256_Final(digest, ctx);
#endif
}

void
native_sha256_digest(const void *data, size_t len, void *digest)
{
	SHA256_CTX ctx;

	native_sha256_stream_init(&ctx);
	native_sha256_stream_update(&ctx, data, len);
	native_sha256_stream_final(digest, &ctx);
}
