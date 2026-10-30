/*
 * R18 WarmLoad host fixture.  New file; there is no prior version.
 *
 * This includes the real production drivers/NativeRamBlock/WarmLoad.h and
 * WarmLoadContract.h, and supplies a Hash adapter that calls the production
 * native_sha256_stream_* interface.  It does not copy or reimplement the
 * WarmLoad algorithm or the SHA-256 algorithm.  The fixture image is
 * synthetic; success only means the importer/hash contract is self-consistent
 * for this generated image, not that an APFS capture is valid.
 */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "WarmLoad.h"
#include <libkern/crypto/sha2.h>
#include <libkern/crypto/native_sha256.h>

using namespace warmload;

static size_t hash_inits, hash_updates, hash_finals;

struct Hash {
  SHA256_CTX ctx;
  void init() { ++hash_inits; native_sha256_stream_init(&ctx); }
  void update(const void *data, size_t bytes) { ++hash_updates; native_sha256_stream_update(&ctx, data, bytes); }
  void finish(uint8_t digest[32]) { ++hash_finals; native_sha256_stream_final(digest, &ctx); }
};
static void reset_hash_counts() { hash_inits = hash_updates = hash_finals = 0; }

static void digest(const void *data, size_t bytes, uint8_t out[32]) {
  Hash hash;
  hash.init();
  hash.update(data, bytes);
  hash.finish(out);
}
static void p32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
static void p64(uint8_t *p, uint64_t v) { for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }

struct Fixture {
  std::vector<uint8_t> source, raw;
  uint8_t cold[32] = {}, header[32] = {}, payload[32] = {}, logical[32] = {};
  uint64_t extents = 3, encoded = 0;

  explicit Fixture(bool empty = false) {
    extents = empty ? 0 : 3;
    raw.resize(64 * kBlock);
    static const std::pair<size_t, size_t> ranges[] = {{0, 1}, {3, 36}, {50, 2}};
    size_t present = 0;
    for (size_t e = 0; e < extents; ++e) {
      present += ranges[e].second;
      for (size_t i = 0; i < ranges[e].second * kBlock; ++i)
        raw[ranges[e].first * kBlock + i] = uint8_t((i % 251) + 1);
    }
    encoded = 32 + extents * 16 + present * kBlock;
    source.resize(kBlock + ((encoded + kBlock - 1) / kBlock) * kBlock);
    auto *p = source.data() + kBlock;
    memcpy(p, "DBRDISK1", 8);
    p32(p + 8, 1);
    p32(p + 12, kBlock);
    p64(p + 16, raw.size());
    p64(p + 24, extents);
    size_t at = 32 + extents * 16;
    for (size_t i = 0; i < extents; ++i) {
      const auto &e = ranges[i];
      p64(p + 32 + i * 16, e.first * kBlock);
      p64(p + 40 + i * 16, e.second * kBlock);
      memcpy(p + at, raw.data() + e.first * kBlock, e.second * kBlock);
      at += e.second * kBlock;
    }
    digest(raw.data(), raw.size(), logical);
    digest("cold", 4, cold);
    refresh();
  }
  void refresh() {
    auto *p = source.data();
    memset(p, 0, kBlock);
    memcpy(p, "NVDCAP1", 7);
    p32(p + 8, 1);
    p32(p + 12, kBlock);
    p32(p + 16, 1);
    p32(p + 20, 1);
    p64(p + 24, raw.size());
    p64(p + 32, encoded);
    p64(p + 40, extents);
    p64(p + 48, kBlock);
    p64(p + 56, 7);
    memcpy(p + 64, cold, 32);
    digest(source.data() + kBlock, encoded, payload);
    memcpy(p + 96, payload, 32);
    digest(p, kBlock, header);
    memcpy(p + 128, header, 32);
  }
  Expected expected() const {
    return {7, raw.size(), encoded, extents, cold, header, payload, logical};
  }
};

struct Ops {
  const std::vector<uint8_t> *source = nullptr;
  std::map<void *, size_t> live;
  size_t reads = 0, allocations = 0, checks = 0, resident = 0;
  size_t failRead = SIZE_MAX, failAllocation = SIZE_MAX, failCheck = SIZE_MAX;
  Status checkFailure = BudgetExpired;

  Status check() { return checks++ == failCheck ? checkFailure : Success; }
  void *allocate(size_t bytes) {
    if (allocations++ == failAllocation) return nullptr;
    void *p = calloc(1, bytes);
    assert(p);
    live[p] = bytes;
    resident += bytes;
    return p;
  }
  void deallocate(void *p, size_t bytes) {
    auto it = live.find(p);
    assert(it != live.end() && it->second == bytes);
    resident -= bytes;
    live.erase(it);
    free(p);
  }
  bool read(uint64_t relative, uint32_t blocks, void *dst) {
    assert(blocks && blocks <= kStageBytes / kBlock && relative < kWindowBlocks &&
           blocks <= kWindowBlocks - relative);
    if (reads++ == failRead) return false;
    const size_t offset = static_cast<size_t>(relative * kBlock);
    const size_t bytes = static_cast<size_t>(blocks * kBlock);
    if (offset > source->size() || bytes > source->size() - offset) return false;
    memcpy(dst, source->data() + offset, bytes);
    return true;
  }
};

static size_t cases = 0;
static Status run(Ops &ops, const Expected &expected, const uint8_t *cold,
                  const std::vector<uint8_t> *raw = nullptr) {
  std::vector<uint8_t> buffer(kStageBytes + kBlock);
  RamSparseDisk candidate = {};
  Result result;
  Status status = load<Ops, Hash>(ops, expected, cold, candidate, buffer.data(),
                                  buffer.data() + kStageBytes, result);
  if (status == Success) {
    assert(candidate.bytes == expected.logicalBytes);
    assert(!memcmp(result.logicalDigest, expected.logical, 32));
    assert(!memcmp(result.payloadDigest, expected.payload, 32));
    if (raw) {
      std::vector<uint8_t> expanded(candidate.bytes);
      assert(candidate.bytes == raw->size());
      for (size_t i = 0; i < candidate.bytes / kBlock; ++i)
        if (candidate.pages[i]) memcpy(expanded.data() + i * kBlock, candidate.pages[i], kBlock);
      assert(expanded == *raw);
    }
  } else {
    assert(!candidate.pages && !candidate.bytes && !candidate.resident);
  }
  releaseDisk(candidate, ops);
  assert(ops.live.empty() && ops.resident == 0);
  ++cases;
  return status;
}

static void synthetic() {
  Fixture f;
  Ops baseline;
  baseline.source = &f.source;
  reset_hash_counts();
  assert(run(baseline, f.expected(), f.cold, &f.raw) == Success);
  /* One init/final per digest (header, payload, logical); many updates for the
   * payload and the 64 logical pages.  Segment concatenation would show one
   * init/final per update instead. */
  assert(hash_inits == 3 && hash_finals == 3 && hash_updates == 67);
  const size_t reads = baseline.reads, allocations = baseline.allocations, checks = baseline.checks;
  assert(reads == 3);

  /* Header fields and the zero tail. */
  for (size_t offset : {size_t(0), size_t(8), size_t(12), size_t(16), size_t(20), size_t(24),
                        size_t(32), size_t(40), size_t(48), size_t(56), size_t(64),
                        size_t(96), size_t(160), size_t(4095)}) {
    Fixture x = f;
    x.source[offset] ^= 1;
    Ops o;
    o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == HeaderInvalid);
  }
  { Fixture x = f; x.source[128] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == HeaderHash); }
  { Fixture x = f; x.header[0] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == HeaderHash); }
  { Fixture x = f; x.cold[0] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, f.expected(), x.cold) == ColdMismatch); assert(o.reads == 0); }

  /* The expected payload digest is also bound into the header, so changing it
   * is a header refusal; changing the actual payload bytes is PayloadHash. */
  { Fixture x = f; x.payload[0] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == HeaderInvalid); }
  { Fixture x = f; x.source[kBlock + 32 + x.extents * 16 + 100] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == PayloadHash); }
  { Fixture x = f; x.logical[0] ^= 1; Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == LogicalHash); }

  /* Packed layout refusal. */
  for (size_t i : {size_t(0), size_t(8), size_t(12), size_t(16), size_t(24)}) {
    Fixture x = f;
    x.source[kBlock + i] ^= 1;
    Ops o;
    o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == LayoutInvalid);
  }
  for (unsigned kind = 0; kind < 6; ++kind) {
    Fixture x = f;
    auto *p = x.source.data() + kBlock;
    if (kind == 0) p64(p + 32, 1);
    if (kind == 1) p64(p + 40, 0);
    if (kind == 2) p64(p + 40, x.raw.size() + kBlock);
    if (kind == 3) p64(p + 48, 0);
    if (kind == 4) p64(p + 40, kBlock + 1);
    if (kind == 5) p64(p + 40, 2 * kBlock);
    Ops o;
    o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == LayoutInvalid);
  }
  { Fixture x = f; x.source.resize(kBlock + 128); Ops o; o.source = &x.source;
    assert(run(o, x.expected(), x.cold) == ReadFailed); }
  { Ops o; o.source = &f.source; auto e = f.expected();
    e.extents = kMaxExtents + 1; assert(run(o, e, f.cold) == LayoutInvalid); assert(o.reads == 0); }
  { Ops o; o.source = &f.source; auto e = f.expected();
    e.logicalBytes = (kMaxPages + 1) * kBlock; assert(run(o, e, f.cold) == LayoutInvalid); }
  { Ops o; o.source = &f.source; auto e = f.expected();
    e.encodedBytes = kWindowBlocks * kBlock; assert(run(o, e, f.cold) == LayoutInvalid); }

  /* Every read, allocation and budget checkpoint must reject and release. */
  for (size_t i = 0; i < reads; ++i) {
    Ops o;
    o.source = &f.source;
    o.failRead = i;
    assert(run(o, f.expected(), f.cold) == ReadFailed);
  }
  for (size_t i = 0; i < allocations; ++i) {
    Ops o;
    o.source = &f.source;
    o.failAllocation = i;
    assert(run(o, f.expected(), f.cold) == NoMemory);
  }
  for (size_t i = 0; i < checks; ++i) {
    Ops o;
    o.source = &f.source;
    o.failCheck = i;
    assert(run(o, f.expected(), f.cold) == BudgetExpired);
  }
  for (size_t i = 0; i < checks; ++i) {
    Ops o;
    o.source = &f.source;
    o.failCheck = i;
    o.checkFailure = Cancelled;
    assert(run(o, f.expected(), f.cold) == Cancelled);
  }

  /* Sparse fixture: no extents, every logical page is zero. */
  Fixture empty(true);
  Ops eo;
  eo.source = &empty.source;
  assert(run(eo, empty.expected(), empty.cold, &empty.raw) == Success);

  printf("{\"warmload_cases\":%zu,\"baseline_reads\":%zu,\"baseline_allocations\":%zu,"
         "\"baseline_checks\":%zu,\"hash_inits\":3,\"hash_finals\":3,\"hash_updates\":67,"
         "\"asan_ubsan\":true}\n", cases, reads, allocations, checks);
}

int main() {
  synthetic();
  return 0;
}
