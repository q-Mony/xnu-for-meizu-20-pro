// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// R9 is a pinned, read-only replay experiment, not general Data persistence.
namespace warmload {
constexpr uint64_t kBlock = 4096, kWindowBlocks = 65536, kLogBlocks = 8192;
constexpr size_t kStageBytes = 128 * 1024;
constexpr uint64_t kMaxPages = 131072, kMaxExtents = 65536;
constexpr unsigned kSourceWaitMs = 10000, kLoadBudgetMs = 30000, kRootGateMs = 60000;
constexpr uint64_t kCaptureId = 4013558640ULL, kLogicalBytes = 536870912ULL;
constexpr uint64_t kEncodedBytes = 231147936ULL, kExtents = 152;
constexpr char kDataUuid[] = "691978E3-CC8E-4F14-9BF9-D55DAB2D1A4C";
constexpr uint8_t kColdHash[32] = {
  0x37,0x38,0x80,0xd1,0xf6,0x46,0x95,0xc6,0xba,0x71,0xdb,0x05,0x35,0x4b,0x01,0x9c,
  0xbf,0xe1,0x1d,0x25,0x3d,0x32,0x42,0x7b,0x8d,0x49,0x3b,0xcc,0x08,0x59,0x81,0xb3 };
constexpr uint8_t kPayloadHash[32] = {
  0xa8,0xba,0x06,0x92,0xf6,0x80,0xac,0x46,0x34,0x63,0xe7,0x64,0x05,0x75,0xc1,0xfa,
  0x75,0xbd,0xe8,0x6b,0xe6,0xc6,0x20,0xba,0x0a,0xf0,0x0f,0xab,0xcd,0x85,0x47,0xd9 };
constexpr uint8_t kHeaderHash[32] = {
  0x68,0x52,0x38,0xb9,0x37,0x7d,0x64,0xf1,0x72,0xc1,0xf8,0x9a,0xe3,0xc9,0x8e,0xcb,
  0xa0,0x9d,0x41,0xa9,0x28,0xfa,0xa8,0x39,0x4c,0x8f,0xec,0x91,0xff,0xfa,0xa9,0x0d };
constexpr uint8_t kLogicalHash[32] = {
  0x37,0x25,0x49,0xa9,0xfc,0xb7,0x4b,0xa0,0x62,0xfb,0xd8,0x71,0xa3,0x70,0x19,0x9c,
  0x7d,0x32,0x65,0x7c,0xfa,0x4e,0x43,0x60,0x40,0xc1,0x20,0xb6,0xdb,0xfc,0xbd,0x43 };
enum SourceState : uint32_t { SourcePending = 0, SourceReady = 1, SourceRejected = 2 };
enum Status : uint32_t {
  Success = 0, Disabled = 1, ConfigRefused = 2, SourceRefused = 3, SourceTimeout = 4,
  NoMemory = 5, ReadFailed = 6, HeaderInvalid = 7, HeaderHash = 8, LayoutInvalid = 9,
  PayloadHash = 10, LogicalHash = 11, BudgetExpired = 12, Cancelled = 13,
  ColdMismatch = 14, ThreadFailed = 15, DuplicateTarget = 16, Loading = 100
};
struct SourceHandle { uint64_t firstLba = 0, blocks = 0; };
inline bool sourceWindow(uint64_t first, uint64_t last, uint64_t imageBytes,
                         uint64_t reserve, SourceHandle &out) {
  out = {};
  if (last < first || last > UINT32_MAX || imageBytes > UINT64_MAX - 4095) return false;
  const uint64_t partition = last - first + 1, image = (imageBytes + 4095) / 4096;
  if (reserve < kWindowBlocks + kLogBlocks || reserve >= partition ||
      image > UINT64_MAX - 1024 || partition - reserve < image + 1024) return false;
  out = { last + 1 - kLogBlocks - kWindowBlocks, kWindowBlocks };
  return out.firstLba >= first && out.firstLba <= UINT32_MAX - (kWindowBlocks - 1);
}
inline bool validRead(const SourceHandle &fixed, const SourceHandle &handle,
                      uint64_t relative, uint32_t blocks, const void *buffer) {
  return buffer && fixed.blocks == kWindowBlocks && handle.firstLba == fixed.firstLba &&
    handle.blocks == fixed.blocks && blocks && blocks <= kStageBytes / kBlock &&
    relative < fixed.blocks && blocks <= fixed.blocks - relative;
}
} // namespace warmload
