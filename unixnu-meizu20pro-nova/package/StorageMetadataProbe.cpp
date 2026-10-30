// SPDX-License-Identifier: MIT
// Board-side storage metadata probe. Default off; boot argument meizu-storage-metadata=1.
//
// NativeUfsBlock subclasses the original IOBlockStorageDevice, and the kernel link needs that
// class bound to the prelinked image's absolute addresses. The target kernelcache keeps no
// symbol table for kext classes, so the addresses have to be read from a running kernel:
// the class registry gives the metaclass, an instance gives the vtable.
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <pexpert/pexpert.h>

extern "C" {

static void printWords(const char *label, const uintptr_t *words, unsigned count)
{
  for (unsigned at = 0; at < count; at += 4) {
    kprintf("SMP %s+%03x: %016lx %016lx %016lx %016lx\n", label, at * 8,
            (unsigned long)words[at], (unsigned long)words[at + 1 < count ? at + 1 : at],
            (unsigned long)words[at + 2 < count ? at + 2 : at],
            (unsigned long)words[at + 3 < count ? at + 3 : at]);
  }
}

static void storageMetadataProbe(void)
{
  kprintf("SMP: storage metadata probe\n");
  // The prelinked storage kext may register its classes after this archive's static
  // initializer runs, so wait instead of failing: the console is live at this point and a
  // panic here would be invisible.
  const OSMetaClass *meta = nullptr;
  for (unsigned attempt = 0; attempt < 300 && !meta; attempt++) {
    const OSSymbol *name = OSSymbol::withCStringNoCopy("IOBlockStorageDevice");
    if (name) {
      meta = OSMetaClass::getMetaClassWithName(name);
    }
    if (!meta) {
      IODelay(100000);
    }
  }
  if (!meta) {
    kprintf("SMP: IOBlockStorageDevice not registered\n");
    return;
  }
  const OSMetaClass *super = meta->getSuperClass();
  kprintf("SMP meta=%p class=%s super=%p superclass=%s\n", meta, meta->getClassName(),
          super, super ? super->getClassName() : "-");

  // The class is abstract, so there is no instance to read a vtable from. The loader patches
  // the metaclass into slot 1 of every vtable for the class; a vtable's slot 0 is zero and its
  // destructor slots point into the prelinked text.
  const uintptr_t *const begin = reinterpret_cast<const uintptr_t *>(0xfffffff005c60000ULL);
  const uintptr_t *const end = reinterpret_cast<const uintptr_t *>(0xfffffff00600a000ULL);
  auto inPrelinkedText = [](uintptr_t value) {
    return (value >= 0xfffffff003dc8000ULL && value < 0xfffffff00458c000ULL) ||
           (value >= 0xfffffff00458c000ULL && value < 0xfffffff005c60000ULL);
  };
  const uintptr_t *best = nullptr;
  unsigned hits = 0;
  for (const uintptr_t *at = begin; at + 8 < end; ++at) {
    if (*at != reinterpret_cast<uintptr_t>(meta)) continue;
    hits++;
    if (!best && at[-1] == 0 && inPrelinkedText(at[2]) && inPrelinkedText(at[3])) best = at;
  }
  kprintf("SMP scan hits=%u best=%p\n", hits, best ? best - 1 : nullptr);
  if (best) {
    const uintptr_t *vtable = best - 1;
    kprintf("SMP vt: %016lx %016lx %016lx %016lx\n",
            (unsigned long)vtable[0], (unsigned long)vtable[1],
            (unsigned long)vtable[2], (unsigned long)vtable[3]);
    kprintf("SMP vt4: %016lx %016lx %016lx %016lx\n",
            (unsigned long)vtable[4], (unsigned long)vtable[5],
            (unsigned long)vtable[6], (unsigned long)vtable[7]);
    kprintf("SMP vt8: %016lx %016lx %016lx %016lx\n",
            (unsigned long)vtable[8], (unsigned long)vtable[9],
            (unsigned long)vtable[10], (unsigned long)vtable[11]);
  }
  printWords("meta", reinterpret_cast<const uintptr_t *>(meta), 12);
  kprintf("SMP: storage metadata captured\n");
}

// Called from the board HID probe: the module initializer of a force-loaded archive does not
// run in this kernel, so the probe is triggered explicitly. It runs on the boot thread because
// the frame buffer console only draws that thread's kprintf output.
extern "C" void meizu_storage_metadata_start(void)
{
  unsigned enabled = 0;
  if (!PE_parse_boot_argn("meizu-storage-metadata", &enabled, sizeof(enabled)) || !enabled) {
    return;
  }
  kprintf("SMP: start requested\n");
  storageMetadataProbe();
}

}
