// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
// Shared production window reader. Ops owns synchronization, block I/O,
// integrity verification and copying to the caller. No callback runs under
// this guard: the IOKit wrapper completes the request after this returns.
static bool ufsWindowGeometry (uint64_t absolute, uint64_t bytes)
{
  constexpr uint64_t limit = (uint64_t (UINT32_MAX) + 1) * 4096;
  return bytes && !(bytes & 511) && !(absolute & 4095) && absolute < limit && bytes <= limit - absolute;
}
struct UfsWindowResult { IOReturn status; uint64_t done; };
template <class Ops> struct UfsWindowGuard {
  Ops &ops;
  explicit UfsWindowGuard (Ops &value) : ops(value) { ops.lock (); }
  ~UfsWindowGuard () { ops.unlock (); }
  UfsWindowGuard (const UfsWindowGuard &) = delete;
  UfsWindowGuard &operator= (const UfsWindowGuard &) = delete;
};
template <class Ops>
static UfsWindowResult ufsReadWindow (Ops &ops, uint64_t absolute, uint64_t bytes,
                                     uint64_t block, uint64_t count, uint8_t *bounce)
{
  static_assert (VR_BLOCK_BYTES >= 4096 && VR_BLOCK_BYTES % 4096 == 0,
                 "Integrity units must contain whole transport blocks");
  static_assert (kDataMax >= VR_BLOCK_BYTES && kDataMax % VR_BLOCK_BYTES == 0,
                 "The bounce buffer must contain whole integrity units");
  if (!bounce || !ufsWindowGeometry (absolute, bytes) || !count || block >= bytes / 512 || count > bytes / 512 - block)
    return { kIOReturnBadArgument, 0 };
  UfsWindowGuard<Ops> guard (ops);
  uint64_t remaining = count * 512;
  UfsWindowResult result = { kIOReturnSuccess, 0 };
  while (remaining) {
    // Align relative to the signed window, not the physical disk. Read only
    // the integrity units needed by this request, up to the existing bounce
    // capacity. A small request must not turn into an unrelated 128 KiB read.
    uint64_t position = block * 512 + result.done;
    uint64_t index = position / VR_BLOCK_BYTES;
    uint64_t unitStart = index * VR_BLOCK_BYTES;
    uint64_t within = position - unitStart;
    uint64_t covered = within + remaining;
    if (covered > kDataMax) covered = kDataMax;
    uint64_t batchBytes = ((covered + VR_BLOCK_BYTES - 1) / VR_BLOCK_BYTES) * VR_BLOCK_BYTES;
    if (batchBytes > bytes - unitStart) batchBytes = bytes - unitStart;
    uint64_t blocks = (batchBytes + 4095) / 4096;
    if (blocks * 4096 > kDataMax) { result.status = kIOReturnBadArgument; break; }
    result.status = ops.read ((absolute + unitStart) / 4096, static_cast<uint32_t> (blocks), bounce);
    if (result.status != kIOReturnSuccess) break;
    // Authenticate the entire batch before exposing any of it. The physical
    // read may include 4 KiB tail padding, but no padding enters an original
    // signed block's digest. Earlier successful batches retain their progress.
    for (uint64_t offset = 0; offset < batchBytes; offset += VR_BLOCK_BYTES) {
      uint64_t unitBytes = batchBytes - offset;
      if (unitBytes > VR_BLOCK_BYTES) unitBytes = VR_BLOCK_BYTES;
      result.status = ops.verify (index + offset / VR_BLOCK_BYTES, bounce + offset,
                                  static_cast<size_t> (unitBytes));
      if (result.status != kIOReturnSuccess) break;
    }
    if (result.status != kIOReturnSuccess) break;
    uint64_t usable = batchBytes - within;
    if (usable > remaining) usable = remaining;
    uint64_t copied = ops.copy (result.done, bounce + within, usable);
    if (copied != usable) { result.status = kIOReturnIOError; break; }
    result.done += copied;
    remaining -= usable;
  }
  return result;
}
