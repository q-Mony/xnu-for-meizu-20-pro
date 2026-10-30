// SPDX-License-Identifier: BSD-2-Clause-Patent
// Starts the owned touch digitizer (drivers/NativeHidInput) on the Meizu 20 Pro.
//
// NativeHidInput publishes one IOHIDDevice whose report descriptor is the touch-only collection
// (Digitizer / Touch Screen, Built-In). It is attached to the genuine IOHIDResource nub that IOHIDFamily
// creates, so backboardd sees a normal touch service; no Apple driver is replaced and no entitlement is
// used. The class ABI (207 virtual slots, 208-byte instance) is verified against this board's own
// prelinked image by scripts/build-native-meizu-hid.py before this file is ever linked.
//
// Boot arguments:
//   meizu-touch=<n>     n >= 3 also starts the digitizer (the same switch that starts the probe)
//   meizu-touch-hid=1   starts it on its own; =0 keeps it off even when touch is on
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSKext.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>

extern "C" {
extern volatile uint32_t native_meizu_touch_stats[16];
extern volatile uint32_t native_meizu_rpmh_stats[8];
extern volatile int native_touch_error;
}

extern kmod_info_t g_kernel_kmod_info;
extern "C" void native_hid_static_init(void) __asm__("__GLOBAL__sub_I_NativeHidInput.cpp");
extern "C" IOService *native_hid_touch_allocate(void);
// Board storage probe; only compiled in when its archive is part of the build.
#ifdef HAVE_MEIZU_STORAGE_METADATA
extern "C" void meizu_storage_metadata_start(void);
#endif
extern "C" {
extern kern_return_t OSKextLoadKextWithIdentifier(const char *);

// [0] started, [1] IOHIDFamily load, [2] IOHIDEventDriver load, [3] IOHIDResource found,
// [4] metaclass status, [5] init, [6] attach, [7] start
volatile uint32_t native_meizu_hid_stats[8];
// Report attempts the digitizer's timer makes; indexed by the event number NativeHidInput passes.
volatile uint32_t native_meizu_hid_events[8];
volatile int native_meizu_hid_results[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
}

namespace {
IORegistryEntry *eventDriver(IORegistryEntry *parent, unsigned depth) {
    if (!depth) return nullptr;
    OSIterator *it = parent->getChildIterator(gIOServicePlane);
    if (!it) return nullptr;
    IORegistryEntry *found = nullptr;
    while (OSObject *child = it->getNextObject()) {
        IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, child);
        if (!entry) continue;
        if (entry->metaCast("IOHIDEventDriver")) {found = entry; break;}
        if ((found = eventDriver(entry, depth - 1))) break;
    }
    it->release();
    return found;
}
}

extern "C" void native_hid_input_observed(unsigned event, int result) {
    if (event < 8) {
        native_meizu_hid_events[event] = native_meizu_hid_events[event] + 1;
        native_meizu_hid_results[event] = result;
    }
}

// Shared with NativeHidInput's one-second timer. backboardd classifies a digitizer by the event service it
// opened, not by the HID device below it, so the flags go on IOHIDEventDriver and its interface once.
extern "C" void native_hid_live_checkpoint(IOService *device) {
    IORegistryEntry *driver = eventDriver(device, 4);
    if (!driver) return;
    static IORegistryEntry *publishedFor;
    if (publishedFor == driver) return;
    if (!device->getProperty("Built-In")) return;
    publishedFor = driver;
    driver->setProperty("Built-In", kOSBooleanTrue);
    driver->setProperty("DisplayIntegrated", kOSBooleanTrue);
    driver->setProperty("PrimaryUsagePage", (unsigned long long)0x0d, 32);
    driver->setProperty("PrimaryUsage", (unsigned long long)4, 32);
    if (IORegistryEntry *interface = driver->getParentEntry(gIOServicePlane)) {
        interface->setProperty("Built-In", kOSBooleanTrue);
        interface->setProperty("DisplayIntegrated", kOSBooleanTrue);
        interface->setProperty("PrimaryUsagePage", (unsigned long long)0x0d, 32);
        interface->setProperty("PrimaryUsage", (unsigned long long)4, 32);
    }
}

extern "C" bool native_hid_input_requested(void) {return false;}

// NativeHidInput references this sink for its keyboard/consumer device, which this board does not
// start (no button driver yet). A definition keeps that reference resolvable; the touch-only device
// never reaches it.
extern "C" void native_buttons_set_sink(void (*sink)(void *, uint32_t), void *context) {
    (void)sink;
    (void)context;
}

extern "C" void native_meizu_hid_probe(void) {
    kprintf("meizu-hid: probe enter\n");
    unsigned stage = 0, on = 1;
    bool enabled = PE_parse_boot_argn("meizu-touch", &stage, sizeof stage) && stage >= 3;
    if (PE_parse_boot_argn("meizu-touch-hid", &on, sizeof on)) enabled = on == 1;
    if (!enabled) return;
    if (native_meizu_hid_stats[0]) return;
    native_meizu_hid_stats[0] = 1;
    kprintf("meizu-hid: loading IOHIDFamily\n");
    int load = OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily");
    kprintf("meizu-hid: IOHIDFamily load=%d\n", load);
    native_meizu_hid_stats[1] = (uint32_t)load;
    if (load) {
        IOLog("meizu-hid: IOHIDFamily load failed %d\n", load);
        return;
    }
    kprintf("meizu-hid: loading IOHIDEventDriver\n");
    int events = OSKext::loadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver", true, false,
                                                kOSKextExcludeNone, kOSKextExcludeNone, nullptr);
    kprintf("meizu-hid: IOHIDEventDriver load=%d\n", events);
    native_meizu_hid_stats[2] = (uint32_t)events;
    kprintf("meizu-hid: matching IOHIDResource\n");
    OSDictionary *match = IOService::serviceMatching("IOHIDResource");
    IOService *resource = match ? IOService::waitForMatchingService(match, 5000000000ULL) : nullptr;
    kprintf("meizu-hid: IOHIDResource=%p\n", resource);
    native_meizu_hid_stats[3] = resource != nullptr;
    if (!resource) {
        IOLog("meizu-hid: no IOHIDResource after 5 s\n");
    } else {
        kprintf("meizu-hid: premod\n");
        void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
        kprintf("meizu-hid: premod=%p\n", handle);
        if (handle) {
            native_hid_static_init();
            kprintf("meizu-hid: static init done\n");
            int status = OSMetaClass::postModLoad(handle);
            kprintf("meizu-hid: postmod=%d\n", status);
            native_meizu_hid_stats[4] = (uint32_t)status;
            if (status) {
                IOLog("meizu-hid: metaclass registration failed %d\n", status);
            } else {
                IOService *touch = native_hid_touch_allocate();
                kprintf("meizu-hid: touch=%p\n", touch);
                bool valid = touch && touch->metaCast("IOHIDDevice") && touch->init();
                kprintf("meizu-hid: init valid=%d\n", valid);
                native_meizu_hid_stats[5] = valid;
                bool attached = valid && touch->attach(resource);
                kprintf("meizu-hid: attach=%d\n", attached);
                native_meizu_hid_stats[6] = attached;
                bool started = attached && touch->start(resource);
                kprintf("meizu-hid: start=%d\n", started);
                native_meizu_hid_stats[7] = started;
                if (!started) {
                    IOLog("meizu-hid: digitizer init/attach/start %d/%d/%d\n", valid, attached, started);
                    if (touch) {
                        if (attached) touch->detach(resource);
                        OSSafeReleaseNULL(touch);
                    }
                } else {
                    IOLog("meizu-hid: digitizer started\n");
                }
            }
        }
    }
    IODelay(2000000);
    kprintf("meizu-digest: stage=%u err=%d st=%x,%x,%x,%x bus=%x,%x\n",
            (unsigned)native_meizu_touch_stats[0], native_touch_error,
            (unsigned)native_meizu_touch_stats[1], (unsigned)native_meizu_touch_stats[2],
            (unsigned)native_meizu_touch_stats[3], (unsigned)native_meizu_touch_stats[4],
            (unsigned)native_meizu_touch_stats[5], (unsigned)native_meizu_touch_stats[6]);
    kprintf("meizu-digest: src=%u addr=%x head=%x point=%x panel=%x defbus=%x\n",
            (unsigned)native_meizu_touch_stats[9], (unsigned)native_meizu_touch_stats[10],
            (unsigned)(native_meizu_touch_stats[11] >> 16),
            (unsigned)(native_meizu_touch_stats[11] & 0xffff),
            (unsigned)native_meizu_touch_stats[12], (unsigned)native_meizu_touch_stats[15]);
    kprintf("meizu-digest: rsc=%x avdd=%x rc=%x seen=%x iovdd=%x rc=%x seen=%x\n",
            (unsigned)native_meizu_rpmh_stats[0], (unsigned)native_meizu_rpmh_stats[2],
            (unsigned)native_meizu_rpmh_stats[3], (unsigned)native_meizu_rpmh_stats[6],
            (unsigned)native_meizu_rpmh_stats[4], (unsigned)native_meizu_rpmh_stats[5],
            (unsigned)native_meizu_rpmh_stats[7]);
    OSSafeReleaseNULL(resource);
    OSSafeReleaseNULL(match);
#ifdef HAVE_MEIZU_STORAGE_METADATA
    meizu_storage_metadata_start();
#endif
}
