// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <IOKit/IOBSD.h>
#include <sys/param.h>
#include <libkern/crypto/sha2.h>
#include <libkern/crypto/native_sha256.h>
#include <mach/machine.h>
#include <machine/machine_routines.h>
#include <vm/vm_kern.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "SparseDisk.h"
#include "PersistOverlay.h"
#include "DirectDisk.h"
#include "WarmCapture.h"
#include "WarmLoad.h"

extern "C" {
extern unsigned long gPhysBase, gPhysSize;
int native_ufs_warm_source_try_open(warmload::SourceHandle *) __attribute__((weak_import));
int native_ufs_warm_source_read(const warmload::SourceHandle *, uint64_t, uint32_t, void *) __attribute__((weak_import));
volatile uint32_t native_warm_load_status = warmload::Disabled;
uint8_t native_warm_load_payload_sha256[32], native_warm_load_logical_sha256[32];
volatile uint64_t native_warm_load_start_ticks = 0, native_warm_load_source_ticks = 0,
    native_warm_load_read_ticks = 0, native_warm_load_hash_ticks = 0, native_warm_load_end_ticks = 0,
    native_warm_load_first_read_ticks = 0, native_warm_load_bytes = 0, native_warm_load_import_ticks = 0,
    native_warm_load_publish_ticks = 0;
int native_ram_block_volume_device(const char *, dev_t *, char *, size_t);
volatile uint32_t native_ram_block_ready = 0;
volatile uint64_t native_ram_block_bytes = 0;
volatile uint64_t native_ram_block_reads = 0;
volatile uint64_t native_ram_block_writes = 0;
volatile uint64_t native_ram_block_failures = 0;
volatile uint64_t native_ram_block_resident_bytes = 0;
volatile uint32_t native_ram_block_sparse = 0;
uint8_t native_ram_block_seed_sha256[32];
// Optional persistence of the changed pages through a board-provided store window (see PersistOverlay.h). All weak: a build
// without the provider keeps the volatile disk.
unsigned native_ufs_store_mode(void) __attribute__((weak_import));
int native_ufs_store_open(uint64_t, uint64_t *, const char **) __attribute__((weak_import));
int native_ufs_store_read(uint64_t, uint32_t, void *) __attribute__((weak_import));
int native_ufs_store_write(uint64_t, uint32_t, const void *) __attribute__((weak_import));
int native_ufs_store_sync(void) __attribute__((weak_import));
unsigned native_ufs_store_gpt_check(void) __attribute__((weak_import));
uint64_t native_ufs_export_blocks(void) __attribute__((weak_import));
int native_ufs_export_write(uint64_t, uint32_t, const void *) __attribute__((weak_import));
int native_ufs_log_sync(void) __attribute__((weak_import));
extern volatile int native_owner_data_status __attribute__((weak_import));
// Experimental export diagnostics. Times are Mach absolute ticks, not CPU time.
// Status 100 means no enabled call; other values are warmcapture::Status.
volatile uint32_t native_warm_capture_status = 100, native_warm_capture_header_uncertain = 0;
volatile uint64_t native_warm_capture_bytes = 0, native_warm_capture_extents = 0,
                  native_warm_capture_logical_bytes = 0,
                  native_warm_capture_start_ticks = 0, native_warm_capture_end_ticks = 0,
                  native_warm_capture_frozen_ticks = 0;
// Diagnostics. state: 0 off, 1 blank (read-only probe), 2 resumed, 3 fresh, 4 refused/failed, 5 degraded after an I/O error,
// 6 resumed read-only (probe). header: persist::checkHeader result.
volatile uint32_t native_data_persist_state = 0, native_data_persist_header = 9, native_data_persist_gpt = 0xff, native_data_persist_gpt_after = 0xff;
volatile uint64_t native_data_persist_pages = 0, native_data_persist_loaded = 0, native_data_persist_writes = 0,
                  native_data_persist_syncs = 0, native_data_persist_errors = 0, native_data_persist_first_lba = 0,
                  native_data_persist_present = 0;
}

class NativeRamBlock;
static NativeRamBlock *gWarmCaptureDisk = nullptr; // Retained only after unique seed selection.
static uint32_t gWarmCaptureAttempted = 0, gWarmCaptureAmbiguous = 0;
// Registry IDs remain unique until reboot. Media/ancestor iterators own their
// references; no global raw service pointer is dereferenced by the root gate.
static uint64_t gWarmTargetID = 0;
static uint32_t gWarmTargetAmbiguous = 0;
static bool warmLoadRequested() {
  uint32_t enabled = 0;
  return PE_parse_boot_argn("nova-data-warm", &enabled, sizeof(enabled)) && enabled == 1;
}
static bool warmLoadConfiguration() {
  uint32_t unused = 0;
  char partition[32] = {};
  return warmLoadRequested() &&
      !PE_parse_boot_argn("nova-data-capture", &unused, sizeof(unused)) &&
      !PE_parse_boot_argn("sheng-data-persist", &unused, sizeof(unused)) &&
      !PE_parse_boot_argn("sheng-data-reset", &unused, sizeof(unused)) &&
      PE_parse_boot_argn("native-ufs-partition", partition, sizeof(partition)) && !strcmp(partition, "unixnu");
}
static bool warmCaptureEnabled() {
  uint32_t enabled = 0;
  char partition[16] = {};
  return !warmLoadRequested() && PE_parse_boot_argn("nova-data-capture", &enabled, sizeof(enabled)) && enabled == 1 &&
      PE_parse_boot_argn("native-ufs-partition", partition, sizeof(partition) - 1) && !strcmp(partition, "unixnu");
}

class NativeRamBlock : public IOBlockStorageDevice {
  OSDeclareDefaultStructors(NativeRamBlock)
  IOLock *gate = nullptr;
  IORecursiveLock *publicationGate = nullptr;
  uint32_t warmCancelled = 0;
  bool publicationStarted = false;
  uint8_t *memory = nullptr;
  uint64_t capacity = 0;
  bool online = false;
  RamSparseDisk sparse = {};
  // A disk above 2 GiB keeps only the nonzero pages of its seed (SeedDisk.h), in memory proportional to the content, and is served by the
  // direct (store-backed) path alone: it needs the persistent store open read-write, or its requests fail.
  SeedDisk big = {};
  bool isBig = false;
  uint8_t seedHash[32] = {};
  uint8_t baselineSeedHash[32] = {};
  persist::Overlay overlay;
  uint8_t *overlayBits = nullptr, *stage = nullptr;   // stage: kStagePages pages, one write or read run
  static constexpr uint32_t kStagePages = 32;
  bool storeAttached = false;
  // Direct mode (the store is open read-write): changed pages live in the store window only, never in RAM; reads of them go to the
  // window. A failed store write stops all further writes (a page the system was told is written must be on the medium).
  bool direct = false, directFailed = false;
  struct StoreOps {
    bool read(uint64_t b, uint32_t n, void *d) { __atomic_fetch_add(&native_data_persist_loaded, n, __ATOMIC_RELAXED); return native_ufs_store_read(b, n, d); }
    bool write(uint64_t b, uint32_t n, const void *s) { return native_ufs_store_write(b, n, s); }
    bool sync() { return native_ufs_store_sync(); }
  };
  void persistFailed() { overlay.active = false; native_data_persist_state = 5; __atomic_fetch_add(&native_data_persist_errors, 1, __ATOMIC_RELAXED); }
  static void warmLoadThread(void *, wait_result_t);
  void warmLoad();
  void publishWarmPrepared();
public:
  bool warmMediaAvailable() {
    if (!gate || __atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE)) return false;
    IOLockLock(gate);
    const bool ready = available() && !__atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE);
    IOLockUnlock(gate);
    return ready;
  }
  int captureWarm(uint64_t captureId) {
    using namespace warmcapture;
    // All memory is allocated before freezing Data. No allocation or logging is
    // performed while gate is held; this follows the existing direct-I/O lock order.
    uint8_t *buffer = static_cast<uint8_t *>(IOMallocZero(kStageBytes + kBlock));
    if (!buffer) return NoBuffer;
    if (!gate) { IOFree(buffer, kStageBytes + kBlock); return NotReady; }
    IOLockLock(gate);
    const uint64_t frozenBegin = mach_absolute_time();
    Status status = NotReady;
    Result result;
    if (online && native_ram_block_ready && storeAttached && &native_owner_data_status && native_owner_data_status == 0) {
      if (!sparse.pages || isBig || direct || directFailed || overlay.active || !sparse.bytes || sparse.bytes > kMaxLogical ||
          !&native_ufs_store_mode || native_ufs_store_mode() != 0 || native_data_persist_state != 0) {
        status = Unsupported;
      } else if (!&native_ufs_export_blocks || !&native_ufs_export_write || !&native_ufs_log_sync ||
                 !&native_ufs_store_gpt_check || native_ufs_export_blocks() != kMaxWindowBlocks ||
                 native_ufs_store_gpt_check() != 0) {
        status = WindowRefused;
      } else {
        struct Source {
          const RamSparseDisk &disk;
          const uint8_t *page(uint64_t i) const { return disk.pages[i]; }
        } source{sparse};
        struct Store {
          bool write(uint64_t block, uint32_t count, const void *bytes) { return native_ufs_export_write(block, count, bytes) != 0; }
          bool sync() { return native_ufs_log_sync() != 0; }
        } store;
        struct Hash {
          SHA256_CTX context;
          void init() { SHA256_Init(&context); }
          void update(const void *bytes, size_t count) { SHA256_Update(&context, bytes, count); }
          void finish(uint8_t *digest) { SHA256_Final(digest, &context); }
        } hash;
        status = warmcapture::capture(source, store, hash, sparse.bytes, native_ufs_export_blocks(),
                                      seedHash, captureId, buffer, kStageBytes, buffer + kStageBytes, result);
      }
    }
    native_warm_capture_frozen_ticks = mach_absolute_time() - frozenBegin;
    IOLockUnlock(gate);
    native_warm_capture_logical_bytes = result.logicalBytes;
    native_warm_capture_bytes = result.encodedBytes;
    native_warm_capture_extents = result.extents;
    native_warm_capture_header_uncertain = result.headerUncertain ? 1 : 0;
    IOFree(buffer, kStageBytes + kBlock);
    return static_cast<int>(status);
  }

  // Called once, before the Data volume is mounted (native_ram_block_volume_seed): open the store window, put the pages that
  // earlier boots changed back into the sparse disk, and from then on write changed pages through. The seed identity
  // (seedHash) is not affected. Any refusal or error leaves the disk volatile, exactly as without persistence.
  // sheng-data-reset=1 or 2 starts the overlay over (the old pages are dropped, not applied); 2 also asks for Setup (BootWatchdogPolicy.c).
  static bool discardRequested() { uint32_t v = 0; return PE_parse_boot_argn("sheng-data-reset", &v, sizeof v) && (v == 1 || v == 2); }
  void attachStore() {
    if (!gate) return;
    IOLockLock(gate);
    if (storeAttached) { IOLockUnlock(gate); return; }
    storeAttached = true;
    unsigned mode = !warmLoadRequested() && &native_ufs_store_mode ? native_ufs_store_mode() : 0;
    if (mode && (sparse.pages || isBig) && &native_ufs_store_open && &native_ufs_store_read && &native_ufs_store_write &&
        &native_ufs_store_sync && &native_ufs_store_gpt_check) {
      uint64_t pages = (isBig ? big.bytes : sparse.bytes) / 4096, first = 0;
      const char *why = "";
      native_data_persist_pages = pages;
      native_data_persist_gpt = native_ufs_store_gpt_check();
      if (!native_ufs_store_open(persist::windowBlocks(pages), &first, &why)) {
        native_data_persist_state = 4;
        IOLog("NativeRamBlock: persistent store refused: %s\n", why);
      } else {
        native_data_persist_first_lba = first;
        uint64_t bb = persist::bitmapBlocks(pages);
        overlayBits = static_cast<uint8_t *>(IOMallocZero(bb * persist::kBlock));
        stage = static_cast<uint8_t *>(IOMallocZero(kStagePages * persist::kBlock));
        StoreOps ops;
        persist::Open result = overlayBits && stage ? overlay.open(ops, pages, seedHash, mode == 1, overlayBits, discardRequested())
                                                          : persist::Open::Failed;
        native_data_persist_header = static_cast<uint32_t>(overlay.header);
        if (result == persist::Open::Failed) {
          native_data_persist_state = 4;
          overlay.active = false;
        } else if (result == persist::Open::Blank) {
          native_data_persist_state = 1;
        } else if (result == persist::Open::Fresh) {
          native_data_persist_state = 3;
          direct = mode == 2;
        } else if (mode == 2) {
          // Resumed read-write: nothing is copied into RAM. The window answers for every page whose bit is set.
          direct = true;
          native_data_persist_state = 2;
        } else {
          struct Loader {
            NativeRamBlock *self;
            bool operator()(uint64_t i, const uint8_t *page) {
              if (i >= self->sparse.bytes / 4096) return false;
              if (!self->sparse.pages[i]) {
                uint8_t *fresh = static_cast<uint8_t *>(IOMallocZero(4096));
                if (!fresh) return false;
                self->sparse.pages[i] = fresh;
                self->sparse.resident += 4096;
              }
              memcpy(self->sparse.pages[i], page, 4096);
              __atomic_fetch_add(&native_data_persist_loaded, 1, __ATOMIC_RELAXED);
              return true;
            }
          } load = { this };
          if (overlay.forEachPresent(ops, load, stage, kStagePages)) {
            native_data_persist_state = mode == 1 ? 6 : 2;
            native_ram_block_resident_bytes = sparse.resident;
          } else {
            // A partly restored disk would be worse than a volatile one, but the pages already copied are consistent
            // overlay content; stop writing back and let the system decide what it can mount.
            native_data_persist_state = 4;
            overlay.active = false;
          }
        }
        native_data_persist_present = overlay.present;
        native_data_persist_gpt_after = native_ufs_store_gpt_check();
      }
    }
    IOLockUnlock(gate);
  }

  bool available() const { return online && (memory || sparse.pages || isBig) && capacity; }
  bool seedInfo(uint64_t *base,uint64_t *bytes,uint8_t hash[32]) {
    if(!gate || !base || !bytes || !hash)return false;
    IOLockLock(gate);bool ready=available();
    if(ready){*base=(sparse.pages||isBig)?0:reinterpret_cast<uint64_t>(memory);*bytes=capacity*512;memcpy(hash,seedHash,32);}
    IOLockUnlock(gate);return ready;
  }
  bool start(IOService *) override;
  void stop(IOService *) override;
  void free() override;
  IOReturn doEjectMedia() override { return kIOReturnUnsupported; }
  IOReturn doFormatMedia(UInt64) override { return kIOReturnUnsupported; }
  UInt32 doGetFormatCapacities(UInt64 *, UInt32) const override { return 0; }
  char *getVendorString() override { return const_cast<char *>("Native"); }
  char *getProductString() override { return const_cast<char *>("Owned RAM block storage"); }
  char *getRevisionString() override { return const_cast<char *>("1.0"); }
  char *getAdditionalDeviceInfoString() override { return const_cast<char *>("volatile storage; no persistent trust claim"); }
  IOReturn reportBlockSize(UInt64 *value) override { *value = 512; return kIOReturnSuccess; }
  IOReturn reportEjectability(bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportMaxValidBlock(UInt64 *value) override { *value = capacity - 1; return kIOReturnSuccess; }
  IOReturn reportMediaState(bool *present, bool *changed) override {
    *present = online; if (changed) *changed = false; return kIOReturnSuccess;
  }
  IOReturn reportRemovability(bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportWriteProtection(bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn doSynchronizeCache() override {
    if (!online) return kIOReturnNotReady;
    // Every changed page is on the medium, with its bitmap bit, before its write completed (PersistOverlay.h): nothing to flush.
    return kIOReturnSuccess;
  }
  IOReturn doSynchronize(UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache(); }
  IOReturn getWriteCacheState(bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn setWriteCacheState(bool) override { return kIOReturnUnsupported; }
  IOReturn doAsyncReadWrite(IOMemoryDescriptor *, UInt64, UInt64, IOStorageAttributes *, IOStorageCompletion *) override;
};
OSDefineMetaClassAndStructors(NativeRamBlock, IOBlockStorageDevice)

bool NativeRamBlock::start(IOService *provider)
{
  if (!IOBlockStorageDevice::start(provider)) return false;
  IORegistryEntry *map = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane);
  if (!map) return false;
  OSData *packed = OSDynamicCast(OSData, map->getProperty("PackedRAMDisk"));
  OSData *range = OSDynamicCast(OSData, map->getProperty("RAMDisk"));
  if(packed && range){map->release();return false;}
  if(packed)range=packed;
  bool packedInput=packed!=nullptr;
  uint64_t physical = 0, bytes = 0;
  if (range && range->getLength() == 16) {
    memcpy(&physical, range->getBytesNoCopy(), 8);
    memcpy(&bytes, static_cast<const uint8_t *>(range->getBytesNoCopy()) + 8, 8);
  }
  map->release();
  // Boot memory belongs to the loader/kernel; never allocate or free it here.
  if (!bytes || bytes > 0x80000000 || (physical & 4095) || (!packedInput && (bytes & 4095)) ||
      physical < gPhysBase || bytes > gPhysSize || physical - gPhysBase > gPhysSize - bytes) return false;
  memory = reinterpret_cast<uint8_t *>(ml_static_ptovirt(physical));
  if (!memory || !(gate = IOLockAlloc())) return false;
  uint64_t logical=bytes;
  if(packedInput){
    if(!ramPackedLayout(memory,bytes,&logical))return false;
    struct ImportOps {uint8_t *allocate(){return static_cast<uint8_t *>(IOMallocZero(4096));}} ops;
    if(logical>0x80000000ULL){
      uint64_t nonzero=0;
      if(!seedCountNonzero(memory,bytes,&nonzero))return false;
      big.bytes=logical;big.capacity=nonzero;
      big.index=static_cast<uint64_t *>(IOMallocZero((nonzero?nonzero:1)*sizeof(uint64_t)));
      big.page=static_cast<uint8_t **>(IOMallocZero((nonzero?nonzero:1)*sizeof(uint8_t *)));
      if(!big.index||!big.page||!seedImportSparse(big,memory,bytes,ops))return false;
      isBig=true;sparse.bytes=logical;
    }else{
      sparse.bytes=logical;
      sparse.pages=static_cast<uint8_t **>(IOMallocZero((logical/4096)*sizeof(uint8_t *)));
      if(!ramSparseImport(sparse,memory,bytes,ops))return false;
    }
  }
  capacity = logical / 512;
  SHA256_CTX context;
  SHA256_Init(&context);
  if(isBig){
    seedIdentity(big,context,[](SHA256_CTX &c){SHA256_Init(&c);},[](SHA256_CTX &c,const void *p,size_t n){SHA256_Update(&c,p,n);},
                 [](SHA256_CTX &c,uint8_t *out){SHA256_Final(out,&c);},seedHash);
    memory=nullptr;
  }else{
  if(sparse.pages){
    static const uint8_t zero[4096]={};
    for(uint64_t i=0;i<logical/4096;++i)SHA256_Update(&context,sparse.pages[i]?sparse.pages[i]:zero,4096);
    memory=nullptr;
  }else SHA256_Update(&context, memory, bytes);
  SHA256_Final(seedHash, &context);
  }
  memcpy(native_ram_block_seed_sha256,seedHash,32);
  memcpy(baselineSeedHash,seedHash,32);
  native_ram_block_bytes = logical;
  native_ram_block_sparse=(sparse.pages||isBig)?1:0;
  native_ram_block_resident_bytes=isBig?big.resident:sparse.pages?sparse.resident:logical;
  if (warmLoadRequested()) {
    uint64_t expected = 0;
    if (!__atomic_compare_exchange_n(&gWarmTargetID, &expected, getRegistryEntryID(), false,
                                     __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
      __atomic_store_n(&gWarmTargetAmbiguous, 1, __ATOMIC_RELEASE);
      __atomic_store_n(&native_warm_load_status, warmload::DuplicateTarget, __ATOMIC_RELEASE);
      return false;
    }
    native_warm_load_start_ticks = mach_absolute_time();
    publicationGate = IORecursiveLockAlloc();
    if (publicationGate) {
      __atomic_store_n(&native_warm_load_status, warmload::Loading, __ATOMIC_RELEASE);
      thread_t thread = nullptr;
      retain();
      if (kernel_thread_start(warmLoadThread, this, &thread) == KERN_SUCCESS) {
        thread_deallocate(thread);
        return true; // Matching is never parked on source readiness or APFS.
      }
      release();
      native_warm_load_status = warmload::ThreadFailed;
      publishWarmPrepared();
      return true;
    }
    native_warm_load_status = warmload::NoMemory;
    native_warm_load_end_ticks = mach_absolute_time();
  }
  online = true;
  native_ram_block_ready = 1;
  registerService();
  return true;
}

void NativeRamBlock::publishWarmPrepared()
{
  // registerService may synchronously cause block I/O. Never hold the I/O
  // gate across it; this separate recursive gate serializes it against stop.
  IORecursiveLockLock(publicationGate);
  IOLockLock(gate);
  bool publish = !publicationStarted && !__atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE);
  if (publish) { publicationStarted = true; online = true; native_ram_block_ready = 1; }
  IOLockUnlock(gate);
  if (publish) {
    native_warm_load_publish_ticks = mach_absolute_time();
    registerService();
  }
  else if (__atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE)) native_warm_load_status = warmload::Cancelled;
  native_warm_load_end_ticks = mach_absolute_time();
  IORecursiveLockUnlock(publicationGate);
}

void NativeRamBlock::warmLoadThread(void *argument, wait_result_t)
{
  NativeRamBlock *self = static_cast<NativeRamBlock *>(argument);
  self->warmLoad();
  self->release();
}

void NativeRamBlock::warmLoad()
{
  using namespace warmload;
  Status status = ConfigRefused;
  SourceHandle handle;
  uint64_t deadline = 0;
  clock_interval_to_deadline(kSourceWaitMs, kMillisecondScale, &deadline);
  if (warmLoadConfiguration() && sparse.pages && !isBig && sparse.bytes == kLogicalBytes &&
      &native_ufs_store_mode && native_ufs_store_mode() == 0 &&
      &native_ufs_warm_source_try_open && &native_ufs_warm_source_read) {
    status = SourceTimeout;
    for (;;) {
      if (__atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE)) { status = Cancelled; break; }
      const int source = native_ufs_warm_source_try_open(&handle);
      if (source > 0) { status = Success; break; }
      if (source < 0) { status = SourceRefused; break; }
      if (mach_absolute_time() >= deadline) break;
      IOSleep(10);
    }
  }
  native_warm_load_source_ticks = mach_absolute_time();
  RamSparseDisk candidate = {};
  Result result;
  struct Ops {
    NativeRamBlock *self;
    SourceHandle handle;
    uint64_t deadline;
    Status check() {
      if (__atomic_load_n(&self->warmCancelled, __ATOMIC_ACQUIRE)) return Cancelled;
      return mach_absolute_time() >= deadline ? BudgetExpired : Success;
    }
    void *allocate(size_t bytes) { return IOMallocZero(bytes); }
    void deallocate(void *pointer, size_t bytes) { IOFree(pointer, bytes); }
    bool read(uint64_t relative, uint32_t blocks, void *buffer) {
      const uint64_t begin = mach_absolute_time();
      const bool ok = native_ufs_warm_source_read(&handle, relative, blocks, buffer) != 0;
      native_warm_load_read_ticks += mach_absolute_time() - begin;
      return ok;
    }
  } ops{this, handle, 0};
  uint8_t *buffer = nullptr;
  if (status == Success) {
    clock_interval_to_deadline(kLoadBudgetMs, kMillisecondScale, &ops.deadline);
    const uint64_t begin = mach_absolute_time();
    buffer = static_cast<uint8_t *>(IOMallocZero(kStageBytes + kBlock));
    if (!buffer) status = NoMemory;
    else {
      struct Hash {
        SHA256_CTX context;
        void init() { native_sha256_stream_init(&context); }
        void update(const void *data, size_t bytes) {
          const uint64_t begin = mach_absolute_time();
          native_sha256_stream_update(&context, data, bytes);
          native_warm_load_hash_ticks += mach_absolute_time() - begin;
        }
        void finish(uint8_t digest[32]) {
          const uint64_t begin = mach_absolute_time();
          native_sha256_stream_final(digest, &context);
          native_warm_load_hash_ticks += mach_absolute_time() - begin;
        }
      };
      status = warmload::load<Ops, Hash>(ops, pinned(), baselineSeedHash, candidate,
                                       buffer, buffer + kStageBytes, result);
    }
    native_warm_load_import_ticks = mach_absolute_time() - begin;
  }
  if (buffer) IOFree(buffer, kStageBytes + kBlock);
  native_warm_load_bytes = result.readBytes;
  memcpy(native_warm_load_payload_sha256, result.payloadDigest, 32);
  memcpy(native_warm_load_logical_sha256, result.logicalDigest, 32);
  if (status == Success) {
    IOLockLock(gate);
    if (__atomic_load_n(&warmCancelled, __ATOMIC_ACQUIRE)) status = Cancelled;
    else if (publicationStarted || online) status = ConfigRefused;
    else {
      RamSparseDisk cold = sparse; sparse = candidate; candidate = cold;
      memcpy(seedHash, result.logicalDigest, 32);
      memcpy(native_ram_block_seed_sha256, seedHash, 32);
      native_ram_block_resident_bytes = sparse.resident;
    }
    IOLockUnlock(gate);
  }
  // On success this releases only the superseded cold pages; otherwise only
  // the private candidate. A failed UFS command may still poison root I/O.
  releaseDisk(candidate, ops);
  __atomic_store_n(&native_warm_load_status, status, __ATOMIC_RELEASE);
  publishWarmPrepared();
}

IOReturn NativeRamBlock::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
    IOStorageAttributes *, IOStorageCompletion *completion)
{
  if (!buffer || !completion || !completion->action || !count || block >= capacity ||
      count > capacity - block || buffer->getLength() < count * 512 ||
      (buffer->getDirection() != kIODirectionIn && buffer->getDirection() != kIODirectionOut)) return kIOReturnBadArgument;
  if (!gate) return kIOReturnNotReady;
  const uint64_t bytes = count * 512;
  IOLockLock(gate);
  if (!online) { IOLockUnlock(gate); return kIOReturnNotReady; }
  if (buffer->getDirection() == kIODirectionIn &&
      __atomic_load_n(&gWarmTargetID, __ATOMIC_ACQUIRE) == getRegistryEntryID()) {
    uint64_t expected = 0;
    __atomic_compare_exchange_n(&native_warm_load_first_read_ticks, &expected, mach_absolute_time(),
                                false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  }
  uint64_t actual;bool noMemory=false;
  if(isBig && !direct) {
    // Before the store is attached (APFS probes the disk for its volume UUID long before the kernel asks for the Data volume, which is what
    // attaches the store) a disk this size is read from its seed alone; it has no volatile mode, so writes wait.
    if(buffer->getDirection()==kIODirectionIn) {
      struct SeedReadOps {
        IOMemoryDescriptor *buffer;
        uint64_t copy(uint64_t at,const void *p,uint64_t n){return buffer->writeBytes(at,p,n);}
      } rops={buffer};
      static const uint8_t zero[4096]={};
      uint64_t done=0,offset=block*512;
      while(done<bytes) {
        uint64_t at=offset+done,index=at/4096,skip=at&4095,n=4096-skip;
        if(n>bytes-done)n=bytes-done;
        const uint8_t *page=big.find(index);
        uint64_t got=rops.copy(done,(page?page:zero)+skip,n);
        done+=got;if(got!=n)break;
      }
      __atomic_fetch_add(&native_ram_block_reads,1,__ATOMIC_RELAXED);
      IOReturn early=done==bytes?kIOReturnSuccess:kIOReturnIOError;
      if(early)__atomic_fetch_add(&native_ram_block_failures,1,__ATOMIC_RELAXED);
      IOLockUnlock(gate);
      completion->action(completion->target,completion->parameter,early,done);
      return kIOReturnSuccess;
    }
    IOLockUnlock(gate);return kIOReturnNotReady;
  }
  if(sparse.pages||isBig){
    struct Operations {
      IOMemoryDescriptor *buffer;
      uint8_t *allocate(){return static_cast<uint8_t *>(IOMallocZero(4096));}
      uint64_t read(uint64_t at,void *p,uint64_t n){return buffer->readBytes(at,p,n);}
      uint64_t copy(uint64_t at,const void *p,uint64_t n){return buffer->writeBytes(at,p,n);}
    } ops={buffer};
    bool write=buffer->getDirection()==kIODirectionOut;
    if(direct){
      struct BufferOps {
        IOMemoryDescriptor *buffer;
        uint64_t read(uint64_t at,void *p,uint64_t n){return buffer->readBytes(at,p,n);}
        uint64_t copy(uint64_t at,const void *p,uint64_t n){return buffer->writeBytes(at,p,n);}
      } bops={buffer};
      StoreOps store;bool storeError=false;
      if(write && directFailed)actual=0;
      else actual=isBig?directDiskIO(big,overlay,store,stage,kStagePages,write,block*512,bytes,bops,&storeError)
                       :directDiskIO(sparse,overlay,store,stage,kStagePages,write,block*512,bytes,bops,&storeError);
      if(storeError){
        if(write)directFailed=true;
        native_data_persist_state=5;__atomic_fetch_add(&native_data_persist_errors,1,__ATOMIC_RELAXED);
      }
      if(write && actual){__atomic_fetch_add(&native_data_persist_writes,(actual+4095)/4096,__ATOMIC_RELAXED);__atomic_fetch_add(&native_data_persist_syncs,1,__ATOMIC_RELAXED);native_data_persist_present=overlay.present;}
    } else {
    actual=ramSparseIO(sparse,write,block*512,bytes,ops,noMemory);
    }
    if(!direct && write && actual==bytes && overlay.active && !overlay.readOnly){
      StoreOps store;
      uint64_t firstPage=block*512/4096,lastPage=(block*512+bytes-1)/4096;
      for(uint64_t i=firstPage;i<=lastPage && overlay.active;){
        uint32_t n=0;
        while(n<kStagePages && i+n<=lastPage && sparse.pages[i+n]){memcpy(stage+(uint64_t)n*persist::kBlock,sparse.pages[i+n],persist::kBlock);++n;}
        if(!n || !overlay.writeRun(store,i,n,stage)){persistFailed();break;}
        __atomic_fetch_add(&native_data_persist_writes,n,__ATOMIC_RELAXED);
        __atomic_fetch_add(&native_data_persist_syncs,1,__ATOMIC_RELAXED);   // one run = one durable commit
        native_data_persist_present=overlay.present;
        i+=n;
      }
    }
    if(write)__atomic_fetch_add(&native_ram_block_writes,1,__ATOMIC_RELAXED);
    else __atomic_fetch_add(&native_ram_block_reads,1,__ATOMIC_RELAXED);
    native_ram_block_resident_bytes=isBig?big.resident:sparse.resident;
  }else if (buffer->getDirection() == kIODirectionIn) {
    actual = buffer->writeBytes(0, memory + block * 512, bytes);
    __atomic_fetch_add(&native_ram_block_reads, 1, __ATOMIC_RELAXED);
  } else {
    actual = buffer->readBytes(0, memory + block * 512, bytes);
    __atomic_fetch_add(&native_ram_block_writes, 1, __ATOMIC_RELAXED);
  }
  IOReturn status = actual == bytes ? kIOReturnSuccess : noMemory ? kIOReturnNoMemory : kIOReturnIOError;
  if (status) __atomic_fetch_add(&native_ram_block_failures, 1, __ATOMIC_RELAXED);
  IOLockUnlock(gate);
  completion->action(completion->target, completion->parameter, status, actual);
  return kIOReturnSuccess;
}

void NativeRamBlock::stop(IOService *provider)
{
  __atomic_store_n(&warmCancelled, 1, __ATOMIC_RELEASE);
  if (publicationGate) IORecursiveLockLock(publicationGate);
  if (gate) { IOLockLock(gate); online = false; IOLockUnlock(gate); }
  IOBlockStorageDevice::stop(provider);
  if (publicationGate) IORecursiveLockUnlock(publicationGate);
}

void NativeRamBlock::free()
{
  if (publicationGate) { IORecursiveLockFree(publicationGate); publicationGate = nullptr; }
  if (overlayBits) { IOFree(overlayBits, persist::bitmapBlocks(sparse.bytes / 4096) * persist::kBlock); overlayBits = nullptr; }
  if (stage) { IOFree(stage, kStagePages * persist::kBlock); stage = nullptr; }
  if (isBig) {
    for (uint64_t k = 0; k < big.count; ++k) IOFree(big.page[k], 4096);
    if (big.index) IOFree(big.index, (big.capacity ? big.capacity : 1) * sizeof(uint64_t));
    if (big.page) IOFree(big.page, (big.capacity ? big.capacity : 1) * sizeof(uint8_t *));
    big = {}; isBig = false;
  }
  if (gate) { IOLockFree(gate); gate = nullptr; }
  if(sparse.pages){
    for(uint64_t i=0;i<sparse.bytes/4096;++i)if(sparse.pages[i])IOFree(sparse.pages[i],4096);
    IOFree(sparse.pages,(sparse.bytes/4096)*sizeof(uint8_t *));sparse={};
  }
  // Dense boot allocation and packed input remain owned until reboot.
  IOBlockStorageDevice::free();
}

extern "C" int native_ram_block_warm_media_ready(void)
{
  if (!warmLoadRequested() || __atomic_load_n(&gWarmTargetAmbiguous, __ATOMIC_ACQUIRE)) return -1;
  const uint64_t target = __atomic_load_n(&gWarmTargetID, __ATOMIC_ACQUIRE);
  if (!target) return 0;
  OSDictionary *matching = IOService::serviceMatching("IOMedia");
  OSDictionary *properties = OSDictionary::withCapacity(1);
  OSString *wanted = OSString::withCString(warmload::kDataUuid);
  OSIterator *iterator = nullptr;
  if (matching && properties && wanted && properties->setObject("UUID", wanted) &&
      matching->setObject("IOPropertyMatch", properties)) iterator = IOService::getMatchingServices(matching);
  int result = 0;
  unsigned matches = 0;
  if (iterator) {
    while (OSObject *object = iterator->getNextObject()) {
      if (++matches != 1) { result = -1; break; }
      IOService *media = OSDynamicCast(IOService, object);
      if (!media) continue;
      OSString *uuid = OSDynamicCast(OSString, media->getProperty("UUID"));
      OSString *name = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
      OSNumber *major = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMajorKey));
      OSNumber *minor = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMinorKey));
      if (!uuid || strcmp(uuid->getCStringNoCopy(), warmload::kDataUuid) || !name || !name->getLength() || name->getLength() >= 58 ||
          !major || !minor || major->unsigned64BitValue() > 255 || minor->unsigned64BitValue() > 0xffffff) continue;
      IORegistryIterator *parents = IORegistryIterator::iterateOver(media, gIOServicePlane,
          kIORegistryIterateRecursively | kIORegistryIterateParents);
      if (parents) {
        for (unsigned depth = 0; depth < 256 && parents->isValid(); ++depth) {
          IORegistryEntry *entry = parents->getNextObject();
          if (!entry) break;
          NativeRamBlock *ram = OSDynamicCast(NativeRamBlock, entry);
          if (!ram || ram->getRegistryEntryID() != target) continue;
          ram->retain();
          result = ram->warmMediaAvailable() && parents->isValid() ? 1 : 0;
          ram->release();
          break;
        }
        parents->release();
      }
    }
    if (!iterator->isValid()) result = 0;
  }
  OSSafeReleaseNULL(iterator); OSSafeReleaseNULL(wanted);
  OSSafeReleaseNULL(properties); OSSafeReleaseNULL(matching);
  return result;
}

extern "C" void native_ram_block_warm_log_stats(void)
{
  if (!warmLoadRequested()) return;
  const unsigned status = __atomic_load_n(&native_warm_load_status, __ATOMIC_ACQUIRE);
  char payload[65] = {}, logical[65] = {};
  for (unsigned i = 0; i < 32; ++i) {
    snprintf(payload + i * 2, 3, "%02x", native_warm_load_payload_sha256[i]);
    snprintf(logical + i * 2, 3, "%02x", native_warm_load_logical_sha256[i]);
  }
  printf("nova: warmload status=%u capture_id=%llu bytes=%llu start_ticks=%llu source_ticks=%llu read_ticks=%llu hash_ticks=%llu import_ticks=%llu publish_ticks=%llu end_ticks=%llu first_read_ticks=%llu\n",
      status,
      (unsigned long long)warmload::kCaptureId, (unsigned long long)native_warm_load_bytes,
      (unsigned long long)native_warm_load_start_ticks, (unsigned long long)native_warm_load_source_ticks,
      (unsigned long long)native_warm_load_read_ticks, (unsigned long long)native_warm_load_hash_ticks,
      (unsigned long long)native_warm_load_import_ticks, (unsigned long long)native_warm_load_publish_ticks,
      (unsigned long long)native_warm_load_end_ticks,
      (unsigned long long)native_warm_load_first_read_ticks);
  printf("nova: warmload digests verified=%u payload_sha256=%s logical_sha256=%s\n",
      status == warmload::Success ? 1U : 0U, payload, logical);
}

static int selectVolumeSeed(const char *uuid, dev_t *device, char *path, size_t size,
    uint64_t *seedBase,uint64_t *seedBytes,uint8_t seedDigest[32])
{
  if (!uuid || strlen(uuid) != 36 || !device || !path || size < 6) return 0;
  *device = NODEV;
  path[0] = 0;
  OSDictionary *matching = IOService::serviceMatching("IOMedia");
  OSDictionary *properties = OSDictionary::withCapacity(1);
  OSString *wanted = OSString::withCString(uuid);
  if (!matching || !properties || !wanted) {
    OSSafeReleaseNULL(wanted); OSSafeReleaseNULL(properties); OSSafeReleaseNULL(matching);
    return 0;
  }
  bool ready = properties->setObject("UUID", wanted) && matching->setObject("IOPropertyMatch", properties);
  wanted->release(); properties->release();
  OSIterator *iterator = ready ? IOService::getMatchingServices(matching) : nullptr;
  matching->release();
  if (!iterator) return 0;
  bool selected = false;
  unsigned matches = 0;
  dev_t candidate = NODEV;
  char candidatePath[64] = {};
  uint64_t base=0,bytes=0;uint8_t hash[32]={};
  const bool pinCapture = seedDigest && warmCaptureEnabled();
  NativeRamBlock *selectedRam = nullptr;
  while (OSObject *object = iterator->getNextObject()) {
    IOService *media = OSDynamicCast(IOService, object);
    if (!media) continue;
    if (++matches != 1) { selected = false; break; }
    OSString *identity = OSDynamicCast(OSString, media->getProperty("UUID"));
    OSString *name = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
    OSNumber *mjr = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMajorKey));
    OSNumber *mnr = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMinorKey));
    if (!identity || strcmp(identity->getCStringNoCopy(), uuid) || !name || !mjr || !mnr ||
        !name->getLength() || name->getLength() >= sizeof(candidatePath) - 6 || name->getLength() >= size - 6 ||
        mjr->unsigned64BitValue() > 255 || mnr->unsigned64BitValue() > 0xffffff) continue;
    unsigned depth = 0;
    for (IOService *provider = media; provider && depth++ < 64; provider = provider->getProvider()) {
      NativeRamBlock *ram = OSDynamicCast(NativeRamBlock, provider);
      if (!ram) continue;
      ram->attachStore();
      if (!ram->seedInfo(&base,&bytes,hash)) continue;
      candidate = static_cast<dev_t>(makedev(mjr->unsigned32BitValue(), mnr->unsigned32BitValue()));
      snprintf(candidatePath, sizeof(candidatePath), "/dev/%s", name->getCStringNoCopy());
      selected = true;
      if (pinCapture) { selectedRam = ram; selectedRam->retain(); }
      break;
    }
  }
  iterator->release();
  kprintf("NativeRamBlock: seed uuid=%.36s matches=%u selected=%d ready=%d bytes=%llu\n", uuid,
      matches, selected ? 1 : 0, (unsigned)native_ram_block_ready, (unsigned long long)native_ram_block_bytes);
  if (!selected || matches != 1) { OSSafeReleaseNULL(selectedRam); return 0; }
  if (seedDigest && selectedRam) {
    NativeRamBlock *expected = nullptr;
    if (__atomic_compare_exchange_n(&gWarmCaptureDisk, &expected, selectedRam, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      selectedRam = nullptr; // The one-shot target owns this retained reference.
    } else if (expected != selectedRam) {
      __atomic_store_n(&gWarmCaptureAmbiguous, 1, __ATOMIC_RELEASE);
    }
  }
  OSSafeReleaseNULL(selectedRam);
  *device = candidate;
  strlcpy(path, candidatePath, size);
  if(seedBase)*seedBase=base;
  if(seedBytes)*seedBytes=bytes;
  if(seedDigest)memcpy(seedDigest,hash,32);
  return 1;
}

extern "C" int native_ram_block_volume_device(const char *uuid,dev_t *device,char *path,size_t size) {
  return selectVolumeSeed(uuid,device,path,size,nullptr,nullptr,nullptr);
}

extern "C" int native_ram_block_capture_warm(uint64_t captureId) {
  if (!warmCaptureEnabled() || !captureId) return warmcapture::Disabled;
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&gWarmCaptureAttempted, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return warmcapture::AlreadyAttempted;
  native_warm_capture_start_ticks = mach_absolute_time();
  NativeRamBlock *disk = __atomic_load_n(&gWarmCaptureDisk, __ATOMIC_ACQUIRE);
  const int result = disk && !__atomic_load_n(&gWarmCaptureAmbiguous, __ATOMIC_ACQUIRE)
      ? disk->captureWarm(captureId) : warmcapture::NotReady;
  native_warm_capture_end_ticks = mach_absolute_time();
  __atomic_store_n(&native_warm_capture_status, static_cast<uint32_t>(result), __ATOMIC_RELEASE);
  // The Data gate and all UFS calls have returned before emitting diagnostics.
  IOLog("NOVA-DATACAP result capture_id=%llu status=%d logical=%llu encoded=%llu extents=%llu start_ticks=%llu end_ticks=%llu frozen_ticks=%llu header_uncertain=%u\n",
      (unsigned long long)captureId, result, (unsigned long long)native_warm_capture_logical_bytes,
      (unsigned long long)native_warm_capture_bytes, (unsigned long long)native_warm_capture_extents,
      (unsigned long long)native_warm_capture_start_ticks, (unsigned long long)native_warm_capture_end_ticks,
      (unsigned long long)native_warm_capture_frozen_ticks, (unsigned)native_warm_capture_header_uncertain);
  return result;
}
// The GPU addon the loader placed after the trust caches (/chosen/memory-map/GpuAddon, scripts/make-gpu-addon.py), for the kernel's
// boot-time addon install (kern_exec.c). Boot memory, like the seed: never freed here. Returns 0 without one.
extern "C" int native_ram_block_gpu_addon(const uint8_t **data,uint64_t *bytes) {
  if(!data || !bytes)return 0;
  *data=nullptr;*bytes=0;
  IORegistryEntry *map=IORegistryEntry::fromPath("/chosen/memory-map",gIODTPlane);
  if(!map)return 0;
  OSData *range=OSDynamicCast(OSData,map->getProperty("GpuAddon"));
  uint64_t physical=0,size=0;
  if(range && range->getLength()==16){
    memcpy(&physical,range->getBytesNoCopy(),8);
    memcpy(&size,static_cast<const uint8_t *>(range->getBytesNoCopy())+8,8);
  }
  map->release();
  if(!size || size>(UINT64_C(64)<<20) || (physical&4095) || physical<gPhysBase || size>gPhysSize || physical-gPhysBase>gPhysSize-size)return 0;
  *data=reinterpret_cast<const uint8_t *>(ml_static_ptovirt(physical));
  if(!*data)return 0;
  *bytes=size;
  return 1;
}

extern "C" int native_ram_block_volume_seed(const char *uuid,dev_t *device,char *path,size_t size,
    uint64_t *base,uint64_t *bytes,uint8_t digest[32]) {
  if(!base || !bytes || !digest)return 0;
  *base=0;*bytes=0;memset(digest,0,32);
  return selectVolumeSeed(uuid,device,path,size,base,bytes,digest);
}
