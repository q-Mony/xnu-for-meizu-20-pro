/*
 * Diagnostic only. The 20H392 watchdogd panic selector (2) must never be
 * acknowledged without a hardware reset or kernel panic path. Do not publish
 * an IOWatchdog service until all live selectors have a verified implementation.
 */
#include "MeizuWatchdog.h"

#define super IOService
OSDefineMetaClassAndStructors(IOWatchdog, IOService);

bool
IOWatchdog::start(IOService *provider)
{
	(void)provider;
	/* Fail closed even if a future build accidentally instantiates this class. */
	return false;
}

IOReturn
IOWatchdog::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    IOUserClient **handler)
{
	(void)owningTask;
	(void)securityID;
	(void)type;
	if (handler != nullptr) {
		*handler = nullptr;
	}
	return kIOReturnUnsupported;
}
