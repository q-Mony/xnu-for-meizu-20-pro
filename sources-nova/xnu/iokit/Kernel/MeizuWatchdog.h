#ifndef _IOKIT_MEIZU_WATCHDOG_H
#define _IOKIT_MEIZU_WATCHDOG_H

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>

/* Deliberately unregistered diagnostic: no safe 20H392 selector 2 backend. */
class IOWatchdog : public IOService
{
	OSDeclareDefaultStructors(IOWatchdog);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
	    IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};

#endif
