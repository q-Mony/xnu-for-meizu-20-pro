// SPDX-License-Identifier: MIT
// Board-side original PMGR ABI qualification. Default off. Modes: 1 builds and
// verifies the original nub without publishing it, 2 publishes it, 3 tests
// power refusal. No clocks, sleep, reset or secondary-CPU success is
// advertised; every failure stops with the step and its pointers.
//
// AMFI (16.7.16) only needs a published ApplePMGRNub: it waits for the
// service, casts it to ApplePMGRNub, then follows nub+0x88 to the provider
// vtable slots 312/313. The provider is never started, so no device tree,
// register map or hardware power state is involved.
#include "OriginalPMGRABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void sheng_pmgr_static_init(void) __asm__("__GLOBAL__sub_I_ShengPlatformPower.cpp");
IOService *sheng_original_pmgr_nub_create(IORegistryEntry *, IOService *);
uint32_t sheng_original_pmgr_nub_read(IOService *);
void sheng_original_pmgr_nub_write(IOService *, uint64_t);
// stage,status,parent-size,own-size,provider,nub,bound-provider,read-before,
// read-after,own-before,own-after,nub-before,nub-after,attached,registered,mode.
volatile uint64_t sheng_pmgr_observation[16];
__attribute__((noinline)) void sheng_pmgr_checkpoint(void) { __asm__ volatile("" ::: "memory"); }
}

__attribute__((noreturn)) static void unavailable(unsigned slot)
{
  sheng_pmgr_observation[0] = 0x100 + slot;
  sheng_pmgr_observation[1] = static_cast<uint32_t>(kIOReturnUnsupported);
  sheng_pmgr_checkpoint();
  panic("ShengPlatformPower: unimplemented operation %u", slot);
}

// The original nub's init casts its provider to ApplePMGR::gMetaClass and
// faults at [NULL+0xe4] otherwise, so the provider is the kext's own
// AppleBringUpPMGR (its AON slots 312/313 are the base 0xffffffff/no-op).
// This guarded subclass stays as the explicit power-refusal probe.
OSDefineMetaClassWithInit(ShengPmgrBase, IOPMGR, )
OSMetaClassConstructorInit(ShengPmgrBase, IOPMGR, )
OSDefineAbstractStructors(ShengPmgrBase, IOPMGR)

class ShengPlatformPower : public ShengPmgrBase {
  OSDeclareDefaultStructors(ShengPlatformPower)
public:
  void enableCPUCore(unsigned, uint64_t) override { unavailable(168); }
  void enableCPUCore(unsigned) override { unavailable(169); }
  void disableCPUCore(unsigned) override { unavailable(170); }
  void enableCPUCluster(unsigned) override { unavailable(171); }
  void disableCPUCluster(unsigned) override { unavailable(172); }
  void initCPUIdle(ml_processor_info_t *) override { unavailable(173); }
  void enterCPUIdle(UInt64 *) override { unavailable(174); }
  void exitCPUIdle(UInt64 *) override { unavailable(175); }
  void updateCPUIdle(UInt64 *) override { unavailable(176); }
#include "GuardedPMGRSlots.inc"
};
OSDefineMetaClassWithInit(ShengPlatformPower, ShengPmgrBase, )
OSMetaClassConstructorInit(ShengPlatformPower, ShengPmgrBase, )
OSDefineBasicStructors(ShengPlatformPower, ShengPmgrBase)
static_assert(sizeof(ShengPlatformPower) == 0x36968, "Target ApplePMGR storage");
// OSObject's zeroed, size-aware allocation pair (small object; no original
// ApplePMGR storage is inherited any more).
void *ShengPlatformPower::operator new(size_t size) { return OSObject::operator new(size); }
void ShengPlatformPower::operator delete(void *memory, size_t size)
{
  OSObject::operator delete(memory, size);
}

static const OSMetaClass *lookup(const char *text)
{
  const OSSymbol *name = OSSymbol::withCString(text);
  const OSMetaClass *meta = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
  OSSafeReleaseNULL(name);
  return meta;
}


extern "C" void sheng_pmgr_run(void)
{
  unsigned mode = 0;

  if (!PE_parse_boot_argn("sheng-pmgr", &mode, sizeof(mode)) || !mode) return;
  sheng_pmgr_observation[15] = mode;
  if (mode > 3) return;
  sheng_pmgr_observation[0] = 1;

  kern_return_t result = OSKextLoadKextWithIdentifier("com.apple.driver.ApplePMGR");
  kprintf("sheng-pmgr: 1 load ApplePMGR=%x\n", result);
  if (result) {
    sheng_pmgr_observation[1] = static_cast<uint32_t>(result);
    sheng_pmgr_checkpoint();
    return;
  }

  const OSMetaClass *parent = lookup("ApplePMGR");
  const OSMetaClass *nubMeta = lookup("ApplePMGRNub");
  if (!parent || !nubMeta || parent->getClassSize() != 0x36968 ||
      nubMeta->getClassSize() != 0x740) {
    panic("sheng-pmgr: 2 meta parent=%p nub=%p", parent, nubMeta);
  }
  sheng_pmgr_observation[2] = parent->getClassSize();
  sheng_pmgr_observation[9] = parent->getInstanceCount();
  sheng_pmgr_observation[11] = nubMeta->getInstanceCount();

  const OSMetaClass *providerMeta = lookup("AppleBringUpPMGR");
  if (!providerMeta || providerMeta->getClassSize() != 0x36968) {
    panic("sheng-pmgr: 2 provider meta=%p", providerMeta);
  }
  kprintf("sheng-pmgr: 2 meta parent=%p nub=%p provider=%p mode=%u\n",
      parent, nubMeta, providerMeta, mode);
  kprintf("2a nubmeta=%p size=%x\n", nubMeta, nubMeta->getClassSize());

  void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
  if (!handle) {
    panic("sheng-pmgr: 2 preload failed");
  }
  sheng_pmgr_static_init();
  result = OSMetaClass::postModLoad(handle);
  if (result) {
    panic("sheng-pmgr: 2 postload=%x", result);
  }

  // The original nub's init casts its provider to ApplePMGR::gMetaClass, so a
  // plain IOPMGR object faults at [NULL+0xe4]. The kext's own BringUp class is
  // the concrete ApplePMGR subclass and inherits the AON read/write slots.
  IOService *provider = static_cast<IOService *>(providerMeta->alloc());
  if (!provider) {
    panic("sheng-pmgr: 3 alloc meta=%p", providerMeta);
  }
  IORegistryEntry *description = new IORegistryEntry;
  if (!description) {
    panic("sheng-pmgr: 3 description alloc");
  }
  if (!provider->init()) {
    panic("sheng-pmgr: 3 provider init object=%p", provider);
  }
  if (!description->init()) {
    panic("sheng-pmgr: 3 description init object=%p", description);
  }
  // Empty resource description: no Apple register map or device capabilities.
  description->setName("sheng-platform-power");
  kprintf("sheng-pmgr: 3 provider=%p description=%p\n", provider, description);

  kprintf("3a nubmeta=%p\n", nubMeta);
  IOService *nub = sheng_original_pmgr_nub_create(description, provider);
  if (!nub) {
    panic("sheng-pmgr: 3 nub create provider=%p", provider);
  }
  // AMFI casts the nub with the metaclass baked into its own image, so the
  // name lookup result is informational only. The nub's own metaclass must
  // still be an ApplePMGRNub (size 0x740) for that cast to succeed.
  if (nub->getMetaClass()->getClassSize() != 0x740) {
    panic("sheng-pmgr: 3 nub meta=%p size=%x lookup=%p", nub->getMetaClass(),
        nub->getMetaClass()->getClassSize(), nubMeta);
  }
  kprintf("sheng-pmgr: 3 nub meta=%p (%s) lookup=%p (%s)\n", nub->getMetaClass(),
      nub->getMetaClass()->getClassName(), nubMeta, nubMeta->getClassName());
  uintptr_t bound = 0;
  memcpy(&bound, reinterpret_cast<const char *>(nub) + 0x88, sizeof(bound));
  if (bound != reinterpret_cast<uintptr_t>(provider)) {
    panic("sheng-pmgr: 3 bound=%p provider=%p",
        reinterpret_cast<void *>(bound), provider);
  }
  kprintf("sheng-pmgr: 3 nub=%p bound=%p\n", nub, reinterpret_cast<void *>(bound));

  uint32_t value = sheng_original_pmgr_nub_read(nub);
  sheng_pmgr_observation[7] = value;
  sheng_original_pmgr_nub_write(nub, uint64_t(value | 4));
  sheng_pmgr_observation[8] = sheng_original_pmgr_nub_read(nub);
  kprintf("sheng-pmgr: 3 aon read=%x write=%x\n", value,
      (unsigned)sheng_pmgr_observation[8]);
  if (value != 0xffffffff || sheng_pmgr_observation[8] != 0xffffffff) {
    panic("sheng-pmgr: 3 aon read=%x write=%x", value,
        (unsigned)sheng_pmgr_observation[8]);
  }

  // Mode 3 must not reach the real power methods of the original object.
  if (mode == 3) unavailable(170);
  if (mode != 2) {
    sheng_pmgr_observation[1] = kIOReturnSuccess;
    sheng_pmgr_checkpoint();
    return;
  }

  IOService *platform = IOService::getPlatform();
  kprintf("sheng-pmgr: 4 platform=%p provider=%p nub=%p\n",
      platform, provider, nub);
  if (!platform) {
    panic("sheng-pmgr: 4 platform");
  }
  if (!provider->attach(platform)) {
    panic("sheng-pmgr: 4 attach provider=%p platform=%p", provider, platform);
  }
  kprintf("sheng-pmgr: 4 provider attached parent=%p\n", provider->getProvider());
  if (!nub->attach(provider)) {
    panic("sheng-pmgr: 4 attach nub=%p provider=%p", nub, provider);
  }
  kprintf("sheng-pmgr: 4 nub attached parent=%p state=%x\n",
      nub->getProvider(), (unsigned)nub->getState());
  // Publication only: AMFI reads the nub's provider slots; neither start()
  // nor any hardware resource is needed for that. registerService() runs
  // doServiceMatch, which marks the service matched for waiters.
  nub->registerService(kIOServiceSynchronous);
  sheng_pmgr_observation[13] =
      nub->getProvider() == provider && provider->getProvider() == platform;
  sheng_pmgr_observation[14] = nub->getState() & kIOServiceRegisteredState;
  kprintf("sheng-pmgr: 5 registered attached=%u registered=%u state=%x\n",
      (unsigned)sheng_pmgr_observation[13], (unsigned)sheng_pmgr_observation[14],
      (unsigned)nub->getState());

  auto matching = IOService::serviceMatching("ApplePMGRNub");
  IOService *matched = matching ? IOService::waitForMatchingService(matching, 1000000) : nullptr;
  OSSafeReleaseNULL(matching);
  if (!matched) {
    // Same bits doServiceMatch sets; AMFI's search only needs these.
    volatile uint32_t *state = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<char *>(nub) + 0x48);
    *state |= static_cast<uint32_t>(kIOServiceRegisteredState) |
        static_cast<uint32_t>(kIOServiceMatchedState);
    kprintf("sheng-pmgr: 5 no match, state=%x\n", (unsigned)*state);
    matching = IOService::serviceMatching("ApplePMGRNub");
    matched = matching ? IOService::waitForMatchingService(matching, 1000000) : nullptr;
    OSSafeReleaseNULL(matching);
  }
  kprintf("sheng-pmgr: 5 matched=%p nub=%p state=%x\n",
      matched, nub, (unsigned)nub->getState());
  if (matched != nub) {
    panic("sheng-pmgr: 5 matched=%p nub=%p", matched, nub);
  }
  sheng_pmgr_observation[0] = 5;
  sheng_pmgr_observation[1] = kIOReturnSuccess;
  OSSafeReleaseNULL(matched);
  sheng_pmgr_checkpoint();
}
