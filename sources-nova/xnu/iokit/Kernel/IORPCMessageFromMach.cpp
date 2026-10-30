/*
 * The open source drop declares IORPCMessageFromMach for user space only; the
 * shipping kernel exports it, and the prelinked 16.7.16 components bind to it.
 * Reproduce the shipped implementation: validate the IORPC message id, walk
 * the bounded port-descriptor array, then return the IORPCMessage after it.
 */
#include <IOKit/IORPC.h>
#include <libkern/OSDebug.h>

extern "C" IORPCMessage *
IORPCMessageFromMach(IORPCMessageMach *msg, bool reply)
{
	mach_msg_id_t expected = reply ? kIORPCVersion190615Reply : kIORPCVersion190615;

	if (msg->msgh.msgh_id != expected) {
		OSReportWithBacktrace("IORPCMessageFromMach: bad message id 0x%x",
		    msg->msgh.msgh_id);
		return NULL;
	}

	mach_msg_size_t count = msg->msgh_body.msgh_descriptor_count;
	IORPCMessage *message = (IORPCMessage *)msg->objects;

	if (count != 0) {
		char *end = (char *)msg + msg->msgh.msgh_size;

		for (;;) {
			if ((char *)message >= end) {
				return NULL;
			}
			if ((*(uint32_t *)((char *)message + 8) >> 25) != 0) {
				return NULL;
			}
			message = (IORPCMessage *)((char *)message +
			    sizeof(mach_msg_port_descriptor_t));
			if (--count == 0) {
				break;
			}
		}
	}

	return message;
}
