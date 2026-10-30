// SPDX-License-Identifier: MIT
// Meizu 20 Pro disposable Data experiment: only a matching/interest endpoint.
// No AppleKeyStore metaclass, user client, selector, keybag or lock-state API.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
#include <string.h>
#include "MeizuAksNotificationPolicy.h"

extern "C" unsigned native_ufs_store_mode(void);
extern "C" volatile uint32_t native_data_persist_state;
extern kmod_info_t g_kernel_kmod_info;
extern "C" void meizu_aks_static_init(void) __asm__("__GLOBAL__sub_I_MeizuKeyStoreNotification.cpp");

class MeizuKeyStoreNotification final : public IOService {
    OSDeclareDefaultStructors(MeizuKeyStoreNotification)
public:
    // Both IOService user-client entry points are closed, including the
    // properties variant used by IOServiceOpenExtended.
    IOReturn newUserClient(task_t, void *, UInt32, IOUserClient **out) override {
        if (out) *out = nullptr;
        return kIOReturnUnsupported;
    }
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **out) override {
        if (out) *out = nullptr;
        return kIOReturnUnsupported;
    }
};
OSDefineMetaClassAndStructors(MeizuKeyStoreNotification, IOService)

extern "C" {
// 0 off, 1 invalid opt-in, 2 Data guard refused, 3 real class present,
// 4 allocation/attachment failure, 5 published; the lookup bridge checks 5.
volatile unsigned meizu_keystore_notification_status;
}

static bool volatileDataConfirmed(int dataError)
{
    if (dataError || native_ufs_store_mode() != 0 || native_data_persist_state != 0) return false;
    IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
    IORegistryEntry *map = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane);
    OSData *flag = chosen ? OSDynamicCast(OSData, chosen->getProperty("ephemeral-storage")) : nullptr;
    OSData *ram = map ? OSDynamicCast(OSData, map->getProperty("PackedRAMDisk")) : nullptr;
    bool valid = meizuAksVolatileData(dataError, native_ufs_store_mode(),
        native_data_persist_state, flag ? flag->getBytesNoCopy() : nullptr,
        flag ? flag->getLength() : 0, ram ? ram->getLength() : 0);
    OSSafeReleaseNULL(map);
    OSSafeReleaseNULL(chosen);
    return valid;
}

extern "C" void meizu_keystore_notification_publish(int dataError)
{
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("meizu-aks-notify", &enabled, sizeof(enabled))) return;
    if (enabled != 1) { meizu_keystore_notification_status = 1; return; }
    if (meizu_keystore_notification_status) return;
    if (!volatileDataConfirmed(dataError)) {
        meizu_keystore_notification_status = 2;
        IOLog("meizu-aks-notify: refused: Data is not confirmed volatile\n");
        return;
    }
    const OSSymbol *name = OSSymbol::withCString("AppleKeyStore");
    const OSMetaClass *real = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    if (!name) { meizu_keystore_notification_status = 4; return; }
    OSSafeReleaseNULL(name);
    if (real) { meizu_keystore_notification_status = 3; return; }
    auto realMatch = IOService::serviceMatching("AppleKeyStore");
    if (!realMatch) { meizu_keystore_notification_status = 4; return; }
    IOService *existing = IOService::copyMatchingService(realMatch);
    OSSafeReleaseNULL(realMatch);
    if (existing) {
        existing->release();
        meizu_keystore_notification_status = 3;
        return;
    }
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) { meizu_keystore_notification_status = 4; return; }
    meizu_aks_static_init();
    if (OSMetaClass::postModLoad(handle) != kIOReturnSuccess) {
        meizu_keystore_notification_status = 4;
        return;
    }
    // The loaded metaclass is not published by itself; only our own service
    // instance may be matched by the bridge.
    IOService *platform = IOService::getPlatform();
    if (!platform) { meizu_keystore_notification_status = 4; return; }
    auto *service = new MeizuKeyStoreNotification;
    if (!service || !service->init()) {
        meizu_keystore_notification_status = 4;
        OSSafeReleaseNULL(service);
        return;
    }
    service->setName("MeizuKeyStoreNotification");
    if (!service->setProperty("KeyOperationsAvailable", false)) {
        meizu_keystore_notification_status = 4;
        service->release();
        return;
    }
    if (!service->attach(platform)) {
        meizu_keystore_notification_status = 4;
        service->release();
        return;
    }
    // The service is complete before publication; IOGeneralInterest belongs to
    // IOService and requires no synthetic events or power state.
    meizu_keystore_notification_status = 5;
    service->registerService(kIOServiceSynchronous);
    if (!(service->getState() & kIOServiceMatchedState)) {
        meizu_keystore_notification_status = 4;
        IOLog("meizu-aks-notify: registerService did not match\n");
        return; // keep the allocation reference while attached
    }
    IOLog("meizu-aks-notify: published notification endpoint\n");
    // Keep the allocation reference while the service remains published;
    // registry attachment is not an OSObject retain of this instance.
}

// Called only after the original AppleKeyStore lookup failed. The bridge in
// IOUserClient.cpp restricts the request to an exact class-only dictionary.
extern "C" IOService *meizu_keystore_notification_lookup(void)
{
    if (meizu_keystore_notification_status != 5) return nullptr;
    // Keep the fallback closed if persistent backing is selected later.
    if (!volatileDataConfirmed(0)) return nullptr;
    // A late original provider takes precedence even after this endpoint exists.
    auto realMatch = IOService::serviceMatching("AppleKeyStore");
    if (!realMatch) return nullptr;
    IOService *real = IOService::copyMatchingService(realMatch);
    realMatch->release();
    if (real) return real;
    // A loaded real class supersedes the fixture even before it publishes.
    const OSSymbol *name = OSSymbol::withCString("AppleKeyStore");
    if (!name) return nullptr;
    const OSMetaClass *meta = OSMetaClass::getMetaClassWithName(name);
    name->release();
    if (meta) return nullptr;
    auto matching = IOService::serviceMatching("MeizuKeyStoreNotification");
    if (!matching) return nullptr;
    auto found = IOService::copyMatchingService(matching);
    matching->release();
    return found; // copyMatchingService returns an owned reference
}
