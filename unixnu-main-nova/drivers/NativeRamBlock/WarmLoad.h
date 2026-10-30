// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include "WarmLoadContract.h"
#include "SparseDisk.h"

namespace warmload {
struct Expected {
  uint64_t captureId, logicalBytes, encodedBytes, extents;
  const uint8_t *cold, *header, *payload, *logical;
};
inline Expected pinned() {
  return { kCaptureId, kLogicalBytes, kEncodedBytes, kExtents,
           kColdHash, kHeaderHash, kPayloadHash, kLogicalHash };
}
struct Result {
  uint64_t readBytes = 0, importedPages = 0;
  uint8_t logicalDigest[32] = {}, payloadDigest[32] = {};
};
inline uint32_t u32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t u64(const uint8_t *p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }
inline bool zero(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; ++i) if (p[i]) return false;
  return true;
}
template<class Ops> void releaseDisk(RamSparseDisk &disk, Ops &ops) {
  if (disk.pages) {
    for (uint64_t i = 0; i < disk.bytes / kBlock; ++i) if (disk.pages[i]) ops.deallocate(disk.pages[i], kBlock);
    ops.deallocate(disk.pages, (disk.bytes / kBlock) * sizeof(uint8_t *));
  }
  disk = {};
}

// The stage receives whole UFS blocks; only encoded bytes participate in SHA.
// Ops::check is a software deadline/cancellation checkpoint, not an I/O timeout.
template<class Ops, class Hash> class Reader {
  Ops &ops; Hash &hash; uint8_t *stage; uint64_t encoded, loaded = 0;
  size_t cursor = 0, available = 0;
  Result &result;
public:
  Status status = Success;
  Reader(Ops &o, Hash &h, uint8_t *s, uint64_t n, Result &r) : ops(o), hash(h), stage(s), encoded(n), result(r) {}
  bool copy(void *destination, size_t bytes) {
    uint8_t *out = static_cast<uint8_t *>(destination);
    while (bytes) {
      if ((status = ops.check()) != Success) return false;
      if (cursor == available) {
        if (loaded >= encoded) { status = LayoutInvalid; return false; }
        uint64_t remain = encoded - loaded;
        available = remain > kStageBytes ? kStageBytes : static_cast<size_t>(remain);
        uint32_t blocks = static_cast<uint32_t>((available + kBlock - 1) / kBlock);
        if (!ops.read(1 + loaded / kBlock, blocks, stage)) { status = ReadFailed; return false; }
        result.readBytes += blocks * kBlock;
        if ((status = ops.check()) != Success) return false;
        if (!zero(stage + available, blocks * kBlock - available)) { status = LayoutInvalid; return false; }
        hash.update(stage, available); loaded += available; cursor = 0;
      }
      size_t count = available - cursor;
      if (count > bytes) count = bytes;
      memcpy(out, stage + cursor, count); cursor += count; out += count; bytes -= count;
    }
    return true;
  }
  bool finished() const { return loaded == encoded && cursor == available; }
};

// All output belongs to a fresh candidate until the adapter swaps it. No alias
// to the live cold disk and no writer/sync API exist at this seam.
template<class Ops, class Hash>
__attribute__((noinline)) Status load(Ops &ops, const Expected &expected, const uint8_t cold[32],
    RamSparseDisk &candidate, uint8_t *stage, uint8_t *header, Result &result) {
  result = {};
  if (candidate.pages || candidate.bytes || candidate.resident || !stage || !header) return ConfigRefused;
  if (memcmp(cold, expected.cold, 32)) return ColdMismatch;
  if (!expected.logicalBytes || expected.logicalBytes % kBlock || expected.logicalBytes / kBlock > kMaxPages ||
      expected.extents > kMaxExtents || expected.encodedBytes < 32 ||
      expected.encodedBytes > (kWindowBlocks - 1) * kBlock) return LayoutInvalid;
  Status status = ops.check();
  if (status != Success) return status;
  if (!ops.read(0, 1, header)) return ReadFailed;
  result.readBytes += kBlock;
  if ((status = ops.check()) != Success) return status;
  if (memcmp(header, "NVDCAP1\0", 8) || u32(header + 8) != 1 || u32(header + 12) != kBlock ||
      u32(header + 16) != 1 || u32(header + 20) != 1 || u64(header + 24) != expected.logicalBytes ||
      u64(header + 32) != expected.encodedBytes || u64(header + 40) != expected.extents ||
      u64(header + 48) != kBlock || u64(header + 56) != expected.captureId ||
      memcmp(header + 64, expected.cold, 32) || memcmp(header + 96, expected.payload, 32) ||
      !zero(header + 160, kBlock - 160)) return HeaderInvalid;
  uint8_t recorded[32], digest[32]; memcpy(recorded, header + 128, 32); memset(header + 128, 0, 32);
  Hash hash; hash.init(); hash.update(header, kBlock); hash.finish(digest);
  if (memcmp(recorded, digest, 32) || memcmp(digest, expected.header, 32)) return HeaderHash;

  struct Extent { uint64_t first, bytes; };
  const size_t extentBytes = static_cast<size_t>(expected.extents * sizeof(Extent));
  Extent *extents = extentBytes ? static_cast<Extent *>(ops.allocate(extentBytes)) : nullptr;
  if (extentBytes && !extents) return NoMemory;
  hash.init(); Reader<Ops, Hash> reader(ops, hash, stage, expected.encodedBytes, result);
  uint8_t packed[32];
  if (!reader.copy(packed, sizeof(packed))) status = reader.status;
  else if (memcmp(packed, "DBRDISK1", 8) || u32(packed + 8) != 1 || u32(packed + 12) != kBlock ||
           u64(packed + 16) != expected.logicalBytes || u64(packed + 24) != expected.extents ||
           expected.extents > (expected.encodedBytes - 32) / 16) status = LayoutInvalid;
  uint64_t end = 0, encoded = 32 + expected.extents * 16;
  for (uint64_t i = 0; status == Success && i < expected.extents; ++i) {
    uint8_t raw[16];
    if (!reader.copy(raw, sizeof(raw))) { status = reader.status; break; }
    const uint64_t first = u64(raw), bytes = u64(raw + 8);
    if (!bytes || ((first | bytes) & (kBlock - 1)) || first < end || first > expected.logicalBytes ||
        bytes > expected.logicalBytes - first || encoded > expected.encodedBytes || bytes > expected.encodedBytes - encoded) {
      status = LayoutInvalid; break;
    }
    extents[i] = { first, bytes }; end = first + bytes; encoded += bytes;
  }
  if (status == Success && encoded != expected.encodedBytes) status = LayoutInvalid;
  if (status == Success) {
    candidate.bytes = expected.logicalBytes;
    candidate.pages = static_cast<uint8_t **>(ops.allocate((candidate.bytes / kBlock) * sizeof(uint8_t *)));
    if (!candidate.pages) status = NoMemory;
  }
  for (uint64_t i = 0; status == Success && i < expected.extents; ++i) {
    for (uint64_t offset = 0; status == Success && offset < extents[i].bytes; offset += kBlock) {
      if ((status = ops.check()) != Success) break;
      uint8_t *page = static_cast<uint8_t *>(ops.allocate(kBlock));
      if (!page) { status = NoMemory; break; }
      if (!reader.copy(page, kBlock)) { ops.deallocate(page, kBlock); status = reader.status; break; }
      candidate.pages[(extents[i].first + offset) / kBlock] = page;
      candidate.resident += kBlock; ++result.importedPages;
    }
  }
  if (extents) ops.deallocate(extents, extentBytes);
  if (status == Success && !reader.finished()) status = LayoutInvalid;
  if (status == Success) {
    hash.finish(digest);
    memcpy(result.payloadDigest, digest, 32);
    if (memcmp(digest, expected.payload, 32)) status = PayloadHash;
  }
  if (status == Success) {
    static const uint8_t empty[kBlock] = {};
    hash.init();
    for (uint64_t i = 0; status == Success && i < candidate.bytes / kBlock; ++i) {
      if ((status = ops.check()) == Success) hash.update(candidate.pages[i] ? candidate.pages[i] : empty, kBlock);
    }
    if (status == Success) {
      hash.finish(result.logicalDigest);
      if (memcmp(result.logicalDigest, expected.logical, 32)) status = LogicalHash;
      else status = ops.check();
    }
  }
  if (status != Success) releaseDisk(candidate, ops);
  return status;
}
} // namespace warmload
