// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// One-shot experimental block-device cut. This is NOT a clean filesystem or an
// application-consistent snapshot. Caller freezes all source writes throughout
// planning and capture, and owns two preallocated, nonoverlapping buffers.
namespace warmcapture {
constexpr uint64_t kBlock = 4096;
constexpr uint64_t kMaxLogical = 512ULL * 1024 * 1024;
constexpr uint64_t kMaxWindowBlocks = 65536;
constexpr uint64_t kMaxExtents = 65536;
constexpr size_t kStageBytes = 128 * 1024;
constexpr size_t kHeaderDigestOffset = 128;
enum Status : uint32_t {
  Success = 0, Disabled = 1, NotReady = 2, Unsupported = 3, WindowRefused = 4,
  NoBuffer = 5, TooLarge = 6, InvalidateWrite = 7, InvalidateSync = 8,
  PayloadWrite = 9, PayloadSync = 10, HeaderWrite = 11, HeaderSync = 12,
  AlreadyAttempted = 13, SourceChanged = 14
};
struct Result {
  uint64_t logicalBytes = 0, encodedBytes = 0, extents = 0, nonzeroPages = 0;
  uint8_t payloadDigest[32] = {};
  // A failed final commit or invalidation cannot guarantee on-media invalidity
  // if the medium refuses subsequent I/O. A consumer also needs this run's
  // successful completion record, its exact capture ID and complete hashes.
  bool headerUncertain = false;
};
inline void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline void put64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline bool nonzero(const uint8_t *p) {
  if (p) for (size_t i = 0; i < kBlock; ++i) if (p[i]) return true;
  return false;
}

template<class Source>
Status plan(const Source &source, uint64_t logicalBytes, uint64_t windowBlocks, Result &out) {
  out = Result{};
  if (!logicalBytes || logicalBytes > kMaxLogical || logicalBytes % kBlock) return Unsupported;
  if (windowBlocks < 2 || windowBlocks > kMaxWindowBlocks) return WindowRefused;
  out.logicalBytes = logicalBytes;
  bool inRun = false;
  for (uint64_t i = 0; i < logicalBytes / kBlock; ++i) {
    const bool present = nonzero(source.page(i));
    if (present) { ++out.nonzeroPages; if (!inRun) ++out.extents; }
    inRun = present;
  }
  if (out.extents > kMaxExtents) return TooLarge;
  out.encodedBytes = 32 + out.extents * 16 + out.nonzeroPages * kBlock;
  const uint64_t available = (windowBlocks - 1) * kBlock;
  if (out.encodedBytes > available) return TooLarge;
  return Success;
}

template<class Store, class Hash> class Stream {
  Store &store;
  Hash &hash;
  uint8_t *stage;
  size_t capacity, used = 0;
  uint64_t block = 1, limit;
public:
  uint64_t encoded = 0;
  bool sourceExceeded = false;
  Stream(Store &s, Hash &h, uint8_t *p, size_t n, uint64_t maximum)
      : store(s), hash(h), stage(p), capacity(n), limit(maximum) { hash.init(); }
  bool append(const void *data, size_t bytes) {
    if (bytes > limit - encoded) { sourceExceeded = true; return false; }
    const uint8_t *p = static_cast<const uint8_t *>(data);
    hash.update(p, bytes); encoded += bytes;
    while (bytes) {
      size_t n = bytes < capacity - used ? bytes : capacity - used;
      memcpy(stage + used, p, n); used += n; p += n; bytes -= n;
      if (used == capacity) {
        if (!store.write(block, static_cast<uint32_t>(capacity / kBlock), stage)) return false;
        block += capacity / kBlock; used = 0;
      }
    }
    return true;
  }
  bool finish(uint8_t digest[32]) {
    if (used) {
      const size_t padded = (used + kBlock - 1) & ~(static_cast<size_t>(kBlock) - 1);
      memset(stage + used, 0, padded - used);
      if (!store.write(block, static_cast<uint32_t>(padded / kBlock), stage)) return false;
    }
    hash.finish(digest);
    return true;
  }
};

template<class Store>
void revoke(Store &store, uint8_t *header, Result &out) {
  memset(header, 0, kBlock);
  // Do not pretend this succeeds when the transport remains unavailable.
  const bool wrote = store.write(0, 1, header);
  const bool synced = wrote && store.sync();
  out.headerUncertain = !synced;
}

template<class Source, class Store, class Hash>
Status capture(const Source &source, Store &store, Hash &hash, uint64_t logicalBytes,
               uint64_t windowBlocks, const uint8_t coldSeed[32], uint64_t captureId,
               uint8_t *stage, size_t stageBytes, uint8_t *header, Result &out) {
  Status status = plan(source, logicalBytes, windowBlocks, out);
  if (status != Success) return status;
  if (!captureId || !coldSeed) return Unsupported;
  if (!stage || !header || stageBytes < kBlock || stageBytes > kStageBytes || stageBytes % kBlock) return NoBuffer;
  memset(header, 0, kBlock);
  if (!store.write(0, 1, header)) { out.headerUncertain = true; return InvalidateWrite; }
  if (!store.sync()) { out.headerUncertain = true; return InvalidateSync; }

  Stream<Store, Hash> stream(store, hash, stage, stageBytes, out.encodedBytes);
  uint8_t packedHeader[32] = {};
  memcpy(packedHeader, "DBRDISK1", 8); put32(packedHeader + 8, 1); put32(packedHeader + 12, kBlock);
  put64(packedHeader + 16, logicalBytes); put64(packedHeader + 24, out.extents);
  bool ok = stream.append(packedHeader, sizeof(packedHeader));
  uint64_t emittedExtents = 0, emittedPages = 0;
  for (uint64_t i = 0; ok && i < logicalBytes / kBlock;) {
    if (!nonzero(source.page(i))) { ++i; continue; }
    const uint64_t first = i++;
    while (i < logicalBytes / kBlock && nonzero(source.page(i))) ++i;
    uint8_t extent[16]; put64(extent, first * kBlock); put64(extent + 8, (i - first) * kBlock);
    ok = stream.append(extent, sizeof(extent)); ++emittedExtents;
  }
  for (uint64_t i = 0; ok && i < logicalBytes / kBlock; ++i) {
    const uint8_t *page = source.page(i);
    if (nonzero(page)) { ok = stream.append(page, kBlock); ++emittedPages; }
  }
  if (!ok) { revoke(store, header, out); return stream.sourceExceeded ? SourceChanged : PayloadWrite; }
  if (emittedExtents != out.extents || emittedPages != out.nonzeroPages || stream.encoded != out.encodedBytes) {
    revoke(store, header, out); return SourceChanged;
  }
  if (!stream.finish(out.payloadDigest)) { revoke(store, header, out); return PayloadWrite; }
  if (!store.sync()) { revoke(store, header, out); return PayloadSync; }

  memset(header, 0, kBlock);
  memcpy(header, "NVDCAP1", 7);
  put32(header + 8, 1); put32(header + 12, kBlock); put32(header + 16, 1);
  put32(header + 20, 1); // BLOCK_CUT_NOT_CLEAN, never silently upgraded.
  put64(header + 24, logicalBytes); put64(header + 32, out.encodedBytes);
  put64(header + 40, out.extents); put64(header + 48, kBlock); put64(header + 56, captureId);
  memcpy(header + 64, coldSeed, 32); memcpy(header + 96, out.payloadDigest, 32);
  hash.init(); hash.update(header, kBlock); hash.finish(header + kHeaderDigestOffset);
  if (!store.write(0, 1, header)) { revoke(store, header, out); return HeaderWrite; }
  if (!store.sync()) { revoke(store, header, out); return HeaderSync; }
  return Success;
}
} // namespace warmcapture
