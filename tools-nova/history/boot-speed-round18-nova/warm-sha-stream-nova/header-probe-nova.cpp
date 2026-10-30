/*
 * R18 production header probe.  New file; there is no prior version.
 * Includes the real libkern/crypto/native_sha256.h and proves that the
 * streaming declarations are visible with SHA256_CTX from the real sha2.h.
 */
#include <cstdio>
#include <cstring>
#include <libkern/crypto/sha2.h>
#include <libkern/crypto/native_sha256.h>

int main() {
  SHA256_CTX ctx;
  native_sha256_stream_init(&ctx);
  native_sha256_stream_update(&ctx, "abc", 3);
  unsigned char digest[32];
  native_sha256_stream_final(digest, &ctx);
  static const unsigned char expected[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
  };
  if (memcmp(digest, expected, sizeof(digest))) return 1;
  std::printf("HEADER_STREAMING_OK\n");
  return 0;
}
