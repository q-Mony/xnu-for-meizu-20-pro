/* A separate entry point with the kernel vector-register preservation ABI.
 * Do not change the build mode or symbol of the pre-existing implementation. */
#ifdef BUILDKERNEL
#undef BUILDKERNEL
#endif
#define BUILDKERNEL 1
#define _AccelerateCrypto_SHA256_compress _native_sha256_compress_arm64
#include "sha256_compress_arm64.s"
