char meizu_cache_mntfrom[64];
char meizu_cache_fstype[16];
#include <kern/boot_progress.h>
/*
 * Copyright (c) 2000-2020 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/* Copyright (c) 1995 NeXT Computer, Inc. All Rights Reserved */
/*
 * Mach Operating System
 * Copyright (c) 1987 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)kern_exec.c	8.1 (Berkeley) 6/10/93
 */
/*
 * NOTICE: This file was modified by SPARTA, Inc. in 2005 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 */
#include <machine/reg.h>
#include <machine/cpu_capabilities.h>

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/proc_internal.h>
#include <sys/kauth.h>
#include <sys/user.h>
#include <sys/socketvar.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/mount_internal.h>
#include <sys/vnode_internal.h>
#include <sys/file_internal.h>
#include <sys/stat.h>
#include <sys/uio_internal.h>
#include <sys/acct.h>
#include <sys/exec.h>
#include <sys/kdebug.h>
#include <sys/signal.h>
#include <sys/aio_kern.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/persona.h>
#include <sys/reason.h>
#if SYSV_SHM
#include <sys/shm_internal.h>           /* shmexec() */
#endif
#include <sys/ubc_internal.h>           /* ubc_map() */
#include <sys/spawn.h>
#include <sys/spawn_internal.h>
#include <sys/process_policy.h>
#include <sys/codesign.h>
#include <sys/random.h>
#include <crypto/sha1.h>

#include <libkern/libkern.h>
#include <libkern/crypto/sha2.h>
#include <security/audit/audit.h>

#include <ipc/ipc_types.h>

#include <mach/mach_param.h>
#include <mach/mach_types.h>
#include <mach/port.h>
#include <mach/task.h>
#include <mach/task_access.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#include <mach/mach_vm.h>
#include <mach/vm_param.h>
#include <mach_debug/mach_debug_types.h>

#include <kern/sched_prim.h> /* thread_wakeup() */
#include <kern/affinity.h>
#include <kern/assert.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/recount.h>
#include <machine/config.h>
#include <kern/coalition.h>
#include <kern/policy_internal.h>
#include <kern/kalloc.h>
#include <kern/zalloc.h> /* zone_userspace_reboot_checks() */

#include <os/log.h>

#if CONFIG_MACF
#include <security/mac_framework.h>
#include <security/mac_mach_internal.h>
#endif

#if CONFIG_AUDIT
#include <bsm/audit_kevents.h>
#endif

#if CONFIG_ARCADE
#include <kern/arcade.h>
#endif

#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_protos.h>
#include <vm/vm_kern.h>
#include <vm/vm_fault.h>
#include <vm/vm_pageout.h>
#include <vm/pmap.h>
#include <vm/vm_reclaim_internal.h>

#include <kdp/kdp_dyld.h>

#include <machine/machine_routines.h>
#include <machine/pal_routines.h>

#include <pexpert/pexpert.h>

#if CONFIG_MEMORYSTATUS
#include <sys/kern_memorystatus.h>
#endif

#include <IOKit/IOBSD.h>

#include "kern_exec_internal.h"

#include <CoreEntitlements/CoreEntitlements.h>

#include <mach/exclaves.h>

extern boolean_t vm_darkwake_mode;

/* enable crash reports on various exec failures */
static TUNABLE(bool, bootarg_execfailurereports, "execfailurecrashes", false);

#if XNU_TARGET_OS_OSX
#if __has_feature(ptrauth_calls)
static TUNABLE(bool, bootarg_arm64e_preview_abi, "-arm64e_preview_abi", false);
#endif /* __has_feature(ptrauth_calls) */

#if DEBUG || DEVELOPMENT
static TUNABLE(bool, unentitled_ios_sim_launch, "unentitled_ios_sim_launch", false);
#endif /* DEBUG || DEVELOPMENT */
#endif /* XNU_TARGET_OS_OSX */

#if CONFIG_DTRACE
/* Do not include dtrace.h, it redefines kmem_[alloc/free] */
extern void dtrace_proc_exec(proc_t);
extern void (*dtrace_proc_waitfor_exec_ptr)(proc_t);

/*
 * Since dtrace_proc_waitfor_exec_ptr can be added/removed in dtrace_subr.c,
 * we will store its value before actually calling it.
 */
static void (*dtrace_proc_waitfor_hook)(proc_t) = NULL;

#include <sys/dtrace_ptss.h>
#endif

#if __has_feature(ptrauth_calls)
static TUNABLE_DEV_WRITEABLE(int, vm_shared_region_per_team_id,
    "vm_shared_region_per_team_id", 1);
static TUNABLE_DEV_WRITEABLE(int, vm_shared_region_by_entitlement,
    "vm_shared_region_by_entitlement", 1);

/* Upon userland request, reslide the shared cache. */
static TUNABLE_DEV_WRITEABLE(int, vm_shared_region_reslide_aslr,
    "vm_shared_region_reslide_aslr",
#if CONFIG_RESLIDE_SHARED_CACHE
    1
#else
    0
#endif /* CONFIG_RESLIDE_SHARED_CACHE */
    );

/*
 * Flag to control what processes should get shared cache randomize resliding
 * after a fault in the shared cache region:
 *
 * 0 - all processes get a new randomized slide
 * 1 - only platform processes get a new randomized slide
 */
TUNABLE_DEV_WRITEABLE(int, vm_shared_region_reslide_restrict,
    "vm_shared_region_reslide_restrict", 1);

#if DEVELOPMENT || DEBUG
SYSCTL_INT(_vm, OID_AUTO, vm_shared_region_per_team_id,
    CTLFLAG_RW, &vm_shared_region_per_team_id, 0, "");
SYSCTL_INT(_vm, OID_AUTO, vm_shared_region_by_entitlement,
    CTLFLAG_RW, &vm_shared_region_by_entitlement, 0, "");
SYSCTL_INT(_vm, OID_AUTO, vm_shared_region_reslide_restrict,
    CTLFLAG_RW, &vm_shared_region_reslide_restrict, 0, "");
SYSCTL_INT(_vm, OID_AUTO, vm_shared_region_reslide_aslr,
    CTLFLAG_RW, &vm_shared_region_reslide_aslr, 0, "");
#endif
#endif /* __has_feature(ptrauth_calls) */

#if DEVELOPMENT || DEBUG
static TUNABLE(bool, enable_dext_coredumps_on_panic, "dext_panic_coredump", true);
#else
static TUNABLE(bool, enable_dext_coredumps_on_panic, "dext_panic_coredump", false);
#endif
extern kern_return_t kern_register_userspace_coredump(task_t task, const char * name);
#define USERSPACE_COREDUMP_PANIC_ENTITLEMENT "com.apple.private.enable-coredump-on-panic"
#define USERSPACE_COREDUMP_PANIC_SEED_ENTITLEMENT \
	"com.apple.private.enable-coredump-on-panic-seed-privacy-approved"

extern void proc_apply_task_networkbg_internal(proc_t, thread_t);
extern void task_set_did_exec_flag(task_t task);
extern void task_clear_exec_copy_flag(task_t task);
proc_t proc_exec_switch_task(proc_t old_proc, proc_t new_proc, task_t old_task,
    task_t new_task, struct image_params *imgp, void **inherit);
boolean_t task_is_active(task_t);
boolean_t thread_is_active(thread_t thread);
void thread_copy_resource_info(thread_t dst_thread, thread_t src_thread);
void *ipc_importance_exec_switch_task(task_t old_task, task_t new_task);
extern void ipc_importance_release(void *elem);
extern boolean_t task_has_watchports(task_t task);
extern void task_set_no_smt(task_t task);
#if defined(HAS_APPLE_PAC)
char *task_get_vm_shared_region_id_and_jop_pid(task_t task, uint64_t *jop_pid);
#endif
task_t convert_port_to_task(ipc_port_t port);

#if CONFIG_EXCLAVES
int task_add_conclave(task_t task, void *vnode, int64_t off, const char *task_conclave_id);
kern_return_t task_inherit_conclave(task_t old_task, task_t new_task, void *vnode, int64_t off);
#endif /* CONFIG_EXCLAVES */


/*
 * Mach things for which prototypes are unavailable from Mach headers
 */
#define IPC_OBJECT_COPYIN_FLAGS_ALLOW_IMMOVABLE_SEND 0x1
void            ipc_task_enable(
	task_t          task);
void            ipc_task_reset(
	task_t          task);
void            ipc_thread_reset(
	thread_t        thread);
kern_return_t ipc_object_copyin(
	ipc_space_t             space,
	mach_port_name_t        name,
	mach_msg_type_name_t    msgt_name,
	ipc_object_t            *objectp,
	mach_port_context_t     context,
	mach_msg_guard_flags_t  *guard_flags,
	uint32_t                kmsg_flags);
void ipc_port_release_send(ipc_port_t);

#if DEVELOPMENT || DEBUG
void task_importance_update_owner_info(task_t);
#endif

extern struct savearea *get_user_regs(thread_t);

__attribute__((noinline)) int __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(mach_port_t task_access_port, int32_t new_pid);

#include <kern/thread.h>
#include <kern/task.h>
#include <kern/ast.h>
#include <kern/mach_loader.h>
#include <kern/mach_fat.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <machine/vmparam.h>
#include <sys/imgact.h>

#include <sys/sdt.h>


/*
 * EAI_ITERLIMIT	The maximum number of times to iterate an image
 *			activator in exec_activate_image() before treating
 *			it as malformed/corrupt.
 */
#define EAI_ITERLIMIT           3

/*
 * For #! interpreter parsing
 */
#define IS_WHITESPACE(ch) ((ch == ' ') || (ch == '\t'))
#define IS_EOL(ch) ((ch == '#') || (ch == '\n'))

extern vm_map_t bsd_pageable_map;
extern const struct fileops vnops;
extern int nextpidversion;


#define USER_ADDR_ALIGN(addr, val) \
	( ( (user_addr_t)(addr) + (val) - 1) \
	        & ~((val) - 1) )

/*
 * For subsystem root support
 */
#define SPAWN_SUBSYSTEM_ROOT_ENTITLEMENT "com.apple.private.spawn-subsystem-root"

/*
 * Allow setting p_crash_behavior to trigger panic on crash
 */
#define SPAWN_SET_PANIC_CRASH_BEHAVIOR "com.apple.private.spawn-panic-crash-behavior"

/* Platform Code Exec Logging */
static int platform_exec_logging = 0;

SYSCTL_DECL(_security_mac);

SYSCTL_INT(_security_mac, OID_AUTO, platform_exec_logging, CTLFLAG_RW, &platform_exec_logging, 0,
    "log cdhashes for all platform binary executions");

static os_log_t peLog = OS_LOG_DEFAULT;

struct exception_port_action_t {
	ipc_port_t port;
	_ps_port_action_t *port_action;
};

struct exec_port_actions {
	uint32_t exception_port_count;
	uint32_t portwatch_count;
	uint32_t registered_count;
	struct exception_port_action_t *excport_array;
	ipc_port_t *portwatch_array;
	ipc_port_t registered_array[TASK_PORT_REGISTER_MAX];
};

struct image_params;    /* Forward */
static int exec_activate_image(struct image_params *imgp);
static int exec_copyout_strings(struct image_params *imgp, user_addr_t *stackp);
static int load_return_to_errno(load_return_t lrtn);
static int execargs_alloc(struct image_params *imgp);
static int execargs_free(struct image_params *imgp);
static int exec_check_permissions(struct image_params *imgp);
static int exec_extract_strings(struct image_params *imgp);
static int exec_add_apple_strings(struct image_params *imgp, const load_result_t *load_result);
static int exec_handle_sugid(struct image_params *imgp);
static int sugid_scripts = 0;
SYSCTL_INT(_kern, OID_AUTO, sugid_scripts, CTLFLAG_RW | CTLFLAG_LOCKED, &sugid_scripts, 0, "");
static kern_return_t create_unix_stack(vm_map_t map, load_result_t* load_result, proc_t p);
static int copyoutptr(user_addr_t ua, user_addr_t ptr, int ptr_size);
static void exec_resettextvp(proc_t, struct image_params *);
static int process_signature(proc_t, struct image_params *);
static void exec_prefault_data(proc_t, struct image_params *, load_result_t *);
static errno_t exec_handle_port_actions(struct image_params *imgp,
    struct exec_port_actions *port_actions);
static errno_t exec_handle_exception_port_actions(const struct image_params *imgp,
    const struct exec_port_actions *port_actions);
static errno_t exec_handle_spawnattr_policy(proc_t p, thread_t thread, int psa_apptype, uint64_t psa_qos_clamp,
    task_role_t psa_darwin_role, struct exec_port_actions *port_actions);
static void exec_port_actions_destroy(struct exec_port_actions *port_actions);

/*
 * exec_add_user_string
 *
 * Add the requested string to the string space area.
 *
 * Parameters;	struct image_params *		image parameter block
 *		user_addr_t			string to add to strings area
 *		int				segment from which string comes
 *		boolean_t			TRUE if string contributes to NCARGS
 *
 * Returns:	0			Success
 *		!0			Failure errno from copyinstr()
 *
 * Implicit returns:
 *		(imgp->ip_strendp)	updated location of next add, if any
 *		(imgp->ip_strspace)	updated byte count of space remaining
 *		(imgp->ip_argspace) updated byte count of space in NCARGS
 */
__attribute__((noinline))
static int
exec_add_user_string(struct image_params *imgp, user_addr_t str, int seg, boolean_t is_ncargs)
{
	int error = 0;

	do {
		size_t len = 0;
		int space;

		if (is_ncargs) {
			space = imgp->ip_argspace; /* by definition smaller than ip_strspace */
		} else {
			space = imgp->ip_strspace;
		}

		if (space <= 0) {
			error = E2BIG;
			break;
		}

		if (!UIO_SEG_IS_USER_SPACE(seg)) {
			char *kstr = CAST_DOWN(char *, str);     /* SAFE */
			error = copystr(kstr, imgp->ip_strendp, space, &len);
		} else {
			error = copyinstr(str, imgp->ip_strendp, space, &len);
		}

		imgp->ip_strendp += len;
		imgp->ip_strspace -= len;
		if (is_ncargs) {
			imgp->ip_argspace -= len;
		}
	} while (error == ENAMETOOLONG);

	return error;
}

/*
 * dyld is now passed the executable path as a getenv-like variable
 * in the same fashion as the stack_guard and malloc_entropy keys.
 */
#define EXECUTABLE_KEY "executable_path="

/*
 * exec_save_path
 *
 * To support new app package launching for Mac OS X, the dyld needs the
 * first argument to execve() stored on the user stack.
 *
 * Save the executable path name at the bottom of the strings area and set
 * the argument vector pointer to the location following that to indicate
 * the start of the argument and environment tuples, setting the remaining
 * string space count to the size of the string area minus the path length.
 *
 * Parameters;	struct image_params *		image parameter block
 *		char *				path used to invoke program
 *		int				segment from which path comes
 *
 * Returns:	int			0	Success
 *		EFAULT				Bad address
 *	copy[in]str:EFAULT			Bad address
 *	copy[in]str:ENAMETOOLONG		Filename too long
 *
 * Implicit returns:
 *		(imgp->ip_strings)		saved path
 *		(imgp->ip_strspace)		space remaining in ip_strings
 *		(imgp->ip_strendp)		start of remaining copy area
 *		(imgp->ip_argspace)		space remaining of NCARGS
 *		(imgp->ip_applec)		Initial applev[0]
 *
 * Note:	We have to do this before the initial namei() since in the
 *		path contains symbolic links, namei() will overwrite the
 *		original path buffer contents.  If the last symbolic link
 *		resolved was a relative pathname, we would lose the original
 *		"path", which could be an absolute pathname. This might be
 *		unacceptable for dyld.
 */
static int
exec_save_path(struct image_params *imgp, user_addr_t path, int seg, const char **excpath)
{
	int error;
	size_t len;
	char *kpath;

	// imgp->ip_strings can come out of a cache, so we need to obliterate the
	// old path.
	memset(imgp->ip_strings, '\0', strlen(EXECUTABLE_KEY) + MAXPATHLEN);

	len = MIN(MAXPATHLEN, imgp->ip_strspace);

	switch (seg) {
	case UIO_USERSPACE32:
	case UIO_USERSPACE64:   /* Same for copyin()... */
		error = copyinstr(path, imgp->ip_strings + strlen(EXECUTABLE_KEY), len, &len);
		break;
	case UIO_SYSSPACE:
		kpath = CAST_DOWN(char *, path); /* SAFE */
		error = copystr(kpath, imgp->ip_strings + strlen(EXECUTABLE_KEY), len, &len);
		break;
	default:
		error = EFAULT;
		break;
	}

	if (!error) {
		bcopy(EXECUTABLE_KEY, imgp->ip_strings, strlen(EXECUTABLE_KEY));
		len += strlen(EXECUTABLE_KEY);

		imgp->ip_strendp += len;
		imgp->ip_strspace -= len;

		if (excpath) {
			*excpath = imgp->ip_strings + strlen(EXECUTABLE_KEY);
		}
	}

	return error;
}

/*
 * exec_reset_save_path
 *
 * If we detect a shell script, we need to reset the string area
 * state so that the interpreter can be saved onto the stack.
 *
 * Parameters;	struct image_params *		image parameter block
 *
 * Returns:	int			0	Success
 *
 * Implicit returns:
 *		(imgp->ip_strings)		saved path
 *		(imgp->ip_strspace)		space remaining in ip_strings
 *		(imgp->ip_strendp)		start of remaining copy area
 *		(imgp->ip_argspace)		space remaining of NCARGS
 *
 */
static int
exec_reset_save_path(struct image_params *imgp)
{
	imgp->ip_strendp = imgp->ip_strings;
	imgp->ip_argspace = NCARGS;
	imgp->ip_strspace = (NCARGS + PAGE_SIZE);

	return 0;
}

/*
 * exec_shell_imgact
 *
 * Image activator for interpreter scripts.  If the image begins with
 * the characters "#!", then it is an interpreter script.  Verify the
 * length of the script line indicating the interpreter is not in
 * excess of the maximum allowed size.  If this is the case, then
 * break out the arguments, if any, which are separated by white
 * space, and copy them into the argument save area as if they were
 * provided on the command line before all other arguments.  The line
 * ends when we encounter a comment character ('#') or newline.
 *
 * Parameters;	struct image_params *	image parameter block
 *
 * Returns:	-1			not an interpreter (keep looking)
 *		-3			Success: interpreter: relookup
 *		>0			Failure: interpreter: error number
 *
 * A return value other than -1 indicates subsequent image activators should
 * not be given the opportunity to attempt to activate the image.
 */
static int
exec_shell_imgact(struct image_params *imgp)
{
	char *vdata = imgp->ip_vdata;
	char *ihp;
	char *line_startp, *line_endp;
	char *interp;

	/*
	 * Make sure it's a shell script.  If we've already redirected
	 * from an interpreted file once, don't do it again.
	 */
	if (vdata[0] != '#' ||
	    vdata[1] != '!' ||
	    (imgp->ip_flags & IMGPF_INTERPRET) != 0) {
		return -1;
	}

	if (imgp->ip_origcputype != 0) {
		/* Fat header previously matched, don't allow shell script inside */
		return -1;
	}

	imgp->ip_flags |= IMGPF_INTERPRET;
	imgp->ip_interp_sugid_fd = -1;
	imgp->ip_interp_buffer[0] = '\0';

	/* Check to see if SUGID scripts are permitted.  If they aren't then
	 * clear the SUGID bits.
	 * imgp->ip_vattr is known to be valid.
	 */
	if (sugid_scripts == 0) {
		imgp->ip_origvattr->va_mode &= ~(VSUID | VSGID);
	}

	/* Try to find the first non-whitespace character */
	for (ihp = &vdata[2]; ihp < &vdata[IMG_SHSIZE]; ihp++) {
		if (IS_EOL(*ihp)) {
			/* Did not find interpreter, "#!\n" */
			return ENOEXEC;
		} else if (IS_WHITESPACE(*ihp)) {
			/* Whitespace, like "#!    /bin/sh\n", keep going. */
		} else {
			/* Found start of interpreter */
			break;
		}
	}

	if (ihp == &vdata[IMG_SHSIZE]) {
		/* All whitespace, like "#!           " */
		return ENOEXEC;
	}

	line_startp = ihp;

	/* Try to find the end of the interpreter+args string */
	for (; ihp < &vdata[IMG_SHSIZE]; ihp++) {
		if (IS_EOL(*ihp)) {
			/* Got it */
			break;
		} else {
			/* Still part of interpreter or args */
		}
	}

	if (ihp == &vdata[IMG_SHSIZE]) {
		/* A long line, like "#! blah blah blah" without end */
		return ENOEXEC;
	}

	/* Backtrack until we find the last non-whitespace */
	while (IS_EOL(*ihp) || IS_WHITESPACE(*ihp)) {
		ihp--;
	}

	/* The character after the last non-whitespace is our logical end of line */
	line_endp = ihp + 1;

	/*
	 * Now we have pointers to the usable part of:
	 *
	 * "#!  /usr/bin/int first    second   third    \n"
	 *      ^ line_startp                       ^ line_endp
	 */

	/* copy the interpreter name */
	interp = imgp->ip_interp_buffer;
	for (ihp = line_startp; (ihp < line_endp) && !IS_WHITESPACE(*ihp); ihp++) {
		*interp++ = *ihp;
	}
	*interp = '\0';

	exec_reset_save_path(imgp);
	exec_save_path(imgp, CAST_USER_ADDR_T(imgp->ip_interp_buffer),
	    UIO_SYSSPACE, NULL);

	/* Copy the entire interpreter + args for later processing into argv[] */
	interp = imgp->ip_interp_buffer;
	for (ihp = line_startp; (ihp < line_endp); ihp++) {
		*interp++ = *ihp;
	}
	*interp = '\0';

#if CONFIG_SETUID
	/*
	 * If we have an SUID or SGID script, create a file descriptor
	 * from the vnode and pass /dev/fd/%d instead of the actual
	 * path name so that the script does not get opened twice
	 */
	if (imgp->ip_origvattr->va_mode & (VSUID | VSGID)) {
		proc_t p;
		struct fileproc *fp;
		int fd;
		int error;

		p = vfs_context_proc(imgp->ip_vfs_context);
		error = falloc_exec(p, imgp->ip_vfs_context, &fp, &fd);
		if (error) {
			return error;
		}

		fp->fp_glob->fg_flag = FREAD;
		fp->fp_glob->fg_ops = &vnops;
		fp_set_data(fp, imgp->ip_vp);

		proc_fdlock(p);
		procfdtbl_releasefd(p, fd, NULL);
		fp_drop(p, fd, fp, 1);
		proc_fdunlock(p);
		vnode_ref(imgp->ip_vp);

		imgp->ip_interp_sugid_fd = fd;
	}
#endif /* CONFIG_SETUID */

	return -3;
}



/*
 * exec_fat_imgact
 *
 * Image activator for fat 1.0 binaries.  If the binary is fat, then we
 * need to select an image from it internally, and make that the image
 * we are going to attempt to execute.  At present, this consists of
 * reloading the first page for the image with a first page from the
 * offset location indicated by the fat header.
 *
 * Parameters;	struct image_params *	image parameter block
 *
 * Returns:	-1			not a fat binary (keep looking)
 *		-2			Success: encapsulated binary: reread
 *		>0			Failure: error number
 *
 * Important:	This image activator is byte order neutral.
 *
 * Note:	A return value other than -1 indicates subsequent image
 *		activators should not be given the opportunity to attempt
 *		to activate the image.
 *
 *              If we find an encapsulated binary, we make no assertions
 *		about its  validity; instead, we leave that up to a rescan
 *		for an activator to claim it, and, if it is claimed by one,
 *		that activator is responsible for determining validity.
 */
static int
exec_fat_imgact(struct image_params *imgp)
{
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);
	kauth_cred_t cred = kauth_cred_proc_ref(p);
	struct fat_header *fat_header = (struct fat_header *)imgp->ip_vdata;
	struct _posix_spawnattr *psa = NULL;
	struct fat_arch fat_arch;
	int resid, error;
	load_return_t lret;

	if (imgp->ip_origcputype != 0) {
		/* Fat header previously matched, don't allow another fat file inside */
		error = -1; /* not claimed */
		goto bad;
	}

	/* Make sure it's a fat binary */
	if (OSSwapBigToHostInt32(fat_header->magic) != FAT_MAGIC) {
		error = -1; /* not claimed */
		goto bad;
	}

	/* imgp->ip_vdata has PAGE_SIZE, zerofilled if the file is smaller */
	lret = fatfile_validate_fatarches((vm_offset_t)fat_header, PAGE_SIZE,
	    (off_t)imgp->ip_vattr->va_data_size);
	if (lret != LOAD_SUCCESS) {
		error = load_return_to_errno(lret);
		goto bad;
	}

	/* If posix_spawn binprefs exist, respect those prefs. */
	psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
	if (psa != NULL && psa->psa_binprefs[0] != 0) {
		uint32_t pr = 0;

		/* Check each preference listed against all arches in header */
		for (pr = 0; pr < NBINPREFS; pr++) {
			cpu_type_t pref = psa->psa_binprefs[pr];
			cpu_type_t subpref = psa->psa_subcpuprefs[pr];

			if (pref == 0) {
				/* No suitable arch in the pref list */
				error = EBADARCH;
				goto bad;
			}

			if (pref == CPU_TYPE_ANY) {
				/* Fall through to regular grading */
				goto regular_grading;
			}

			lret = fatfile_getbestarch_for_cputype(pref,
			    subpref,
			    (vm_offset_t)fat_header,
			    PAGE_SIZE,
			    imgp,
			    &fat_arch);
			if (lret == LOAD_SUCCESS) {
				goto use_arch;
			}
		}

		/* Requested binary preference was not honored */
		error = EBADEXEC;
		goto bad;
	}

regular_grading:
	/* Look up our preferred architecture in the fat file. */
	lret = fatfile_getbestarch((vm_offset_t)fat_header,
	    PAGE_SIZE,
	    imgp,
	    &fat_arch,
	    (p->p_flag & P_AFFINITY) != 0);
	if (lret != LOAD_SUCCESS) {
		error = load_return_to_errno(lret);
		goto bad;
	}

use_arch:
	/* Read the Mach-O header out of fat_arch */
	error = vn_rdwr(UIO_READ, imgp->ip_vp, imgp->ip_vdata,
	    PAGE_SIZE, fat_arch.offset,
	    UIO_SYSSPACE, (IO_UNIT | IO_NODELOCKED),
	    cred, &resid, p);
	if (error) {
		goto bad;
	}

	if (resid) {
		memset(imgp->ip_vdata + (PAGE_SIZE - resid), 0x0, resid);
	}

	/* Success.  Indicate we have identified an encapsulated binary */
	error = -2;
	imgp->ip_arch_offset = (user_size_t)fat_arch.offset;
	imgp->ip_arch_size = (user_size_t)fat_arch.size;
	imgp->ip_origcputype = fat_arch.cputype;
	imgp->ip_origcpusubtype = fat_arch.cpusubtype;

bad:
	kauth_cred_unref(&cred);
	return error;
}

static int
activate_exec_state(task_t task, proc_t p, thread_t thread, load_result_t *result)
{
	int ret;

	(void)task_set_dyld_info(task, MACH_VM_MIN_ADDRESS, 0);
	task_set_64bit(task, result->is_64bit_addr, result->is_64bit_data);
	if (result->is_64bit_addr) {
		OSBitOrAtomic(P_LP64, &p->p_flag);
		get_bsdthread_info(thread)->uu_flag |= UT_LP64;
	} else {
		OSBitAndAtomic(~((uint32_t)P_LP64), &p->p_flag);
		get_bsdthread_info(thread)->uu_flag &= ~UT_LP64;
	}
	task_set_mach_header_address(task, result->mach_header);

	ret = thread_state_initialize(thread);
	if (ret != KERN_SUCCESS) {
		return ret;
	}

	if (result->threadstate) {
		uint32_t *ts = result->threadstate;
		uint32_t total_size = (uint32_t)result->threadstate_sz;

		while (total_size > 0) {
			uint32_t flavor = *ts++;
			uint32_t size = *ts++;

			ret = thread_setstatus(thread, flavor, (thread_state_t)ts, size);
			if (ret) {
				return ret;
			}
			ts += size;
			total_size -= (size + 2) * sizeof(uint32_t);
		}
	}

	thread_setentrypoint(thread, result->entry_point);

	return KERN_SUCCESS;
}

#if (DEVELOPMENT || DEBUG)
extern char panic_on_proc_crash[];
extern int use_panic_on_proc_crash;

extern char panic_on_proc_exit[];
extern int use_panic_on_proc_exit;

extern char panic_on_proc_spawn_fail[];
extern int use_panic_on_proc_spawn_fail;
#endif

void
set_proc_name(struct image_params *imgp, proc_t p)
{
	int p_name_len = sizeof(p->p_name) - 1;

	if (imgp->ip_ndp->ni_cnd.cn_namelen > p_name_len) {
		imgp->ip_ndp->ni_cnd.cn_namelen = p_name_len;
	}

	bcopy((caddr_t)imgp->ip_ndp->ni_cnd.cn_nameptr, (caddr_t)p->p_name,
	    (unsigned)imgp->ip_ndp->ni_cnd.cn_namelen);
	p->p_name[imgp->ip_ndp->ni_cnd.cn_namelen] = '\0';

	if (imgp->ip_ndp->ni_cnd.cn_namelen > MAXCOMLEN) {
		imgp->ip_ndp->ni_cnd.cn_namelen = MAXCOMLEN;
	}

	bcopy((caddr_t)imgp->ip_ndp->ni_cnd.cn_nameptr, (caddr_t)p->p_comm,
	    (unsigned)imgp->ip_ndp->ni_cnd.cn_namelen);
	p->p_comm[imgp->ip_ndp->ni_cnd.cn_namelen] = '\0';

#if DEVELOPMENT || DEBUG
	/*
	 * This happens during image activation, so the crash behavior flags from
	 * posix_spawn will have already been set. So we don't have to worry about
	 * this being overridden.
	 */
	if (use_panic_on_proc_crash && strcmp(p->p_comm, panic_on_proc_crash) == 0) {
		printf("will panic on proc crash: %s\n", p->p_comm);
		p->p_crash_behavior |= POSIX_SPAWN_PANIC_ON_CRASH;
	}

	if (use_panic_on_proc_exit && strcmp(p->p_comm, panic_on_proc_exit) == 0) {
		printf("will panic on proc exit: %s\n", p->p_comm);
		p->p_crash_behavior |= POSIX_SPAWN_PANIC_ON_EXIT;
	}

	if (use_panic_on_proc_spawn_fail && strcmp(p->p_comm, panic_on_proc_spawn_fail) == 0) {
		printf("will panic on proc spawn fail: %s\n", p->p_comm);
		p->p_crash_behavior |= POSIX_SPAWN_PANIC_ON_SPAWN_FAIL;
	}
#endif
}

#if __has_feature(ptrauth_calls)
/**
 * Returns a team ID string that may be used to assign a shared region.
 *
 * Platform binaries do not have team IDs and will return NULL.  Non-platform
 * binaries without a team ID will be assigned an artificial team ID of ""
 * (empty string) so that they will not be assigned to the default shared
 * region.
 *
 * @param imgp image parameter block
 * @return NULL if this is a platform binary, or an appropriate team ID string
 *         otherwise
 */
static inline const char *
get_teamid_for_shared_region(struct image_params *imgp)
{
	assert(imgp->ip_vp != NULL);

	const char *ret = csvnode_get_teamid(imgp->ip_vp, imgp->ip_arch_offset);
	if (ret) {
		return ret;
	}

	struct cs_blob *blob = csvnode_get_blob(imgp->ip_vp, imgp->ip_arch_offset);
	if (csblob_get_platform_binary(blob)) {
		return NULL;
	} else {
		static const char *NO_TEAM_ID = "";
		return NO_TEAM_ID;
	}
}

/**
 * Determines whether ptrauth should be enabled for the provided arm64 CPU subtype.
 *
 * @param cpusubtype Mach-O style CPU subtype
 * @return whether the CPU subtype matches arm64e with the current ptrauth ABI
 */
static inline bool
arm64_cpusubtype_uses_ptrauth(cpu_subtype_t cpusubtype)
{
	return (cpusubtype & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E &&
	       CPU_SUBTYPE_ARM64_PTR_AUTH_VERSION(cpusubtype) == CPU_SUBTYPE_ARM64_PTR_AUTH_CURRENT_VERSION;
}

#endif /* __has_feature(ptrauth_calls) */

/**
 * Returns whether a type/subtype slice matches the requested
 * type/subtype.
 *
 * @param mask Bits to mask from the requested/tested cpu type
 * @param req_cpu Requested cpu type
 * @param req_subcpu Requested cpu subtype
 * @param test_cpu Tested slice cpu type
 * @param test_subcpu Tested slice cpu subtype
 */
boolean_t
binary_match(cpu_type_t mask, cpu_type_t req_cpu,
    cpu_subtype_t req_subcpu, cpu_type_t test_cpu,
    cpu_subtype_t test_subcpu)
{
	if ((test_cpu & ~mask) != (req_cpu & ~mask)) {
		return FALSE;
	}

	test_subcpu &= ~CPU_SUBTYPE_MASK;
	req_subcpu  &= ~CPU_SUBTYPE_MASK;

	if (test_subcpu != req_subcpu && req_subcpu != (CPU_SUBTYPE_ANY & ~CPU_SUBTYPE_MASK)) {
		return FALSE;
	}

	return TRUE;
}


#define MIN_IOS_TPRO_SDK_VERSION        0x00100000
#define MIN_OSX_TPRO_SDK_VERSION        0x000D0000
#define MIN_TVOS_TPRO_SDK_VERSION       0x000D0000
#define MIN_WATCHOS_TPRO_SDK_VERSION    0x00090000
#define MIN_DRIVERKIT_TPRO_SDK_VERSION  0x00600000

static void
exec_setup_tpro(struct image_params *imgp, load_result_t *load_result)
{
	extern boolean_t xprr_tpro_enabled;
	extern boolean_t enable_user_modifiable_perms;
	uint32_t min_sdk_version = 0;

	/* x86-64 translated code cannot take advantage of TPRO */
	if (imgp->ip_flags & IMGPF_ROSETTA) {
		return;
	}

	/* Do not enable on 32-bit VA targets */
	if (!(imgp->ip_flags & IMGPF_IS_64BIT_ADDR)) {
		return;
	}

	switch (load_result->ip_platform) {
	case PLATFORM_IOS:
	case PLATFORM_IOSSIMULATOR:
	case PLATFORM_MACCATALYST:
		min_sdk_version = MIN_IOS_TPRO_SDK_VERSION;
		break;
	case PLATFORM_MACOS:
		min_sdk_version = MIN_OSX_TPRO_SDK_VERSION;
		break;
	case PLATFORM_TVOS:
	case PLATFORM_TVOSSIMULATOR:
		min_sdk_version = MIN_TVOS_TPRO_SDK_VERSION;
		break;
	case PLATFORM_WATCHOS:
	case PLATFORM_WATCHOSSIMULATOR:
		min_sdk_version = MIN_WATCHOS_TPRO_SDK_VERSION;
		break;
	case PLATFORM_DRIVERKIT:
		min_sdk_version = MIN_DRIVERKIT_TPRO_SDK_VERSION;
		break;
	default:
		/* TPRO is on by default for newer platforms */
		break;
	}

}

/*
 * If the passed in executable's vnode should use the RSR
 * shared region, then this should return TRUE, otherwise, return FALSE.
 */
static uint32_t rsr_current_version = 0;
boolean_t (*rsr_check_vnode)(void *vnode) = NULL;

boolean_t
vnode_is_rsr(vnode_t vp)
{
	if (!(vnode_isreg(vp) && vnode_tag(vp) == VT_APFS)) {
		return FALSE;
	}

	if (rsr_check_vnode != NULL && rsr_check_vnode((void *)vp)) {
		return TRUE;
	}
	return FALSE;
}


static inline void
encode_HR_entitlement(const char *entitlement, HR_flags_t mask,
    const struct image_params *imgp, load_result_t *load_result)
{
	if (IOVnodeHasEntitlement(imgp->ip_vp, (int64_t)imgp->ip_arch_offset, entitlement)) {
		load_result->hardened_runtime_binary |= mask;
	}
}

uint32_t
rsr_get_version(void)
{
	return os_atomic_load(&rsr_current_version, relaxed);
}

void
rsr_bump_version(void)
{
	os_atomic_inc(&rsr_current_version, relaxed);
}

#if XNU_TARGET_OS_OSX
static int
rsr_version_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1, arg2, oidp)
	int value = rsr_get_version();
	int error = SYSCTL_OUT(req, &value, sizeof(int));
	if (error) {
		return error;
	}

	if (!req->newptr) {
		return 0;
	}

	error = SYSCTL_IN(req, &value, sizeof(int));
	if (error) {
		return error;
	}
	if (value != 0) {
		rsr_bump_version();
	}
	return 0;
}


SYSCTL_PROC(_vm, OID_AUTO, shared_region_control,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_MASKED,
    0, 0, rsr_version_sysctl, "I", "");
#endif /* XNU_TARGET_OS_OSX */

/*
 * exec_mach_imgact
 *
 * Image activator for mach-o 1.0 binaries.
 *
 * Parameters;	struct image_params *	image parameter block
 *
 * Returns:	-1			not a fat binary (keep looking)
 *		-2			Success: encapsulated binary: reread
 *		>0			Failure: error number
 *		EBADARCH		Mach-o binary, but with an unrecognized
 *					architecture
 *		ENOMEM			No memory for child process after -
 *					can only happen after vfork()
 *
 * Important:	This image activator is NOT byte order neutral.
 *
 * Note:	A return value other than -1 indicates subsequent image
 *		activators should not be given the opportunity to attempt
 *		to activate the image.
 */
static int
exec_mach_imgact(struct image_params *imgp)
{
	struct mach_header *mach_header = (struct mach_header *)imgp->ip_vdata;
	proc_t                  p = vfs_context_proc(imgp->ip_vfs_context);
	int                     error = 0;
	task_t                  task;
	task_t                  new_task = NULL; /* protected by vfexec */
	thread_t                thread;
	struct uthread          *uthread;
	vm_map_t old_map = VM_MAP_NULL;
	vm_map_t map = VM_MAP_NULL;
	load_return_t           lret;
	load_result_t           load_result = {};
	struct _posix_spawnattr *psa = NULL;
	int                     spawn = (imgp->ip_flags & IMGPF_SPAWN);
	const int               vfexec = 0;
	int                     exec = (imgp->ip_flags & IMGPF_EXEC);
	os_reason_t             exec_failure_reason = OS_REASON_NULL;
	boolean_t               reslide = FALSE;
	char *                  userspace_coredump_name = NULL;

	/*
	 * make sure it's a Mach-O 1.0 or Mach-O 2.0 binary; the difference
	 * is a reserved field on the end, so for the most part, we can
	 * treat them as if they were identical. Reverse-endian Mach-O
	 * binaries are recognized but not compatible.
	 */
	if ((mach_header->magic == MH_CIGAM) ||
	    (mach_header->magic == MH_CIGAM_64)) {
		error = EBADARCH;
		goto bad;
	}

	if ((mach_header->magic != MH_MAGIC) &&
	    (mach_header->magic != MH_MAGIC_64)) {
		error = -1;
		goto bad;
	}

	if (mach_header->filetype != MH_EXECUTE) {
		error = -1;
		goto bad;
	}

	if (imgp->ip_origcputype != 0) {
		/* Fat header previously had an idea about this thin file */
		if (imgp->ip_origcputype != mach_header->cputype ||
		    imgp->ip_origcpusubtype != mach_header->cpusubtype) {
			error = EBADARCH;
			goto bad;
		}
	} else {
		imgp->ip_origcputype = mach_header->cputype;
		imgp->ip_origcpusubtype = mach_header->cpusubtype;
	}

	task = current_task();
	thread = current_thread();
	uthread = get_bsdthread_info(thread);

	if ((mach_header->cputype & CPU_ARCH_ABI64) == CPU_ARCH_ABI64) {
		imgp->ip_flags |= IMGPF_IS_64BIT_ADDR | IMGPF_IS_64BIT_DATA;
	}


	/* If posix_spawn binprefs exist, respect those prefs. */
	psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
	if (psa != NULL && psa->psa_binprefs[0] != 0) {
		int pr = 0;
		for (pr = 0; pr < NBINPREFS; pr++) {
			cpu_type_t pref = psa->psa_binprefs[pr];
			cpu_subtype_t subpref = psa->psa_subcpuprefs[pr];

			if (pref == 0) {
				/* No suitable arch in the pref list */
				error = EBADARCH;
				goto bad;
			}

			if (pref == CPU_TYPE_ANY) {
				/* Jump to regular grading */
				goto grade;
			}

			if (binary_match(CPU_ARCH_MASK, pref, subpref,
			    imgp->ip_origcputype, imgp->ip_origcpusubtype)) {
				goto grade;
			}
		}
		error = EBADARCH;
		goto bad;
	}
grade:
	if (!grade_binary(imgp->ip_origcputype, imgp->ip_origcpusubtype & ~CPU_SUBTYPE_MASK,
	    imgp->ip_origcpusubtype & CPU_SUBTYPE_MASK, TRUE)) {
		error = EBADARCH;
		goto bad;
	}

	if (validate_potential_simulator_binary(imgp->ip_origcputype, imgp,
	    imgp->ip_arch_offset, imgp->ip_arch_size) != LOAD_SUCCESS) {
#if __x86_64__
		const char *excpath;
		error = exec_save_path(imgp, imgp->ip_user_fname, imgp->ip_seg, &excpath);
		os_log_error(OS_LOG_DEFAULT, "Unsupported 32-bit executable: \"%s\"", (error) ? imgp->ip_vp->v_name : excpath);
#endif
		error = EBADARCH;
		goto bad;
	}

#if defined(HAS_APPLE_PAC)
	assert(mach_header->cputype == CPU_TYPE_ARM64
	    );

	if ((mach_header->cputype == CPU_TYPE_ARM64 &&
	    arm64_cpusubtype_uses_ptrauth(mach_header->cpusubtype))
	    ) {
		imgp->ip_flags &= ~IMGPF_NOJOP;
	} else {
		imgp->ip_flags |= IMGPF_NOJOP;
	}
#endif

	/* Copy in arguments/environment from the old process */
	error = exec_extract_strings(imgp);
	if (error) {
		goto bad;
	}

	AUDIT_ARG(argv, imgp->ip_startargv, imgp->ip_argc,
	    imgp->ip_endargv - imgp->ip_startargv);
	AUDIT_ARG(envv, imgp->ip_endargv, imgp->ip_envc,
	    imgp->ip_endenvv - imgp->ip_endargv);



	/* reset local idea of thread, uthread, task */
	thread = imgp->ip_new_thread;
	uthread = get_bsdthread_info(thread);
	task = new_task = get_threadtask(thread);

	/*
	 *	Load the Mach-O file.
	 *
	 * NOTE: An error after this point  indicates we have potentially
	 * destroyed or overwritten some process state while attempting an
	 * execve() following a vfork(), which is an unrecoverable condition.
	 * We send the new process an immediate SIGKILL to avoid it executing
	 * any instructions in the mutated address space. For true spawns,
	 * this is not the case, and "too late" is still not too late to
	 * return an error code to the parent process.
	 */

	/*
	 * Actually load the image file we previously decided to load.
	 */
	boot_progress_process_stage(proc_getpid(p),p->p_name,"exec load_machfile enter");
	lret = load_machfile(imgp, mach_header, thread, &map, &load_result);
	boot_progress_process_stage(proc_getpid(p),p->p_name,"exec load_machfile returned");
	if (lret != LOAD_SUCCESS) {
		error = load_return_to_errno(lret);

		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_MACHO, 0, 0);
		if (lret == LOAD_BADMACHO_UPX) {
			set_proc_name(imgp, p);
			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_UPX);
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
		} else {
			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_MACHO);

			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
		}

		exec_failure_reason->osr_flags |= OS_REASON_FLAG_CONSISTENT_FAILURE;

		goto badtoolate;
	}

	/*
	 * ERROR RECOVERY
	 *
	 * load_machfile() returned the new VM map ("map") but we haven't
	 * committed to it yet.
	 * Any error path between here and the point where we commit to using
	 * the new "map" (with swap_task_map()) should deallocate "map".
	 */

#ifndef KASAN
	/*
	 * Security: zone sanity checks on fresh boot or initproc re-exec.
	 * launchd by design does not tear down its own service port on USR (rdar://72797967),
	 * which means here is the earliest point we can assert on empty service port label zone,
	 * after load_machfile() above terminates old launchd's IPC space.
	 *
	 * Disable on KASAN builds since zone_size_allocated() accounts for elements
	 * under quarantine.
	 */
	if (task_pid(task) == 1) {
		zone_userspace_reboot_checks();
	}
#endif

	proc_lock(p);
	p->p_cputype = imgp->ip_origcputype;
	p->p_cpusubtype = imgp->ip_origcpusubtype;
	proc_setplatformdata(p, load_result.ip_platform, load_result.lr_min_sdk, load_result.lr_sdk);
	exec_setup_tpro(imgp, &load_result);

	vm_map_set_size_limit(map, proc_limitgetcur(p, RLIMIT_AS));
	vm_map_set_data_limit(map, proc_limitgetcur(p, RLIMIT_DATA));
	vm_map_set_user_wire_limit(map, (vm_size_t)proc_limitgetcur(p, RLIMIT_MEMLOCK));
#if XNU_TARGET_OS_OSX
	if (proc_platform(p) == PLATFORM_IOS) {
		assert(vm_map_is_alien(map));
	} else {
		assert(!vm_map_is_alien(map));
	}
#endif /* XNU_TARGET_OS_OSX */
	proc_unlock(p);

	/*
	 * Set TPRO flags if enabled
	 */

	/*
	 * Set code-signing flags if this binary is signed, or if parent has
	 * requested them on exec.
	 */
	if (load_result.csflags & CS_VALID) {
		imgp->ip_csflags |= load_result.csflags &
		    (CS_VALID | CS_SIGNED | CS_DEV_CODE | CS_LINKER_SIGNED |
		    CS_HARD | CS_KILL | CS_RESTRICT | CS_ENFORCEMENT | CS_REQUIRE_LV |
		    CS_FORCED_LV | CS_ENTITLEMENTS_VALIDATED | CS_NO_UNTRUSTED_HELPERS | CS_RUNTIME |
		    CS_ENTITLEMENT_FLAGS |
		    CS_EXEC_SET_HARD | CS_EXEC_SET_KILL | CS_EXEC_SET_ENFORCEMENT);
	} else {
		imgp->ip_csflags &= ~CS_VALID;
	}

	if (proc_getcsflags(p) & CS_EXEC_SET_HARD) {
		imgp->ip_csflags |= CS_HARD;
	}
	if (proc_getcsflags(p) & CS_EXEC_SET_KILL) {
		imgp->ip_csflags |= CS_KILL;
	}
	if (proc_getcsflags(p) & CS_EXEC_SET_ENFORCEMENT) {
		imgp->ip_csflags |= CS_ENFORCEMENT;
	}
	if (proc_getcsflags(p) & CS_EXEC_INHERIT_SIP) {
		if (proc_getcsflags(p) & CS_INSTALLER) {
			imgp->ip_csflags |= CS_INSTALLER;
		}
		if (proc_getcsflags(p) & CS_DATAVAULT_CONTROLLER) {
			imgp->ip_csflags |= CS_DATAVAULT_CONTROLLER;
		}
		if (proc_getcsflags(p) & CS_NVRAM_UNRESTRICTED) {
			imgp->ip_csflags |= CS_NVRAM_UNRESTRICTED;
		}
	}

#if __has_feature(ptrauth_calls) && defined(XNU_TARGET_OS_OSX)
	/*
	 * ptrauth version 0 is a preview ABI.  Developers can opt into running
	 * their own arm64e binaries for local testing, with the understanding
	 * that future OSes may break ABI.
	 */
	if ((imgp->ip_origcpusubtype & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E &&
	    CPU_SUBTYPE_ARM64_PTR_AUTH_VERSION(imgp->ip_origcpusubtype) == 0 &&
	    !load_result.platform_binary &&
	    !bootarg_arm64e_preview_abi) {
		static bool logged_once = false;
		set_proc_name(imgp, p);

		printf("%s: not running binary \"%s\" built against preview arm64e ABI\n", __func__, p->p_name);
		if (!os_atomic_xchg(&logged_once, true, relaxed)) {
			printf("%s: (to allow this, add \"-arm64e_preview_abi\" to boot-args)\n", __func__);
		}

		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_MACHO);
		if (bootarg_execfailurereports) {
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_CONSISTENT_FAILURE;
		}

		/* release new address space since we won't use it */
		imgp->ip_free_map = map;
		map = VM_MAP_NULL;
		goto badtoolate;
	}

	if ((imgp->ip_origcpusubtype & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64E &&
	    imgp->ip_origcputype == CPU_TYPE_ARM64 &&
	    load_result.platform_binary &&
	    (imgp->ip_flags & IMGPF_DRIVER) != 0) {
		set_proc_name(imgp, p);
		printf("%s: disallowing arm64 platform driverkit binary \"%s\", should be arm64e\n", __func__, p->p_name);
		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_MACHO);
		if (bootarg_execfailurereports) {
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_CONSISTENT_FAILURE;
		}

		/* release new address space since we won't use it */
		imgp->ip_free_map = map;
		map = VM_MAP_NULL;
		goto badtoolate;
	}
#endif /* __has_feature(ptrauth_calls) && defined(XNU_TARGET_OS_OSX) */


	load_result.hardened_runtime_binary = 0;
	// Propogate which hardened runtime entitlements are active to the apple array
	encode_HR_entitlement(kCSWebBrowserHostEntitlement, BrowserHostEntitlementMask, imgp, &load_result);
	encode_HR_entitlement(kCSWebBrowserGPUEntitlement, BrowserGPUEntitlementMask, imgp, &load_result);
	encode_HR_entitlement(kCSWebBrowserNetworkEntitlement, BrowserNetworkEntitlementMask, imgp, &load_result);
	encode_HR_entitlement(kCSWebBrowserWebContentEntitlement, BrowserWebContentEntitlementMask, imgp, &load_result);

	/*
	 * Set up the shared cache region in the new process.
	 *
	 * Normally there is a single shared region per architecture.
	 * However on systems with Pointer Authentication, we can create
	 * multiple shared caches with the amount of sharing determined
	 * by team-id or entitlement. Inherited shared region IDs are used
	 * for system processes that need to match and be able to inspect
	 * a pre-existing task.
	 */
	int cpu_subtype = 0; /* all cpu_subtypes use the same shared region */
#if __has_feature(ptrauth_calls)
	char *shared_region_id = NULL;
	size_t len;
	char *base;
	const char *cbase;
#define HARDENED_RUNTIME_CONTENT_ID "C-"
#define TEAM_ID_PREFIX "T-"
#define ENTITLE_PREFIX "E-"
#define SR_PREFIX_LEN  2
#define SR_ENTITLEMENT "com.apple.pac.shared_region_id"

	if (cpu_type() == CPU_TYPE_ARM64 &&
	    arm64_cpusubtype_uses_ptrauth(p->p_cpusubtype) &&
	    (imgp->ip_flags & IMGPF_NOJOP) == 0) {
		assertf(p->p_cputype == CPU_TYPE_ARM64,
		    "p %p cpu_type() 0x%x p->p_cputype 0x%x p->p_cpusubtype 0x%x",
		    p, cpu_type(), p->p_cputype, p->p_cpusubtype);

		/*
		 * arm64e uses pointer authentication, so request a separate
		 * shared region for this CPU subtype.
		 */
		cpu_subtype = p->p_cpusubtype & ~CPU_SUBTYPE_MASK;

		/*
		 * Determine which shared cache to select based on being told,
		 * matching a team-id or matching an entitlement.
		 */
		if (load_result.hardened_runtime_binary & BrowserWebContentEntitlementMask) {
			len = sizeof(HARDENED_RUNTIME_CONTENT_ID);
			shared_region_id = kalloc_data(len, Z_WAITOK | Z_NOFAIL);
			strlcpy(shared_region_id, HARDENED_RUNTIME_CONTENT_ID, len);
		} else if (imgp->ip_inherited_shared_region_id) {
			len = strlen(imgp->ip_inherited_shared_region_id);
			shared_region_id = kalloc_data(len + 1, Z_WAITOK | Z_NOFAIL);
			memcpy(shared_region_id, imgp->ip_inherited_shared_region_id, len + 1);
		} else if ((cbase = get_teamid_for_shared_region(imgp)) != NULL) {
			len = strlen(cbase);
			if (vm_shared_region_per_team_id) {
				shared_region_id = kalloc_data(len + SR_PREFIX_LEN + 1,
				    Z_WAITOK | Z_NOFAIL);
				memcpy(shared_region_id, TEAM_ID_PREFIX, SR_PREFIX_LEN);
				memcpy(shared_region_id + SR_PREFIX_LEN, cbase, len + 1);
			}
		} else if ((base = IOVnodeGetEntitlement(imgp->ip_vp,
		    (int64_t)imgp->ip_arch_offset, SR_ENTITLEMENT)) != NULL) {
			len = strlen(base);
			if (vm_shared_region_by_entitlement) {
				shared_region_id = kalloc_data(len + SR_PREFIX_LEN + 1,
				    Z_WAITOK | Z_NOFAIL);
				memcpy(shared_region_id, ENTITLE_PREFIX, SR_PREFIX_LEN);
				memcpy(shared_region_id + SR_PREFIX_LEN, base, len + 1);
			}
			/* Discard the copy of the entitlement */
			kfree_data(base, len + 1);
		}
	}

	if (imgp->ip_flags & IMGPF_RESLIDE) {
		reslide = TRUE;
	}

	/* use "" as the default shared_region_id */
	if (shared_region_id == NULL) {
		shared_region_id = kalloc_data(1, Z_WAITOK | Z_ZERO | Z_NOFAIL);
	}

	/* ensure there's a unique pointer signing key for this shared_region_id */
	shared_region_key_alloc(shared_region_id,
	    imgp->ip_inherited_shared_region_id != NULL, imgp->ip_inherited_jop_pid);
	task_set_shared_region_id(task, shared_region_id);
	shared_region_id = NULL;
#endif /* __has_feature(ptrauth_calls) */

#if CONFIG_ROSETTA
	if (imgp->ip_flags & IMGPF_ROSETTA) {
		OSBitOrAtomic(P_TRANSLATED, &p->p_flag);
	} else if (p->p_flag & P_TRANSLATED) {
		OSBitAndAtomic(~P_TRANSLATED, &p->p_flag);
	}
#endif

	int cputype = cpu_type();

	uint32_t rsr_version = 0;
#if XNU_TARGET_OS_OSX
	if (vnode_is_rsr(imgp->ip_vp)) {
		rsr_version = rsr_get_version();
		os_atomic_or(&p->p_ladvflag, P_RSR, relaxed);
		os_atomic_or(&p->p_vfs_iopolicy, P_VFS_IOPOLICY_ALTLINK, relaxed);
	}
#endif /* XNU_TARGET_OS_OSX */

	vm_map_exec(map, task, load_result.is_64bit_addr,
	    (void *)p->p_fd.fd_rdir, cputype, cpu_subtype, reslide,
	    (imgp->ip_flags & IMGPF_DRIVER) != 0,
	    rsr_version);

	/*
	 * Close file descriptors which specify close-on-exec.
	 */
	fdt_exec(p, vfs_context_ucred(imgp->ip_vfs_context),
	    psa != NULL ? psa->psa_flags : 0, imgp->ip_new_thread, exec);

	/*
	 * deal with set[ug]id.
	 */
	error = exec_handle_sugid(imgp);
	if (error) {
		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_SUGID_FAILURE, 0, 0);

		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_SUGID_FAILURE);
		if (bootarg_execfailurereports) {
			set_proc_name(imgp, p);
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
		}

		/* release new address space since we won't use it */
		imgp->ip_free_map = map;
		map = VM_MAP_NULL;
		goto badtoolate;
	}

	/*
	 * Commit to new map.
	 *
	 * Swap the new map for the old for target task, which consumes
	 * our new map reference but each leaves us responsible for the
	 * old_map reference.  That lets us get off the pmap associated
	 * with it, and then we can release it.
	 *
	 * The map needs to be set on the target task which is different
	 * than current task, thus swap_task_map is used instead of
	 * vm_map_switch.
	 */
	old_map = swap_task_map(task, thread, map);
#if MACH_ASSERT
	/*
	 * Reset the pmap's process info to prevent ledger checks
	 * which might fail due to the ledgers being shared between
	 * the old and new pmaps.
	 */
	vm_map_pmap_set_process(old_map, -1, "<old_map>");
#endif /* MACH_ASSERT */
	imgp->ip_free_map = old_map;
	old_map = NULL;

	lret = activate_exec_state(task, p, thread, &load_result);
	if (lret != KERN_SUCCESS) {
		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_ACTV_THREADSTATE, 0, 0);

		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_ACTV_THREADSTATE);
		if (bootarg_execfailurereports) {
			set_proc_name(imgp, p);
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
		}

		goto badtoolate;
	}

	/*
	 * deal with voucher on exec-calling thread.
	 */
	if (imgp->ip_new_thread == NULL) {
		thread_set_mach_voucher(current_thread(), IPC_VOUCHER_NULL);
	}

	/* Make sure we won't interrupt ourself signalling a partial process */
	if (!vfexec && !spawn && (p->p_lflag & P_LTRACED)) {
		psignal(p, SIGTRAP);
	}

	if (load_result.unixproc &&
	    create_unix_stack(get_task_map(task),
	    &load_result,
	    p) != KERN_SUCCESS) {
		error = load_return_to_errno(LOAD_NOSPACE);

		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_STACK_ALLOC, 0, 0);

		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_STACK_ALLOC);
		if (bootarg_execfailurereports) {
			set_proc_name(imgp, p);
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
		}

		goto badtoolate;
	}

	if (load_result.hardened_runtime_binary) {
		if (cs_debug) {
			printf("setting hardened runtime with entitlement mask= "
			    "0x%x on task: pid = %d\n",
			    load_result.hardened_runtime_binary,
			    proc_getpid(p));
		}
		task_set_hardened_runtime(task, true);
	}

	/*
	 * The load result will have already been munged by AMFI to include the
	 * platform binary flag if boot-args dictated it (AMFI will mark anything
	 * that doesn't go through the upcall path as a platform binary if its
	 * enforcement is disabled).
	 */
	if (load_result.platform_binary) {
		if (cs_debug) {
			printf("setting platform binary on task: pid = %d\n", proc_getpid(p));
		}

		/*
		 * We must use 'task' here because the proc's task has not yet been
		 * switched to the new one.
		 */
		task_set_platform_binary(task, TRUE);
	} else {
		if (cs_debug) {
			printf("clearing platform binary on task: pid = %d\n", proc_getpid(p));
		}

		task_set_platform_binary(task, FALSE);
	}

#if XNU_TARGET_OS_OSX
	/* Disable mach hardening for all 1P tasks which load 3P plugins */
	if (imgp->ip_flags & IMGPF_3P_PLUGINS) {
		if (cs_debug) {
			printf("Disabling some mach hardening on task due to 3P plugins: pid = %d\n", proc_getpid(p));
		}
		task_disable_mach_hardening(task);
	}
#if DEVELOPMENT || DEBUG
	/* Disable mach hardening for all tasks if amfi_get_out_of_my_way is set.
	 * Customers will have to turn SIP off to use this boot-arg, and so this is
	 * only needed internally since we disable this feature when SIP is off. */
	if (AMFI_bootarg_disable_mach_hardening) {
		if (cs_debug) {
			printf("Disabling some mach hardening on task due to AMFI boot-args: pid = %d\n", proc_getpid(p));
		}
		task_disable_mach_hardening(task);
	}
#endif /* DEVELOPMENT || DEBUG */
#endif /* XNU_TARGET_OS_OSX */

	/*
	 * Set starting EXC_GUARD and control port behavior for task now that
	 * platform and hardened runtime is set. Use the name directly from imgp since we haven't
	 * set_proc_name() yet. Also make control port for the task and main thread
	 * immovable/pinned based on task's option.
	 *
	 * Must happen before main thread port copyout in exc_add_apple_strings.
	 */
	task_set_exc_guard_ctrl_port_default(task, thread,
	    imgp->ip_ndp->ni_cnd.cn_nameptr,
	    (unsigned)imgp->ip_ndp->ni_cnd.cn_namelen,
	    proc_is_simulated(p),
	    load_result.ip_platform,
	    load_result.lr_sdk);

	error = exec_add_apple_strings(imgp, &load_result); /* copies out main thread port */

	if (error) {
		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_APPLE_STRING_INIT, 0, 0);

		exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_APPLE_STRING_INIT);
		if (bootarg_execfailurereports) {
			set_proc_name(imgp, p);
			exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
		}
		goto badtoolate;
	}

	/* Switch to target task's map to copy out strings */
	old_map = vm_map_switch(get_task_map(task));

	if (load_result.unixproc) {
		user_addr_t     ap;

		/*
		 * Copy the strings area out into the new process address
		 * space.
		 */
		ap = p->user_stack;
		error = exec_copyout_strings(imgp, &ap);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_STRINGS, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_STRINGS);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto badtoolate;
		}
		/* Set the stack */
		thread_setuserstack(thread, ap);
	}

	if (load_result.dynlinker || load_result.is_rosetta) {
		user_addr_t        ap;
		int                     new_ptr_size = (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) ? 8 : 4;

		/* Adjust the stack */
		ap = thread_adjuserstack(thread, -new_ptr_size);
		error = copyoutptr(load_result.mach_header, ap, new_ptr_size);

		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_DYNLINKER, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_DYNLINKER);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto badtoolate;
		}
		error = task_set_dyld_info(task, load_result.all_image_info_addr,
		    load_result.all_image_info_size);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_SET_DYLD_INFO, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_SET_DYLD_INFO);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			error = EINVAL;
			goto badtoolate;
		}
	}

#if CONFIG_ROSETTA
	if (load_result.is_rosetta) {
		// Add an fd for the executable file for Rosetta's use
		int main_binary_fd;
		struct fileproc *fp;

		error = falloc_exec(p, imgp->ip_vfs_context, &fp, &main_binary_fd);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_MAIN_FD_ALLOC, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_MAIN_FD_ALLOC);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto badtoolate;
		}

		error = VNOP_OPEN(imgp->ip_vp, FREAD, imgp->ip_vfs_context);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_MAIN_FD_ALLOC, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_MAIN_FD_ALLOC);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto cleanup_rosetta_fp;
		}

		fp->fp_glob->fg_flag = FREAD;
		fp->fp_glob->fg_ops = &vnops;
		fp_set_data(fp, imgp->ip_vp);

		proc_fdlock(p);
		procfdtbl_releasefd(p, main_binary_fd, NULL);
		fp_drop(p, main_binary_fd, fp, 1);
		proc_fdunlock(p);

		vnode_ref(imgp->ip_vp);

		// Pass the dyld load address, main binary fd, and dyld fd on the stack
		uint64_t ap = thread_adjuserstack(thread, -24);

		error = copyoutptr((user_addr_t)load_result.dynlinker_fd, ap, 8);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto cleanup_rosetta_fp;
		}

		error = copyoutptr(load_result.dynlinker_mach_header, ap + 8, 8);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto cleanup_rosetta_fp;
		}

		error = copyoutptr((user_addr_t)main_binary_fd, ap + 16, 8);
		if (error) {
			vm_map_switch(old_map);

			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA, 0, 0);

			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_COPYOUT_ROSETTA);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, p);
				exec_failure_reason->osr_flags |= OS_REASON_FLAG_GENERATE_CRASH_REPORT;
			}
			goto cleanup_rosetta_fp;
		}

cleanup_rosetta_fp:
		if (error) {
			fp_free(p, load_result.dynlinker_fd, load_result.dynlinker_fp);
			fp_free(p, main_binary_fd, fp);
			goto badtoolate;
		}
	}

#endif

	/* Avoid immediate VM faults back into kernel */
	exec_prefault_data(p, imgp, &load_result);

	vm_map_switch(old_map);

	/*
	 * Reset signal state.
	 */
	execsigs(p, thread);

	/*
	 * need to cancel async IO requests that can be cancelled and wait for those
	 * already active.  MAY BLOCK!
	 */
	_aio_exec( p );

#if SYSV_SHM
	/* FIXME: Till vmspace inherit is fixed: */
	if (!vfexec && p->vm_shm) {
		shmexec(p);
	}
#endif
#if SYSV_SEM
	/* Clean up the semaphores */
	semexit(p);
#endif

	/*
	 * Remember file name for accounting.
	 */
	p->p_acflag &= ~AFORK;

	set_proc_name(imgp, p);

#if CONFIG_SECLUDED_MEMORY
	if (secluded_for_apps &&
	    load_result.platform_binary) {
		if (strncmp(p->p_name,
		    "Camera",
		    sizeof(p->p_name)) == 0) {
			task_set_could_use_secluded_mem(task, TRUE);
		} else {
			task_set_could_use_secluded_mem(task, FALSE);
		}
		if (strncmp(p->p_name,
		    "mediaserverd",
		    sizeof(p->p_name)) == 0) {
			task_set_could_also_use_secluded_mem(task, TRUE);
		}
	}
#endif /* CONFIG_SECLUDED_MEMORY */

#if __arm64__
	if (load_result.legacy_footprint) {
		task_set_legacy_footprint(task);
	}
#endif /* __arm64__ */

	pal_dbg_set_task_name(task);

#if DEVELOPMENT || DEBUG
	/*
	 * Update the pid an proc name for importance base if any
	 */
	task_importance_update_owner_info(task);
#endif

	proc_setexecutableuuid(p, &load_result.uuid[0]);

#if CONFIG_DTRACE
	dtrace_proc_exec(p);
#endif

	if (kdebug_enable) {
		long args[4] = {};

		uintptr_t fsid = 0, fileid = 0;
		if (imgp->ip_vattr) {
			uint64_t fsid64 = vnode_get_va_fsid(imgp->ip_vattr);
			fsid   = (uintptr_t)fsid64;
			fileid = (uintptr_t)imgp->ip_vattr->va_fileid;
			// check for (unexpected) overflow and trace zero in that case
			if (fsid != fsid64 || fileid != imgp->ip_vattr->va_fileid) {
				fsid = fileid = 0;
			}
		}
		KERNEL_DEBUG_CONSTANT_IST1(TRACE_DATA_EXEC, proc_getpid(p), fsid, fileid, 0,
		    (uintptr_t)thread_tid(thread));

		extern void kdebug_proc_name_args(struct proc *proc, long args[static 4]);
		kdebug_proc_name_args(p, args);
		KERNEL_DEBUG_CONSTANT_IST1(TRACE_STRING_EXEC, args[0], args[1],
		    args[2], args[3], (uintptr_t)thread_tid(thread));
	}


	/*
	 * If posix_spawned with the START_SUSPENDED flag, stop the
	 * process before it runs.
	 */
	if (imgp->ip_px_sa != NULL) {
		psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
		if (psa->psa_flags & POSIX_SPAWN_START_SUSPENDED) {
			proc_lock(p);
			p->p_stat = SSTOP;
			proc_unlock(p);
			(void) task_suspend_internal(task);
		}
	}

	/*
	 * mark as execed
	 */
	OSBitOrAtomic(P_EXEC, &p->p_flag);
	proc_resetregister(p);
	if (p->p_pptr && (p->p_lflag & P_LPPWAIT)) {
		proc_lock(p);
		p->p_lflag &= ~P_LPPWAIT;
		proc_unlock(p);
		wakeup((caddr_t)p->p_pptr);
	}

	/*
	 * Set up dext coredumps on kernel panic.
	 * This requires the following:
	 * - dext_panic_coredump=1 boot-arg (enabled by default on DEVELOPMENT, DEBUG and certain Seed builds)
	 * - process must be a driver
	 * - process must have the com.apple.private.enable-coredump-on-panic entitlement, and the
	 *   entitlement has a string value.
	 * - process must have the com.apple.private.enable-coredump-on-panic-seed-privacy-approved
	 *   entitlement (Seed builds only).
	 *
	 * The core dump file name is formatted with the entitlement string value, followed by a hyphen
	 * and the process PID.
	 */
	if (enable_dext_coredumps_on_panic &&
	    (imgp->ip_flags & IMGPF_DRIVER) != 0 &&
	    (userspace_coredump_name = IOVnodeGetEntitlement(imgp->ip_vp,
	    (int64_t)imgp->ip_arch_offset, USERSPACE_COREDUMP_PANIC_ENTITLEMENT)) != NULL) {
		size_t userspace_coredump_name_len = strlen(userspace_coredump_name);

		char core_name[MACH_CORE_FILEHEADER_NAMELEN];
		/* 16 - NULL char - strlen("-") - maximum of 5 digits for pid */
		snprintf(core_name, MACH_CORE_FILEHEADER_NAMELEN, "%.9s-%d", userspace_coredump_name, proc_getpid(p));

		kern_register_userspace_coredump(task, core_name);

		/* Discard the copy of the entitlement */
		kfree_data(userspace_coredump_name, userspace_coredump_name_len + 1);
		userspace_coredump_name = NULL;
	}

	goto done;

badtoolate:
	/* Don't allow child process to execute any instructions */
	if (!spawn) {
		{
			assert(exec_failure_reason != OS_REASON_NULL);
			if (bootarg_execfailurereports) {
				set_proc_name(imgp, current_proc());
			}
			psignal_with_reason(current_proc(), SIGKILL, exec_failure_reason);
			exec_failure_reason = OS_REASON_NULL;

			if (exec) {
				/* Terminate the exec copy task */
				task_terminate_internal(task);
			}
		}

		/* We can't stop this system call at this point, so just pretend we succeeded */
		error = 0;
	} else {
		os_reason_free(exec_failure_reason);
		exec_failure_reason = OS_REASON_NULL;
	}

done:
	if (load_result.threadstate) {
		kfree_data(load_result.threadstate, load_result.threadstate_sz);
		load_result.threadstate = NULL;
	}

bad:
	/* If we hit this, we likely would have leaked an exit reason */
	assert(exec_failure_reason == OS_REASON_NULL);
	return error;
}




/*
 * Our image activator table; this is the table of the image types we are
 * capable of loading.  We list them in order of preference to ensure the
 * fastest image load speed.
 *
 * XXX hardcoded, for now; should use linker sets
 */
struct execsw {
	int(*const ex_imgact)(struct image_params *);
	const char *ex_name;
}const execsw[] = {
	{ exec_mach_imgact, "Mach-o Binary" },
	{ exec_fat_imgact, "Fat Binary" },
	{ exec_shell_imgact, "Interpreter Script" },
	{ NULL, NULL}
};


/*
 * exec_activate_image
 *
 * Description:	Iterate through the available image activators, and activate
 *		the image associated with the imgp structure.  We start with
 *		the activator for Mach-o binaries followed by that for Fat binaries
 *		for Interpreter scripts.
 *
 * Parameters:	struct image_params *	Image parameter block
 *
 * Returns:	0			Success
 *		ENOEXEC			No activator for image.
 *		EBADEXEC		The executable is corrupt/unknown
 *	execargs_alloc:EINVAL		Invalid argument
 *	execargs_alloc:EACCES		Permission denied
 *	execargs_alloc:EINTR		Interrupted function
 *	execargs_alloc:ENOMEM		Not enough space
 *	exec_save_path:EFAULT		Bad address
 *	exec_save_path:ENAMETOOLONG	Filename too long
 *	exec_check_permissions:EACCES	Permission denied
 *	exec_check_permissions:ENOEXEC	Executable file format error
 *	exec_check_permissions:ETXTBSY	Text file busy [misuse of error code]
 *	exec_check_permissions:???
 *	namei:???
 *	vn_rdwr:???			[anything vn_rdwr can return]
 *	<ex_imgact>:???			[anything an imgact can return]
 *	EDEADLK				Process is being terminated
 */
static int
exec_activate_image(struct image_params *imgp)
{
	struct nameidata *ndp = NULL;
	const char *excpath;
	int error;
	int resid;
	int once = 1;   /* save SGUID-ness for interpreted files */
	int i;
	int itercount = 0;
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);

	/*
	 * For exec, the translock needs to be taken on old proc and not
	 * on new shadow proc.
	 */
	if (imgp->ip_flags & IMGPF_EXEC) {
		p = current_proc();
	}

	error = execargs_alloc(imgp);
	if (error) {
		goto bad_notrans;
	}

	error = exec_save_path(imgp, imgp->ip_user_fname, imgp->ip_seg, &excpath);
	if (error) {
		goto bad_notrans;
	}

	/* Use excpath, which contains the copyin-ed exec path */
	DTRACE_PROC1(exec, uintptr_t, excpath);

	ndp = kalloc_type(struct nameidata, Z_WAITOK | Z_ZERO | Z_NOFAIL);

	NDINIT(ndp, LOOKUP, OP_LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1,
	    UIO_SYSSPACE, CAST_USER_ADDR_T(excpath), imgp->ip_vfs_context);

again:
	error = namei(ndp);
	if (error) {
		if (error == ERESTART) {
			error = EINTR;
		}
		goto bad_notrans;
	}
	imgp->ip_ndp = ndp;     /* successful namei(); call nameidone() later */
	imgp->ip_vp = ndp->ni_vp;       /* if set, need to vnode_put() at some point */

	/*
	 * Before we start the transition from binary A to binary B, make
	 * sure another thread hasn't started exiting the process.  We grab
	 * the proc lock to check p_lflag initially, and the transition
	 * mechanism ensures that the value doesn't change after we release
	 * the lock.
	 */
	proc_lock(p);
	if (p->p_lflag & P_LEXIT) {
		error = EDEADLK;
		proc_unlock(p);
		goto bad_notrans;
	}
	error = proc_transstart(p, 1, 0);
	proc_unlock(p);
	if (error) {
		goto bad_notrans;
	}

	error = exec_check_permissions(imgp);
	if (error) {
		goto bad;
	}

	/* Copy; avoid invocation of an interpreter overwriting the original */
	if (once) {
		once = 0;
		*imgp->ip_origvattr = *imgp->ip_vattr;
	}

	error = vn_rdwr(UIO_READ, imgp->ip_vp, imgp->ip_vdata, PAGE_SIZE, 0,
	    UIO_SYSSPACE, IO_NODELOCKED,
	    vfs_context_ucred(imgp->ip_vfs_context),
	    &resid, vfs_context_proc(imgp->ip_vfs_context));
	if (error) {
		goto bad;
	}

	if (resid) {
		memset(imgp->ip_vdata + (PAGE_SIZE - resid), 0x0, resid);
	}

encapsulated_binary:
	/* Limit the number of iterations we will attempt on each binary */
	if (++itercount > EAI_ITERLIMIT) {
		error = EBADEXEC;
		goto bad;
	}
	error = -1;
	for (i = 0; error == -1 && execsw[i].ex_imgact != NULL; i++) {
		error = (*execsw[i].ex_imgact)(imgp);

		switch (error) {
		/* case -1: not claimed: continue */
		case -2:                /* Encapsulated binary, imgp->ip_XXX set for next iteration */
			goto encapsulated_binary;

		case -3:                /* Interpreter */
#if CONFIG_MACF
			/*
			 * Copy the script label for later use. Note that
			 * the label can be different when the script is
			 * actually read by the interpreter.
			 */
			if (imgp->ip_scriptlabelp) {
				mac_vnode_label_free(imgp->ip_scriptlabelp);
				imgp->ip_scriptlabelp = NULL;
			}
			imgp->ip_scriptlabelp = mac_vnode_label_alloc(NULL);
			if (imgp->ip_scriptlabelp == NULL) {
				error = ENOMEM;
				break;
			}
			mac_vnode_label_copy(mac_vnode_label(imgp->ip_vp),
			    imgp->ip_scriptlabelp);

			/*
			 * Take a ref of the script vnode for later use.
			 */
			if (imgp->ip_scriptvp) {
				vnode_put(imgp->ip_scriptvp);
				imgp->ip_scriptvp = NULLVP;
			}
			if (vnode_getwithref(imgp->ip_vp) == 0) {
				imgp->ip_scriptvp = imgp->ip_vp;
			}
#endif

			nameidone(ndp);

			vnode_put(imgp->ip_vp);
			imgp->ip_vp = NULL;     /* already put */
			imgp->ip_ndp = NULL; /* already nameidone */

			/* Use excpath, which exec_shell_imgact reset to the interpreter */
			NDINIT(ndp, LOOKUP, OP_LOOKUP, FOLLOW | LOCKLEAF,
			    UIO_SYSSPACE, CAST_USER_ADDR_T(excpath), imgp->ip_vfs_context);

			proc_transend(p, 0);
			goto again;

		default:
			break;
		}
	}

	if (error == -1) {
		error = ENOEXEC;
	} else if (error == 0) {
		if (imgp->ip_flags & IMGPF_INTERPRET && ndp->ni_vp) {
			AUDIT_ARG(vnpath, ndp->ni_vp, ARG_VNODE2);
		}

		/*
		 * Call out to allow 3rd party notification of exec.
		 * Ignore result of kauth_authorize_fileop call.
		 */
		if (kauth_authorize_fileop_has_listeners()) {
			kauth_authorize_fileop(vfs_context_ucred(imgp->ip_vfs_context),
			    KAUTH_FILEOP_EXEC,
			    (uintptr_t)ndp->ni_vp, 0);
		}
	}
bad:
	proc_transend(p, 0);

bad_notrans:
	if (imgp->ip_strings) {
		execargs_free(imgp);
	}
	if (imgp->ip_ndp) {
		nameidone(imgp->ip_ndp);
	}
	kfree_type(struct nameidata, ndp);

	return error;
}

/*
 * exec_validate_spawnattr_policy
 *
 * Description: Validates the entitlements required to set the apptype.
 *
 * Parameters:  int psa_apptype         posix spawn attribute apptype
 *
 * Returns:     0                       Success
 *              EPERM                   Failure
 */
static errno_t
exec_validate_spawnattr_policy(int psa_apptype)
{
	if ((psa_apptype & POSIX_SPAWN_PROC_TYPE_MASK) != 0) {
		int proctype = psa_apptype & POSIX_SPAWN_PROC_TYPE_MASK;
		if (proctype == POSIX_SPAWN_PROC_TYPE_DRIVER) {
			if (!IOCurrentTaskHasEntitlement(POSIX_SPAWN_ENTITLEMENT_DRIVER)) {
				return EPERM;
			}
		}
	}

	return 0;
}

/*
 * exec_handle_spawnattr_policy
 *
 * Description: Decode and apply the posix_spawn apptype, qos clamp, and watchport ports to the task.
 *
 * Parameters:  proc_t p                process to apply attributes to
 *              int psa_apptype         posix spawn attribute apptype
 *
 * Returns:     0                       Success
 */
static errno_t
exec_handle_spawnattr_policy(proc_t p, thread_t thread, int psa_apptype, uint64_t psa_qos_clamp,
    task_role_t psa_darwin_role, struct exec_port_actions *port_actions)
{
	int apptype     = TASK_APPTYPE_NONE;
	int qos_clamp   = THREAD_QOS_UNSPECIFIED;
	task_role_t role = TASK_UNSPECIFIED;

	if ((psa_apptype & POSIX_SPAWN_PROC_TYPE_MASK) != 0) {
		int proctype = psa_apptype & POSIX_SPAWN_PROC_TYPE_MASK;

		switch (proctype) {
		case POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE:
			apptype = TASK_APPTYPE_DAEMON_INTERACTIVE;
			break;
		case POSIX_SPAWN_PROC_TYPE_DAEMON_STANDARD:
			apptype = TASK_APPTYPE_DAEMON_STANDARD;
			break;
		case POSIX_SPAWN_PROC_TYPE_DAEMON_ADAPTIVE:
			apptype = TASK_APPTYPE_DAEMON_ADAPTIVE;
			break;
		case POSIX_SPAWN_PROC_TYPE_DAEMON_BACKGROUND:
			apptype = TASK_APPTYPE_DAEMON_BACKGROUND;
			break;
		case POSIX_SPAWN_PROC_TYPE_APP_DEFAULT:
			apptype = TASK_APPTYPE_APP_DEFAULT;
			break;
		case POSIX_SPAWN_PROC_TYPE_DRIVER:
			apptype = TASK_APPTYPE_DRIVER;
			break;
		default:
			apptype = TASK_APPTYPE_NONE;
			/* TODO: Should an invalid value here fail the spawn? */
			break;
		}
	}

	if (psa_qos_clamp != POSIX_SPAWN_PROC_CLAMP_NONE) {
		switch (psa_qos_clamp) {
		case POSIX_SPAWN_PROC_CLAMP_UTILITY:
			qos_clamp = THREAD_QOS_UTILITY;
			break;
		case POSIX_SPAWN_PROC_CLAMP_BACKGROUND:
			qos_clamp = THREAD_QOS_BACKGROUND;
			break;
		case POSIX_SPAWN_PROC_CLAMP_MAINTENANCE:
			qos_clamp = THREAD_QOS_MAINTENANCE;
			break;
		default:
			qos_clamp = THREAD_QOS_UNSPECIFIED;
			/* TODO: Should an invalid value here fail the spawn? */
			break;
		}
	}

	if (psa_darwin_role != PRIO_DARWIN_ROLE_DEFAULT) {
		proc_darwin_role_to_task_role(psa_darwin_role, &role);
	}

	if (apptype != TASK_APPTYPE_NONE ||
	    qos_clamp != THREAD_QOS_UNSPECIFIED ||
	    role != TASK_UNSPECIFIED ||
	    port_actions->portwatch_count) {
		proc_set_task_spawnpolicy(proc_task(p), thread, apptype, qos_clamp, role,
		    port_actions->portwatch_array, port_actions->portwatch_count);
	}

	if (port_actions->registered_count) {
		if (_kernelrpc_mach_ports_register3(proc_task(p),
		    port_actions->registered_array[0],
		    port_actions->registered_array[1],
		    port_actions->registered_array[2])) {
			return EINVAL;
		}
		/* mach_ports_register() consumed the array */
		bzero(port_actions->registered_array,
		    sizeof(port_actions->registered_array));
		port_actions->registered_count = 0;
	}

	return 0;
}

static void
exec_port_actions_destroy(struct exec_port_actions *port_actions)
{
	if (port_actions->excport_array) {
		for (uint32_t i = 0; i < port_actions->exception_port_count; i++) {
			ipc_port_t port = NULL;
			if ((port = port_actions->excport_array[i].port) != NULL) {
				ipc_port_release_send(port);
			}
		}
		kfree_type(struct exception_port_action_t, port_actions->exception_port_count,
		    port_actions->excport_array);
	}

	if (port_actions->portwatch_array) {
		for (uint32_t i = 0; i < port_actions->portwatch_count; i++) {
			ipc_port_t port = NULL;
			if ((port = port_actions->portwatch_array[i]) != NULL) {
				ipc_port_release_send(port);
			}
		}
		kfree_type(ipc_port_t, port_actions->portwatch_count,
		    port_actions->portwatch_array);
	}

	for (uint32_t i = 0; i < port_actions->registered_count; i++) {
		ipc_port_t port = NULL;
		if ((port = port_actions->registered_array[i]) != NULL) {
			ipc_port_release_send(port);
		}
	}
}

/*
 * exec_handle_port_actions
 *
 * Description:	Go through the _posix_port_actions_t contents,
 *              calling task_set_special_port, task_set_exception_ports
 *              and/or audit_session_spawnjoin for the current task.
 *
 * Parameters:	struct image_params *	Image parameter block
 *
 * Returns:	0			Success
 *              EINVAL			Failure
 *              ENOTSUP			Illegal posix_spawn attr flag was set
 */
static errno_t
exec_handle_port_actions(struct image_params *imgp,
    struct exec_port_actions *actions)
{
	_posix_spawn_port_actions_t pacts = imgp->ip_px_spa;
#if CONFIG_AUDIT
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);
#endif
	_ps_port_action_t *act = NULL;
	task_t task = get_threadtask(imgp->ip_new_thread);
	ipc_port_t port = NULL;
	errno_t ret = 0;
	int i = 0, portwatch_i = 0, registered_i = 0, excport_i = 0;
	kern_return_t kr;
	boolean_t task_has_watchport_boost = task_has_watchports(current_task());
	boolean_t in_exec = (imgp->ip_flags & IMGPF_EXEC);
	int ptrauth_task_port_count = 0;

	for (i = 0; i < pacts->pspa_count; i++) {
		act = &pacts->pspa_actions[i];

		switch (act->port_type) {
		case PSPA_SPECIAL:
#if CONFIG_AUDIT
		case PSPA_AU_SESSION:
#endif
			break;
		case PSPA_EXCEPTION:
			if (++actions->exception_port_count > TASK_MAX_EXCEPTION_PORT_COUNT) {
				ret = EINVAL;
				goto done;
			}
			break;
		case PSPA_IMP_WATCHPORTS:
			if (++actions->portwatch_count > TASK_MAX_WATCHPORT_COUNT) {
				ret = EINVAL;
				goto done;
			}
			break;
		case PSPA_REGISTERED_PORTS:
			if (++actions->registered_count > TASK_PORT_REGISTER_MAX) {
				ret = EINVAL;
				goto done;
			}
			break;
		case PSPA_PTRAUTH_TASK_PORT:
			if (++ptrauth_task_port_count > 1) {
				ret = EINVAL;
				goto done;
			}
			break;
		default:
			ret = EINVAL;
			goto done;
		}
	}

	if (actions->exception_port_count) {
		actions->excport_array = kalloc_type(struct exception_port_action_t,
		    actions->exception_port_count, Z_WAITOK | Z_ZERO);

		if (actions->excport_array == NULL) {
			ret = ENOMEM;
			goto done;
		}
	}
	if (actions->portwatch_count) {
		if (in_exec && task_has_watchport_boost) {
			ret = EINVAL;
			goto done;
		}
		actions->portwatch_array = kalloc_type(ipc_port_t,
		    actions->portwatch_count, Z_WAITOK | Z_ZERO);
		if (actions->portwatch_array == NULL) {
			ret = ENOMEM;
			goto done;
		}
	}

	for (i = 0; i < pacts->pspa_count; i++) {
		act = &pacts->pspa_actions[i];

		if (MACH_PORT_VALID(act->new_port)) {
			kr = ipc_object_copyin(get_task_ipcspace(current_task()),
			    act->new_port, MACH_MSG_TYPE_COPY_SEND,
			    (ipc_object_t *) &port, 0, NULL, IPC_OBJECT_COPYIN_FLAGS_ALLOW_IMMOVABLE_SEND);

			if (kr != KERN_SUCCESS) {
				ret = EINVAL;
				goto done;
			}
		} else {
			/* it's NULL or DEAD */
			port = CAST_MACH_NAME_TO_PORT(act->new_port);
		}

		switch (act->port_type) {
		case PSPA_SPECIAL:
			kr = task_set_special_port(task, act->which, port);

			if (kr != KERN_SUCCESS) {
				ret = EINVAL;
			}
			break;

#if CONFIG_AUDIT
		case PSPA_AU_SESSION:
			ret = audit_session_spawnjoin(p, port);
			if (ret) {
				/* audit_session_spawnjoin() has already dropped the reference in case of error. */
				goto done;
			}

			break;
#endif
		case PSPA_EXCEPTION:
			assert(excport_i < actions->exception_port_count);
			/* hold on to this till end of spawn */
			actions->excport_array[excport_i].port_action = act;
			actions->excport_array[excport_i].port = port;
			excport_i++;
			break;
		case PSPA_IMP_WATCHPORTS:
			assert(portwatch_i < actions->portwatch_count);
			/* hold on to this till end of spawn */
			actions->portwatch_array[portwatch_i++] = port;
			break;
		case PSPA_REGISTERED_PORTS:
			assert(registered_i < actions->registered_count);
			/* hold on to this till end of spawn */
			actions->registered_array[registered_i++] = port;
			break;

		case PSPA_PTRAUTH_TASK_PORT:
#if (DEVELOPMENT || DEBUG)
#if defined(HAS_APPLE_PAC)
			{
				task_t ptr_auth_task = convert_port_to_task(port);

				if (ptr_auth_task == TASK_NULL) {
					ret = EINVAL;
					break;
				}

				imgp->ip_inherited_shared_region_id =
				    task_get_vm_shared_region_id_and_jop_pid(ptr_auth_task,
				    &imgp->ip_inherited_jop_pid);

				/* Deallocate task ref returned by convert_port_to_task */
				task_deallocate(ptr_auth_task);
			}
#endif /* HAS_APPLE_PAC */
#endif /* (DEVELOPMENT || DEBUG) */

			/* consume the port right in case of success */
			ipc_port_release_send(port);
			break;
		default:
			ret = EINVAL;
			break;
		}

		if (ret) {
			/* action failed, so release port resources */
			ipc_port_release_send(port);
			break;
		}
	}

done:
	if (0 != ret) {
		DTRACE_PROC1(spawn__port__failure, mach_port_name_t, act->new_port);
	}
	return ret;
}


/*
 * exec_handle_exception_port_actions
 *
 * Description:	Go through the saved exception ports in exec_port_actions,
 *              calling task_set_exception_ports for the current Task.
 *              This must happen after image activation, and after exec_resettextvp()
 *				because task_set_exception_ports checks the `TF_PLATFORM` bit and entitlements.
 *
 * Parameters:	struct image_params *		Image parameter block
 *                              struct exec_port_actions *  Saved Port Actions
 *
 * Returns:	0			Success
 *              EINVAL			task_set_exception_ports failed
 */
static errno_t
exec_handle_exception_port_actions(const struct image_params *imgp,
    const struct exec_port_actions *actions)
{
	task_t task = get_threadtask(imgp->ip_new_thread);

	for (int i = 0; i < actions->exception_port_count; i++) {
		ipc_port_t port = actions->excport_array[i].port;
		_ps_port_action_t *act = actions->excport_array[i].port_action;
		assert(act != NULL);
		kern_return_t kr = task_set_exception_ports(task, act->mask, port,
		    act->behavior, act->flavor);
		if (kr != KERN_SUCCESS) {
			DTRACE_PROC1(spawn__exception__port__failure, mach_port_name_t, act->new_port);
			return EINVAL;
		}
		actions->excport_array[i].port = NULL;
	}

	return 0;
}


/*
 * exec_handle_file_actions
 *
 * Description:	Go through the _posix_file_actions_t contents applying the
 *		open, close, and dup2 operations to the open file table for
 *		the current process.
 *
 * Parameters:	struct image_params *	Image parameter block
 *
 * Returns:	0			Success
 *		???
 *
 * Note:	Actions are applied in the order specified, with the credential
 *		of the parent process.  This is done to permit the parent
 *		process to utilize POSIX_SPAWN_RESETIDS to drop privilege in
 *		the child following operations the child may in fact not be
 *		normally permitted to perform.
 */
static int
exec_handle_file_actions(struct image_params *imgp, short psa_flags)
{
	int error = 0;
	int action;
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);
	kauth_cred_t p_cred = vfs_context_ucred(imgp->ip_vfs_context);
	_posix_spawn_file_actions_t px_sfap = imgp->ip_px_sfa;
	int ival[2];            /* dummy retval for system calls) */
#if CONFIG_AUDIT
	struct uthread *uthread = current_uthread();
#endif

	for (action = 0; action < px_sfap->psfa_act_count; action++) {
		_psfa_action_t *psfa = &px_sfap->psfa_act_acts[action];

		switch (psfa->psfaa_type) {
		case PSFA_OPEN: {
			/*
			 * Open is different, in that it requires the use of
			 * a path argument, which is normally copied in from
			 * user space; because of this, we have to support an
			 * open from kernel space that passes an address space
			 * context of UIO_SYSSPACE, and casts the address
			 * argument to a user_addr_t.
			 */
			struct vnode_attr *vap;
			struct nameidata *ndp;
			int mode = psfa->psfaa_openargs.psfao_mode;
			int origfd;
			struct {
				struct vnode_attr va;
				struct nameidata nd;
			} *__open_data;

			__open_data = kalloc_type(typeof(*__open_data), Z_WAITOK | Z_ZERO);
			if (__open_data == NULL) {
				error = ENOMEM;
				break;
			}

			vap = &__open_data->va;
			ndp = &__open_data->nd;

			VATTR_INIT(vap);
			/* Mask off all but regular access permissions */
			mode = ((mode & ~p->p_fd.fd_cmask) & ALLPERMS) & ~S_ISTXT;
			VATTR_SET(vap, va_mode, mode & ACCESSPERMS);

			AUDIT_SUBCALL_ENTER(OPEN, p, uthread);

			NDINIT(ndp, LOOKUP, OP_OPEN, FOLLOW | AUDITVNPATH1, UIO_SYSSPACE,
			    CAST_USER_ADDR_T(psfa->psfaa_openargs.psfao_path),
			    imgp->ip_vfs_context);

			error = open1(imgp->ip_vfs_context, ndp,
			    psfa->psfaa_openargs.psfao_oflag,
			    vap, NULL, NULL, &origfd, AUTH_OPEN_NOAUTHFD);

			kfree_type(typeof(*__open_data), __open_data);

			AUDIT_SUBCALL_EXIT(uthread, error);

			/*
			 * If there's an error, or we get the right fd by
			 * accident, then drop out here.  This is easier than
			 * reworking all the open code to preallocate fd
			 * slots, and internally taking one as an argument.
			 */
			if (error || origfd == psfa->psfaa_filedes) {
				break;
			}

			/*
			 * If we didn't fall out from an error, we ended up
			 * with the wrong fd; so now we've got to try to dup2
			 * it to the right one.
			 */
			AUDIT_SUBCALL_ENTER(DUP2, p, uthread);
			error = dup2(p, p_cred, origfd, psfa->psfaa_filedes, ival);
			AUDIT_SUBCALL_EXIT(uthread, error);
			if (error) {
				break;
			}

			/*
			 * Finally, close the original fd.
			 */
			AUDIT_SUBCALL_ENTER(CLOSE, p, uthread);
			error = close_nocancel(p, p_cred, origfd);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		case PSFA_DUP2: {
			AUDIT_SUBCALL_ENTER(DUP2, p, uthread);
			error = dup2(p, p_cred, psfa->psfaa_filedes,
			    psfa->psfaa_dup2args.psfad_newfiledes, ival);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		case PSFA_FILEPORT_DUP2: {
			ipc_port_t port;
			kern_return_t kr;
			int origfd;

			if (!MACH_PORT_VALID(psfa->psfaa_fileport)) {
				error = EINVAL;
				break;
			}

			kr = ipc_object_copyin(get_task_ipcspace(current_task()),
			    psfa->psfaa_fileport, MACH_MSG_TYPE_COPY_SEND,
			    (ipc_object_t *) &port, 0, NULL, IPC_OBJECT_COPYIN_FLAGS_ALLOW_IMMOVABLE_SEND);

			if (kr != KERN_SUCCESS) {
				error = EINVAL;
				break;
			}

			error = fileport_makefd(p, port, 0, &origfd);

			if (IPC_PORT_NULL != port) {
				ipc_port_release_send(port);
			}

			if (error || origfd == psfa->psfaa_dup2args.psfad_newfiledes) {
				break;
			}

			AUDIT_SUBCALL_ENTER(DUP2, p, uthread);
			error = dup2(p, p_cred, origfd,
			    psfa->psfaa_dup2args.psfad_newfiledes, ival);
			AUDIT_SUBCALL_EXIT(uthread, error);
			if (error) {
				break;
			}

			AUDIT_SUBCALL_ENTER(CLOSE, p, uthread);
			error = close_nocancel(p, p_cred, origfd);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		case PSFA_CLOSE: {
			AUDIT_SUBCALL_ENTER(CLOSE, p, uthread);
			error = close_nocancel(p, p_cred, psfa->psfaa_filedes);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		case PSFA_INHERIT: {
			struct fileproc *fp;

			/*
			 * Check to see if the descriptor exists, and
			 * ensure it's -not- marked as close-on-exec.
			 *
			 * Attempting to "inherit" a guarded fd will
			 * result in a error.
			 */

			proc_fdlock(p);
			if ((fp = fp_get_noref_locked(p, psfa->psfaa_filedes)) == NULL) {
				error = EBADF;
			} else if (fp->fp_guard_attrs) {
				error = fp_guard_exception(p, psfa->psfaa_filedes,
				    fp, kGUARD_EXC_NOCLOEXEC);
			} else {
				fp->fp_flags &= ~FP_CLOEXEC;
				error = 0;
			}
			proc_fdunlock(p);
		}
		break;

		case PSFA_CHDIR: {
			/*
			 * Chdir is different, in that it requires the use of
			 * a path argument, which is normally copied in from
			 * user space; because of this, we have to support a
			 * chdir from kernel space that passes an address space
			 * context of UIO_SYSSPACE, and casts the address
			 * argument to a user_addr_t.
			 */
			struct nameidata *nd;
			nd = kalloc_type(struct nameidata,
			    Z_WAITOK | Z_ZERO | Z_NOFAIL);

			AUDIT_SUBCALL_ENTER(CHDIR, p, uthread);
			NDINIT(nd, LOOKUP, OP_CHDIR, FOLLOW | AUDITVNPATH1, UIO_SYSSPACE,
			    CAST_USER_ADDR_T(psfa->psfaa_chdirargs.psfac_path),
			    imgp->ip_vfs_context);

			error = chdir_internal(p, imgp->ip_vfs_context, nd, 0);
			kfree_type(struct nameidata, nd);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		case PSFA_FCHDIR: {
			AUDIT_SUBCALL_ENTER(FCHDIR, p, uthread);
			error = fchdir(p, imgp->ip_vfs_context,
			    psfa->psfaa_filedes, false);
			AUDIT_SUBCALL_EXIT(uthread, error);
		}
		break;

		default:
			error = EINVAL;
			break;
		}

		/* All file actions failures are considered fatal, per POSIX */

		if (error) {
			if (PSFA_OPEN == psfa->psfaa_type) {
				DTRACE_PROC1(spawn__open__failure, uintptr_t,
				    psfa->psfaa_openargs.psfao_path);
			} else {
				DTRACE_PROC1(spawn__fd__failure, int, psfa->psfaa_filedes);
			}
			break;
		}
	}

	if (error != 0 || (psa_flags & POSIX_SPAWN_CLOEXEC_DEFAULT) == 0) {
		return error;
	}

	/*
	 * If POSIX_SPAWN_CLOEXEC_DEFAULT is set, behave (during
	 * this spawn only) as if "close on exec" is the default
	 * disposition of all pre-existing file descriptors.  In this case,
	 * the list of file descriptors mentioned in the file actions
	 * are the only ones that can be inherited, so mark them now.
	 *
	 * The actual closing part comes later, in fdt_exec().
	 */
	proc_fdlock(p);
	for (action = 0; action < px_sfap->psfa_act_count; action++) {
		_psfa_action_t *psfa = &px_sfap->psfa_act_acts[action];
		int fd = psfa->psfaa_filedes;

		switch (psfa->psfaa_type) {
		case PSFA_DUP2:
		case PSFA_FILEPORT_DUP2:
			fd = psfa->psfaa_dup2args.psfad_newfiledes;
			OS_FALLTHROUGH;
		case PSFA_OPEN:
		case PSFA_INHERIT:
			*fdflags(p, fd) |= UF_INHERIT;
			break;

		case PSFA_CLOSE:
		case PSFA_CHDIR:
		case PSFA_FCHDIR:
			/*
			 * Although PSFA_FCHDIR does have a file descriptor, it is not
			 * *creating* one, thus we do not automatically mark it for
			 * inheritance under POSIX_SPAWN_CLOEXEC_DEFAULT. A client that
			 * wishes it to be inherited should use the PSFA_INHERIT action
			 * explicitly.
			 */
			break;
		}
	}
	proc_fdunlock(p);

	return 0;
}

#if CONFIG_MACF
/*
 * Check that the extension's data is within the bounds of the
 * allocation storing all extensions' data
 */
static inline errno_t
exec_spawnattr_validate_policyext_data(const struct ip_px_smpx_s *px_s,
    const _ps_mac_policy_extension_t *ext)
{
	uint64_t dataend;

	if (__improbable(os_add_overflow(ext->dataoff, ext->datalen, &dataend))) {
		return EOVERFLOW;
	}
	if (__improbable(dataend > px_s->datalen)) {
		return EINVAL;
	}

	return 0;
}

/*
 * exec_spawnattr_getmacpolicyinfo
 */
void *
exec_spawnattr_getmacpolicyinfo(const void *macextensions, const char *policyname, size_t *lenp)
{
	const struct ip_px_smpx_s *px_s = macextensions;
	const struct _posix_spawn_mac_policy_extensions *psmx = NULL;
	int i;

	if (px_s == NULL) {
		return NULL;
	}

	psmx = px_s->array;
	if (psmx == NULL) {
		return NULL;
	}

	for (i = 0; i < psmx->psmx_count; i++) {
		const _ps_mac_policy_extension_t *extension = &psmx->psmx_extensions[i];
		if (strncmp(extension->policyname, policyname, sizeof(extension->policyname)) == 0) {
			if (__improbable(exec_spawnattr_validate_policyext_data(px_s, extension))) {
				panic("invalid mac policy extension data");
			}
			if (lenp != NULL) {
				*lenp = (size_t)extension->datalen;
			}
			return (void *)((uintptr_t)px_s->data + extension->dataoff);
		}
	}

	if (lenp != NULL) {
		*lenp = 0;
	}
	return NULL;
}

static int
spawn_copyin_macpolicyinfo(const struct user__posix_spawn_args_desc *px_args,
    struct ip_px_smpx_s *pxsp)
{
	_posix_spawn_mac_policy_extensions_t psmx = NULL;
	uint8_t *data = NULL;
	uint64_t datalen = 0;
	uint64_t dataoff = 0;
	int error = 0;

	bzero(pxsp, sizeof(*pxsp));

	if (px_args->mac_extensions_size < PS_MAC_EXTENSIONS_SIZE(1) ||
	    px_args->mac_extensions_size > PAGE_SIZE) {
		error = EINVAL;
		goto bad;
	}

	psmx = kalloc_data(px_args->mac_extensions_size, Z_WAITOK);
	if (psmx == NULL) {
		error = ENOMEM;
		goto bad;
	}

	error = copyin(px_args->mac_extensions, psmx, px_args->mac_extensions_size);
	if (error) {
		goto bad;
	}

	size_t extsize = PS_MAC_EXTENSIONS_SIZE(psmx->psmx_count);
	if (extsize == 0 || extsize > px_args->mac_extensions_size) {
		error = EINVAL;
		goto bad;
	}

	for (int i = 0; i < psmx->psmx_count; i++) {
		_ps_mac_policy_extension_t *extension = &psmx->psmx_extensions[i];
		if (extension->datalen == 0 || extension->datalen > PAGE_SIZE) {
			error = EINVAL;
			goto bad;
		}
		if (__improbable(os_add_overflow(datalen, extension->datalen, &datalen))) {
			error = ENOMEM;
			goto bad;
		}
	}

	data = kalloc_data((vm_size_t)datalen, Z_WAITOK);
	if (data == NULL) {
		error = ENOMEM;
		goto bad;
	}

	for (int i = 0; i < psmx->psmx_count; i++) {
		_ps_mac_policy_extension_t *extension = &psmx->psmx_extensions[i];

#if !__LP64__
		if (extension->data > UINT32_MAX) {
			goto bad;
		}
#endif
		error = copyin((user_addr_t)extension->data, &data[dataoff], (size_t)extension->datalen);
		if (error) {
			error = ENOMEM;
			goto bad;
		}
		extension->dataoff = dataoff;
		dataoff += extension->datalen;
	}

	pxsp->array = psmx;
	pxsp->data = data;
	pxsp->datalen = datalen;
	return 0;

bad:
	kfree_data(psmx, px_args->mac_extensions_size);
	kfree_data(data, (vm_size_t)datalen);
	return error;
}
#endif /* CONFIG_MACF */

#if CONFIG_COALITIONS
static inline void
spawn_coalitions_release_all(coalition_t coal[COALITION_NUM_TYPES])
{
	for (int c = 0; c < COALITION_NUM_TYPES; c++) {
		if (coal[c]) {
			coalition_remove_active(coal[c]);
			coalition_release(coal[c]);
		}
	}
}
#endif

#if CONFIG_PERSONAS
static int
spawn_validate_persona(struct _posix_spawn_persona_info *px_persona)
{
	int error = 0;
	struct persona *persona = NULL;
	kauth_cred_t mycred = kauth_cred_get();

	if (!IOCurrentTaskHasEntitlement( PERSONA_MGMT_ENTITLEMENT)) {
		return EPERM;
	}

	if (px_persona->pspi_flags & POSIX_SPAWN_PERSONA_GROUPS) {
		if (px_persona->pspi_ngroups > NGROUPS_MAX) {
			return EINVAL;
		}
	}

	persona = persona_lookup(px_persona->pspi_id);
	if (!persona) {
		return ESRCH;
	}

	// non-root process should not be allowed to set persona with uid/gid 0
	if (!kauth_cred_issuser(mycred) &&
	    (px_persona->pspi_uid == 0 || px_persona->pspi_gid == 0)) {
		return EPERM;
	}

	persona_put(persona);
	return error;
}

static bool
kauth_cred_model_setpersona(
	kauth_cred_t            model,
	struct _posix_spawn_persona_info *px_persona)
{
	bool updated = false;

	if (px_persona->pspi_flags & POSIX_SPAWN_PERSONA_UID) {
		updated |= kauth_cred_model_setresuid(model,
		    px_persona->pspi_uid,
		    px_persona->pspi_uid,
		    px_persona->pspi_uid,
		    KAUTH_UID_NONE);
	}

	if (px_persona->pspi_flags & POSIX_SPAWN_PERSONA_GID) {
		updated |= kauth_cred_model_setresgid(model,
		    px_persona->pspi_gid,
		    px_persona->pspi_gid,
		    px_persona->pspi_gid);
	}

	if (px_persona->pspi_flags & POSIX_SPAWN_PERSONA_GROUPS) {
		updated |= kauth_cred_model_setgroups(model,
		    px_persona->pspi_groups,
		    px_persona->pspi_ngroups,
		    px_persona->pspi_gmuid);
	}

	return updated;
}

static int
spawn_persona_adopt(proc_t p, struct _posix_spawn_persona_info *px_persona)
{
	struct persona *persona = NULL;

	/*
	 * we want to spawn into the given persona, but we want to override
	 * the kauth with a different UID/GID combo
	 */
	persona = persona_lookup(px_persona->pspi_id);
	if (!persona) {
		return ESRCH;
	}

	return persona_proc_adopt(p, persona,
	           ^bool (kauth_cred_t parent __unused, kauth_cred_t model) {
		return kauth_cred_model_setpersona(model, px_persona);
	});
}
#endif

#if __arm64__
#if DEVELOPMENT || DEBUG
TUNABLE(int, legacy_footprint_entitlement_mode, "legacy_footprint_entitlement_mode",
    LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE);

__startup_func
static void
legacy_footprint_entitlement_mode_init(void)
{
	/*
	 * legacy_footprint_entitlement_mode specifies the behavior we want associated
	 * with the entitlement. The supported modes are:
	 *
	 * LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE:
	 *	Indicates that we want every process to have the memory accounting
	 *	that is available in iOS 12.0 and beyond.
	 *
	 * LEGACY_FOOTPRINT_ENTITLEMENT_IOS11_ACCT:
	 *	Indicates that for every process that has the 'legacy footprint entitlement',
	 *      we want to give it the old iOS 11.0 accounting behavior which accounted some
	 *	of the process's memory to the kernel.
	 *
	 * LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE:
	 *      Indicates that for every process that has the 'legacy footprint entitlement',
	 *	we want it to have a higher memory limit which will help them acclimate to the
	 *	iOS 12.0 (& beyond) accounting behavior that does the right accounting.
	 *      The bonus added to the system-wide task limit to calculate this higher memory limit
	 *      is available in legacy_footprint_bonus_mb.
	 */

	if (legacy_footprint_entitlement_mode < LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE ||
	    legacy_footprint_entitlement_mode > LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE) {
		legacy_footprint_entitlement_mode = LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE;
	}
}
STARTUP(TUNABLES, STARTUP_RANK_MIDDLE, legacy_footprint_entitlement_mode_init);
#else
const int legacy_footprint_entitlement_mode = LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE;
#endif

static inline void
proc_legacy_footprint_entitled(proc_t p, task_t task)
{
#pragma unused(p)
	boolean_t legacy_footprint_entitled;

	switch (legacy_footprint_entitlement_mode) {
	case LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE:
		/* the entitlement is ignored */
		break;
	case LEGACY_FOOTPRINT_ENTITLEMENT_IOS11_ACCT:
		/* the entitlement grants iOS11 legacy accounting */
		legacy_footprint_entitled = memorystatus_task_has_legacy_footprint_entitlement(proc_task(p));
		if (legacy_footprint_entitled) {
			task_set_legacy_footprint(task);
		}
		break;
	case LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE:
		/* the entitlement grants a footprint limit increase */
		legacy_footprint_entitled = memorystatus_task_has_legacy_footprint_entitlement(proc_task(p));
		if (legacy_footprint_entitled) {
			task_set_extra_footprint_limit(task);
		}
		break;
	default:
		break;
	}
}

static inline void
proc_ios13extended_footprint_entitled(proc_t p, task_t task)
{
#pragma unused(p)
	boolean_t ios13extended_footprint_entitled;

	/* the entitlement grants a footprint limit increase */
	ios13extended_footprint_entitled = memorystatus_task_has_ios13extended_footprint_limit(proc_task(p));
	if (ios13extended_footprint_entitled) {
		task_set_ios13extended_footprint_limit(task);
	}
}

static inline void
proc_increased_memory_limit_entitled(proc_t p, task_t task)
{
	bool entitled = memorystatus_task_has_increased_memory_limit_entitlement(task);

	if (entitled) {
		memorystatus_act_on_entitled_task_limit(p);
	}
}

/*
 * Check for any of the various entitlements that permit a higher
 * task footprint limit or alternate accounting and apply them.
 */
static inline void
proc_footprint_entitlement_hacks(proc_t p, task_t task)
{
	proc_legacy_footprint_entitled(p, task);
	proc_ios13extended_footprint_entitled(p, task);
	proc_increased_memory_limit_entitled(p, task);
}
#endif /* __arm64__ */

/*
 * Processes with certain entitlements are granted a jumbo-size VM map.
 */
static inline void
proc_apply_jit_and_vm_policies(struct image_params *imgp, proc_t p, task_t task)
{
#if CONFIG_MACF
	bool jit_entitled = false;
#endif /* CONFIG_MACF */
	bool needs_jumbo_va = false;
	struct _posix_spawnattr *psa = imgp->ip_px_sa;

#if CONFIG_MACF
	jit_entitled = (mac_proc_check_map_anon(p, proc_ucred_unsafe(p),
	    0, 0, 0, MAP_JIT, NULL) == 0);
	needs_jumbo_va = jit_entitled || IOTaskHasEntitlement(task,
	    "com.apple.developer.kernel.extended-virtual-addressing") ||
	    memorystatus_task_has_increased_memory_limit_entitlement(task);
#else
#pragma unused(p)
#endif /* CONFIG_MACF */


	if (needs_jumbo_va) {
		vm_map_set_jumbo(get_task_map(task));
	}

	if (psa && psa->psa_max_addr) {
		vm_map_set_max_addr(get_task_map(task), psa->psa_max_addr);
	}

#if CONFIG_MAP_RANGES
	if (task_is_hardened_binary(task) && !proc_is_simulated(p)) {
		/*
		 * This must be done last as it needs to observe
		 * any kind of VA space growth that was requested.
		 * This is used by the secure allocator, so
		 * must be applied to all hardened binaries
		 */
		vm_map_range_configure(get_task_map(task));
	}
#endif /* CONFIG_MAP_RANGES */

#if CONFIG_MACF
	if (jit_entitled) {
		vm_map_set_jit_entitled(get_task_map(task));

	}
#endif /* CONFIG_MACF */

#if XNU_TARGET_OS_OSX
	/* TPRO cannot be enforced on binaries that load 3P plugins on macos - rdar://107420220 */
	const bool task_loads_3P_plugins = imgp->ip_flags & IMGPF_3P_PLUGINS;
#endif /* XNU_TARGET_OS_OSX */

	if (task_is_hardened_binary(task)
#if XNU_TARGET_OS_OSX
	    && !task_loads_3P_plugins
#endif /* XNU_TARGET_OS_OSX */
	    ) {
		/*
		 * Pre-emptively disable TPRO remapping for
		 * hardened binaries (which do not load 3P plugins)
		 */
		vm_map_set_tpro_enforcement(get_task_map(task));
	}
}

static int
spawn_posix_cred_adopt(proc_t p,
    struct _posix_spawn_posix_cred_info *px_pcred_info)
{
	int error = 0;

	if (px_pcred_info->pspci_flags & POSIX_SPAWN_POSIX_CRED_GID) {
		struct setgid_args args = {
			.gid = px_pcred_info->pspci_gid,
		};
		error = setgid(p, &args, NULL);
		if (error) {
			return error;
		}
	}

	if (px_pcred_info->pspci_flags & POSIX_SPAWN_POSIX_CRED_GROUPS) {
		error = setgroups_internal(p,
		    px_pcred_info->pspci_ngroups,
		    px_pcred_info->pspci_groups,
		    px_pcred_info->pspci_gmuid);
		if (error) {
			return error;
		}
	}

	if (px_pcred_info->pspci_flags & POSIX_SPAWN_POSIX_CRED_UID) {
		struct setuid_args args = {
			.uid = px_pcred_info->pspci_uid,
		};
		error = setuid(p, &args, NULL);
		if (error) {
			return error;
		}
	}
	return 0;
}

/*
 * posix_spawn
 *
 * Parameters:	uap->pid		Pointer to pid return area
 *		uap->fname		File name to exec
 *		uap->argp		Argument list
 *		uap->envp		Environment list
 *
 * Returns:	0			Success
 *		EINVAL			Invalid argument
 *		ENOTSUP			Not supported
 *		ENOEXEC			Executable file format error
 *	exec_activate_image:EINVAL	Invalid argument
 *	exec_activate_image:EACCES	Permission denied
 *	exec_activate_image:EINTR	Interrupted function
 *	exec_activate_image:ENOMEM	Not enough space
 *	exec_activate_image:EFAULT	Bad address
 *	exec_activate_image:ENAMETOOLONG	Filename too long
 *	exec_activate_image:ENOEXEC	Executable file format error
 *	exec_activate_image:ETXTBSY	Text file busy [misuse of error code]
 *	exec_activate_image:EAUTH	Image decryption failed
 *	exec_activate_image:EBADEXEC	The executable is corrupt/unknown
 *	exec_activate_image:???
 *	mac_execve_enter:???
 *
 * TODO:	Expect to need __mac_posix_spawn() at some point...
 *		Handle posix_spawnattr_t
 *		Handle posix_spawn_file_actions_t
 */
#if defined(XNU_OWNER_CACHE_MOUNT) && XNU_OWNER_CACHE_MOUNT
/* Bounded successful-exec registry. Each occupied slot owns one thread ref;
 * that also pins thread/task teardown resources, so cap BOTH slots and time.
 * No proc/task pointers are cached. Replacement and expiry put outside lock.
 * The window starts at first successful exec and never rearms this boot. */
#define MEIZU_EXEC_SLOTS 128
#define MEIZU_EXEC_CAPTURES 256
static LCK_MTX_DECLARE_ATTR(meizu_exec_lock, &proc_lck_grp, &proc_lck_attr);
struct meizu_exec_entry {
	thread_t thread;
	uint64_t tid, taskgen;
	int pid;
	char name[256]; /* full vnode component (NAME_MAX bytes plus NUL) */
	char path[MAXPATHLEN];
	int path_error;
	bool container;
};
static struct meizu_exec_entry meizu_exec_registry[MEIZU_EXEC_SLOTS];
static uint64_t meizu_exec_started;
static unsigned meizu_exec_captures, meizu_exec_next;
extern uint64_t meizu_thread_task_generation(thread_t);
static bool meizu_exec_closed;
extern void meizu_lsd_ipc_capture(uint64_t, int);
extern void meizu_lsd_ulock_capture(uint64_t);
extern void meizu_lsd_ipc_snapshot(uint64_t, unsigned);
extern unsigned meizu_container_ipc_capture(uint64_t, int);
extern void meizu_container_ulock_capture(unsigned, uint64_t);
extern void meizu_container_ipc_snapshot(uint64_t, unsigned);
extern bool meizu_lsd_ipc_dependency(uint64_t, unsigned, int *, uint64_t *, uint64_t *, uint64_t *);

/* Called only after the bounded lsd IPC snapshot has released its spin lock.
 * A borrowed registry thread reference pins the task and its recount storage
 * through thread_deallocate_complete(). No registry/task/scheduler lock is
 * held across recount's seqlock retries or printing. These are already charged
 * lifetime totals: running threads' uncharged tails are not forced current.
 * IPC and CPU samples are not atomic together; summed task CPU can exceed wall
 * time. The caller supplies the existing 31-dump/300-second boot budget. */
void nova_lsd_cpu_timing_dump(uint64_t, uint64_t, unsigned);
void
nova_lsd_cpu_timing_dump(uint64_t generation, uint64_t ipc_ticks, unsigned dump)
{
	thread_t anchor = THREAD_NULL;
	uint64_t tid = 0, taskgen = 0, taskgen_after = 0;
	uint64_t begin = 0, end = 0;
	int pid = -1, active_before = -1, active_after = -1;
	bool valid = false;
	const char *status = "skip-no-anchor";
	struct recount_times_mach times = { 0 };
#if PRECISE_USER_KERNEL_TIME
	const unsigned precise = 1;
#else
	const unsigned precise = 0;
#endif
	if (!generation) {
		status = "skip-no-generation";
		goto report;
	}
	if (!lck_mtx_try_lock(&meizu_exec_lock)) {
		status = "skip-registry-busy";
		goto report;
	}
	if (meizu_exec_closed) {
		status = "skip-registry-closed";
	} else {
		for (unsigned i = 0; i < MEIZU_EXEC_SLOTS; i++) {
			struct meizu_exec_entry *e = &meizu_exec_registry[i];
			if (e->thread && e->taskgen == generation && strcmp(e->name, "lsd") == 0) {
				anchor = e->thread;
				tid = e->tid;
				pid = e->pid;
				thread_reference(anchor);
				break;
			}
		}
	}
	lck_mtx_unlock(&meizu_exec_lock);
	if (!anchor) goto report;
	/* A corpse conversion may change the identity while the ref stays valid. */
	taskgen = meizu_thread_task_generation(anchor);
	if (taskgen != generation) {
		status = "failure-generation-before";
		goto release;
	}
	task_t task = get_threadtask(anchor);
	active_before = task_is_active(task);
	begin = mach_absolute_time();
	struct recount_usage usage = { 0 };
	recount_task_usage(task, &usage);
	times = recount_usage_times_mach(&usage);
	end = mach_absolute_time();
	active_after = task_is_active(task);
	taskgen_after = meizu_thread_task_generation(anchor);
	if (taskgen_after != generation) {
		status = "failure-generation-after";
		goto release;
	}
	valid = true;
	status = "matched";
release:
	thread_deallocate(anchor);
report:
	/* Unavailable/invalid samples omit counters: zero is a valid CPU total. */
	if (valid) {
		printf("nova: lsdcpu gen=%llu taskgen=%llu taskgen_after=%llu dump=%u ipc_ticks=%llu begin_ticks=%llu end_ticks=%llu pid=%d tid=%llu status=%s valid=1 active_before=%d active_after=%d user_ticks=%llu system_ticks=%llu precise=%u clock=mach_absolute charged_only=1 non_atomic_with_ipc=1\n",
		    (unsigned long long)generation, (unsigned long long)taskgen,
		    (unsigned long long)taskgen_after, dump, (unsigned long long)ipc_ticks,
		    (unsigned long long)begin, (unsigned long long)end, pid,
		    (unsigned long long)tid, status, active_before, active_after,
		    (unsigned long long)times.rtm_user, (unsigned long long)times.rtm_system, precise);
	} else {
		printf("nova: lsdcpu gen=%llu taskgen=%llu taskgen_after=%llu dump=%u ipc_ticks=%llu begin_ticks=%llu end_ticks=%llu pid=%d tid=%llu status=%s valid=0 active_before=%d active_after=%d precise=%u clock=mach_absolute\n",
		    (unsigned long long)generation, (unsigned long long)taskgen,
		    (unsigned long long)taskgen_after, dump, (unsigned long long)ipc_ticks,
		    (unsigned long long)begin, (unsigned long long)end, pid,
		    (unsigned long long)tid, status, active_before, active_after, precise);
	}
}

/* Setup has its own boot budget and exec-relative deadline. Do not put this
 * behind the early exec registry's 300-second gate. One owned thread reference
 * pins the task; sampling borrows another while the registry lock is dropped. */
#define MEIZU_SETUP_ATTEMPTS 3
static LCK_MTX_DECLARE_ATTR(meizu_setup_lock, &proc_lck_grp, &proc_lck_attr);
static thread_t meizu_setup_thread;
static uint64_t meizu_setup_generation, meizu_setup_tid, meizu_setup_started;
/* Nova R8: a boot-wide delayed capture anchor. The normal Setup sampler
 * retires its reference after 20 seconds; this scalar is never a live task. */
static uint64_t nova_capture_setup_first;
static int meizu_setup_pid;
static unsigned meizu_setup_attempts, meizu_setup_round;
extern void meizu_setup_ipc_capture(uint64_t, uint64_t, int);
extern void meizu_setup_bt_capture(uint64_t, uint64_t, uint64_t, unsigned);
extern void meizu_setup_bt_retire(uint64_t);
extern void meizu_setup_ipc_retire(uint64_t);
extern void meizu_setup_ipc_snapshot(uint64_t, unsigned);
extern void meizu_setup_sema_capture(uint64_t, uint64_t, uint64_t, unsigned);
extern void meizu_setup_sema_retire(uint64_t);
extern void meizu_setup_sema_drain(void);
extern void meizu_setup_worker_lock_drain(void);
extern int meizu_sb_thread_snapshot(thread_t, int, uint64_t, bool, uint64_t, uint64_t);

static void
meizu_setup_capture(proc_t p, thread_t thread)
{
	/* p_comm is only a cheap rejection filter; the committed vnode path below
	 * is the identity check. The mounted image has this exact executable. */
	if (strcmp(p->p_comm, "Setup") != 0) return;
	lck_mtx_lock(&meizu_setup_lock);
	bool exhausted = meizu_setup_attempts >= MEIZU_SETUP_ATTEMPTS && !meizu_setup_thread;
	lck_mtx_unlock(&meizu_setup_lock);
	if (exhausted) return;
	char path[MAXPATHLEN];
	int size = sizeof(path), error = ENOENT;
	vnode_t vp = proc_getexecutablevnode(p);
	if (vp != NULLVP) {
		error = vn_getpath(vp, path, &size);
		vnode_put(vp);
	}
	if (error || strcmp(path, "/Applications/Setup.app/Setup") != 0) return;
	uint64_t gen = meizu_thread_task_generation(thread), tid = thread_tid(thread);
	if (!gen) return;
	int pid = proc_getpid(p);
	uint64_t capture_ticks = 0;
	thread_t old;
	lck_mtx_lock(&meizu_setup_lock);
	if (meizu_setup_thread == thread && meizu_setup_generation == gen) {
		lck_mtx_unlock(&meizu_setup_lock);
		return;
	}
	old = meizu_setup_thread;
	if (old) {
		meizu_setup_ipc_retire(meizu_setup_generation);
		meizu_setup_bt_retire(meizu_setup_generation);
		meizu_setup_sema_retire(meizu_setup_generation);
	}
	meizu_setup_thread = THREAD_NULL;
	bool armed = meizu_setup_attempts < MEIZU_SETUP_ATTEMPTS;
	if (armed) {
		thread_reference(thread); /* successful-exec caller still owns its ref */
		meizu_setup_thread = thread;
		meizu_setup_generation = gen;
		meizu_setup_tid = tid;
		meizu_setup_pid = pid;
		meizu_setup_started = mach_absolute_time();
		if (!nova_capture_setup_first) nova_capture_setup_first = meizu_setup_started;
		capture_ticks = meizu_setup_started;
		meizu_setup_round = 0;
		meizu_setup_attempts++;
		/* Serialized with retirement/replacement; never an IPC->Setup lock edge. */
		meizu_setup_ipc_capture(gen, tid, pid);
		meizu_setup_bt_capture(gen, tid, meizu_setup_started, meizu_setup_attempts);
		meizu_setup_sema_capture(gen, tid, meizu_setup_started, meizu_setup_attempts);
	}
	unsigned attempt = meizu_setup_attempts;
	lck_mtx_unlock(&meizu_setup_lock);
	if (old) thread_deallocate(old);
	uint64_t capture_ns = 0;
	absolutetime_to_nanoseconds(capture_ticks, &capture_ns);
	printf("meizu: setup-arm pid=%d taskgen=%llu tid=%llu attempt=%u status=%s path=%s clock=mach_absolute capture_ticks=%llu capture_ns=%llu\n",
	    pid, (unsigned long long)gen, (unsigned long long)tid, attempt,
	    armed ? "armed" : "boot-cap-reached", path,
	    (unsigned long long)capture_ticks, (unsigned long long)capture_ns);
}

/* MEIZU_BUDD_PROBE_BEGIN
 * One owned exact-exec anchor, at most three generations this boot. Keep it
 * across delayed Setup launches; autolog releases on exit/generation change or
 * after the third Setup window. No timeout tied to budd startup. If Setup never
 * starts, one live anchor can remain until exit/reboot. Rounds never reset on
 * budd replacement: at most three per immutable Setup semaphore slot (nine).
 * No calls from semaphore/scheduler paths; those only publish scalar state.
 */
static LCK_MTX_DECLARE_ATTR(meizu_budd_lock, &proc_lck_grp, &proc_lck_attr);
static thread_t meizu_budd_thread;
static uint64_t meizu_budd_gen, meizu_budd_started;
static unsigned meizu_budd_attempts, meizu_budd_round[3];
static int meizu_budd_pid;
extern void meizu_budd_states(thread_t, int, uint64_t, unsigned);
extern void meizu_setup_budd_budd(uint64_t, uint64_t);
extern void meizu_setup_budd_drain(void);
extern int meizu_budd_anchor_live(thread_t, uint64_t);
extern bool meizu_setup_sema_window(unsigned, uint64_t *, uint64_t *);
extern bool meizu_setup_sema_finished(void);

static void
meizu_budd_capture(proc_t p, thread_t t)
{
	uint64_t gen = meizu_thread_task_generation(t);
	int pid = proc_getpid(p);
	thread_t old = THREAD_NULL;
	/* An exec of a different executable in the tracked process invalidates the
	 * old anchor too. Run before the name filter and even after budget expiry. */
	lck_mtx_lock(&meizu_budd_lock);
	if (meizu_budd_thread && meizu_budd_pid == pid &&
	    (meizu_budd_thread != t || meizu_budd_gen != gen)) {
		old = meizu_budd_thread;
		meizu_setup_budd_budd(meizu_budd_gen, 0);
		meizu_budd_thread = THREAD_NULL;
	}
	bool capped = meizu_budd_attempts >= 3;
	lck_mtx_unlock(&meizu_budd_lock);
	if (old) thread_deallocate(old);
	if (capped || !gen || strcmp(p->p_comm, "budd") != 0 ||
	    meizu_setup_sema_finished()) return;
	char path[MAXPATHLEN];
	int size = sizeof(path), error = ENOENT;
	vnode_t vp = proc_getexecutablevnode(p);
	if (vp != NULLVP) {
		error = vn_getpath(vp, path, &size);
		vnode_put(vp);
	}
	if (error || strcmp(path, "/System/Library/PrivateFrameworks/SetupAssistant.framework/budd")) return;
	old = THREAD_NULL;
	lck_mtx_lock(&meizu_budd_lock);
	if (meizu_budd_attempts < 3 && gen != meizu_budd_gen) {
		old = meizu_budd_thread;
		thread_reference(t);
		meizu_budd_thread = t;
		meizu_budd_gen = gen;
		meizu_budd_pid = pid;
		meizu_budd_started = mach_absolute_time();
		meizu_budd_attempts++;
		meizu_setup_budd_budd(0, gen);
	}
	lck_mtx_unlock(&meizu_budd_lock);
	if (old) thread_deallocate(old);
}

static void
meizu_sample_budd(void)
{
	static const unsigned seconds[] = { 0, 2, 8 };
	if (!lck_mtx_try_lock(&meizu_budd_lock)) return;
	thread_t t = meizu_budd_thread;
	if (!t) { lck_mtx_unlock(&meizu_budd_lock); return; }
	uint64_t gen = meizu_budd_gen, started = meizu_budd_started;
	int pid = meizu_budd_pid;
	thread_reference(t); /* borrow before dropping mutex, including health check */
	lck_mtx_unlock(&meizu_budd_lock);
	int live = meizu_budd_anchor_live(t, gen);
	bool finished = meizu_setup_sema_finished();
	if (live == 0 || finished) {
		thread_t old = THREAD_NULL;
		lck_mtx_lock(&meizu_budd_lock);
		if (meizu_budd_thread == t && meizu_budd_gen == gen) {
			old = meizu_budd_thread;
			meizu_setup_budd_budd(gen, 0);
			meizu_budd_thread = THREAD_NULL;
		}
		lck_mtx_unlock(&meizu_budd_lock);
		if (old) thread_deallocate(old);
	} else if (live > 0) {
		for (unsigned slot = 0; slot < 3; slot++) {
			uint64_t setup_gen, selected_age;
			if (!meizu_setup_sema_window(slot, &setup_gen, &selected_age)) continue;
			lck_mtx_lock(&meizu_budd_lock);
			unsigned round = meizu_budd_round[slot];
			bool sample = meizu_budd_thread == t && meizu_budd_gen == gen &&
			    round < 3 && selected_age >= seconds[round] * (uint64_t)NSEC_PER_SEC;
			if (sample) meizu_budd_round[slot]++;
			lck_mtx_unlock(&meizu_budd_lock);
			if (!sample) continue;
			/* Recheck admission after reservation. A racing retirement may consume
			 * a round but cannot cause a sample for a recycled Setup slot. */
			if (!meizu_setup_sema_window(slot, &setup_gen, &selected_age)) continue;
			uint64_t exec_age;
			absolutetime_to_nanoseconds(mach_absolute_time() - started, &exec_age);
			printf("meizu: budd-window object=%u setup_gen=%llu taskgen=%llu round=%u selected_age_ms=%llu exec_age_ms=%llu base=0 base_valid=0\n",
			    slot + 1, (unsigned long long)setup_gen, (unsigned long long)gen,
			    round + 1, (unsigned long long)(selected_age / NSEC_PER_MSEC),
			    (unsigned long long)(exec_age / NSEC_PER_MSEC));
			meizu_budd_states(t, pid, gen, round + 1);
		}
	}
	thread_deallocate(t);
}
/* MEIZU_BUDD_PROBE_END */

/* MEIZU_SETUP_SERVICE_SNAPSHOTS_BEGIN
 * Autolog only: immutable semaphore slots retain their two-round quota.
 * Borrow the exact Setup anchor under its registry mutex, then drop it before
 * taking task/thread locks. A late tick consumes one due round per call.
 */
extern void meizu_setup_worker_states(thread_t, int, uint64_t, unsigned);
static void
meizu_sample_setup_workers(void)
{
	static unsigned rounds[MEIZU_SETUP_ATTEMPTS];
	static const unsigned seconds[] = { 2, 8 };
	for (unsigned slot = 0; slot < MEIZU_SETUP_ATTEMPTS; slot++) {
		uint64_t gen, age;
		if (!meizu_setup_sema_window(slot, &gen, &age)) continue;
		if (!lck_mtx_try_lock(&meizu_setup_lock)) continue;
		thread_t t = meizu_setup_thread;
		unsigned round = rounds[slot];
		bool sample = t && meizu_setup_generation == gen && round < 2 &&
		    age >= seconds[round] * (uint64_t)NSEC_PER_SEC;
		int pid = meizu_setup_pid;
		if (sample) { rounds[slot]++; thread_reference(t); }
		lck_mtx_unlock(&meizu_setup_lock);
		if (!sample) continue;
		uint64_t checked;
		if (meizu_setup_sema_window(slot, &checked, &age) && checked == gen &&
		    meizu_budd_anchor_live(t, gen) > 0) {
			printf("meizu: setup-worker-window object=%u taskgen=%llu round=%u selected_age_ms=%llu\n",
			    slot + 1, (unsigned long long)gen, round + 1,
			    (unsigned long long)(age / NSEC_PER_MSEC));
			meizu_setup_worker_states(t, pid, gen, round + 1);
		}
		thread_deallocate(t);
	}
}
/* MEIZU_SETUP_SERVICE_SNAPSHOTS_END */

static void
meizu_sample_setup(void)
{
	meizu_setup_worker_lock_drain();
	meizu_setup_budd_drain();
	meizu_setup_sema_drain();
	meizu_sample_budd();
	meizu_sample_setup_workers();
	static const unsigned seconds[] = { 2, 8, 15 };
	thread_t thread;
	uint64_t age, gen, tid;
	unsigned round, attempt;
	int pid;
	if (!lck_mtx_try_lock(&meizu_setup_lock)) return;
	thread = meizu_setup_thread;
	if (!thread) {
		lck_mtx_unlock(&meizu_setup_lock);
		return;
	}
	gen = meizu_setup_generation;
	absolutetime_to_nanoseconds(mach_absolute_time() - meizu_setup_started, &age);
	/* Poll termination on every autolog tick, including between sample rounds.
	 * Thread ref pins both objects even after task_uniqueid changes to corpse. */
	bool changed = meizu_thread_task_generation(thread) != gen;
	bool inactive = !thread_is_active(thread);
	if (changed || inactive || age >= 20ULL * NSEC_PER_SEC) {
		meizu_setup_thread = THREAD_NULL;
		meizu_setup_ipc_retire(gen);
		meizu_setup_bt_retire(gen);
		meizu_setup_sema_retire(gen);
		lck_mtx_unlock(&meizu_setup_lock);
		thread_deallocate(thread);
		printf("meizu: setup-retire taskgen=%llu reason=%s\n",
		    (unsigned long long)gen, changed ? "generation-changed" :
		    inactive ? "inactive" : "deadline");
		return;
	}
	if (meizu_setup_round >= 3 || age < (uint64_t)seconds[meizu_setup_round] * NSEC_PER_SEC) {
		lck_mtx_unlock(&meizu_setup_lock);
		return;
	}
	round = ++meizu_setup_round;
	attempt = meizu_setup_attempts;
	pid = meizu_setup_pid;
	tid = meizu_setup_tid;
	thread_reference(thread);
	lck_mtx_unlock(&meizu_setup_lock);
	printf("meizu: setup-main pid=%d taskgen=%llu tid=%llu attempt=%u round=%u age_ms=%llu\n",
	    pid, (unsigned long long)gen, (unsigned long long)tid, attempt, round,
	    (unsigned long long)(age / NSEC_PER_MSEC));
	/* Reuse the generation-checked, retained-thread snapshot. Its following
	 * receiver-worker line has wait state and safe kernel-saved user PC/LR/SP.
	 * It never walks user memory and only reads regs for a quiescent waiter. */
	meizu_sb_thread_snapshot(thread, pid, tid, true, gen, 0);
	meizu_setup_ipc_snapshot(gen, round);
	thread_deallocate(thread);
}

static void
meizu_exec_capture(proc_t p, thread_t thread)
{
	/* Successful-exec capture point, before registry/path work; not readiness. */
	uint64_t capture_ticks = mach_absolute_time(), capture_ns = 0;
	meizu_setup_capture(p, thread);
	meizu_budd_capture(p, thread);
	struct meizu_exec_entry e = { .thread = thread, .pid = proc_getpid(p),
		.tid = thread_tid(thread), .taskgen = meizu_thread_task_generation(thread) };
	thread_t old = THREAD_NULL;
	uint64_t age;
	/* Bound identity lookups too, including exec churn after registry expiry. */
	lck_mtx_lock(&meizu_exec_lock);
	if (!meizu_exec_started) meizu_exec_started = mach_absolute_time();
	absolutetime_to_nanoseconds(mach_absolute_time() - meizu_exec_started, &age);
	bool stopped = meizu_exec_closed || age >= 300ULL * NSEC_PER_SEC ||
	    meizu_exec_captures >= MEIZU_EXEC_CAPTURES;
	lck_mtx_unlock(&meizu_exec_lock);
	if (stopped) return;
	/* Both successful-exec callers run before releasing the new task's return
	 * wait. ip_vp/ip_strings have already been released there. Take our own
	 * iocount on the committed text vnode; no probe/IPC/task locks are held. */
	vnode_t vp = proc_getexecutablevnode(p);
	e.path_error = ENOENT;
	if (vp != NULLVP) {
		const char *name = vnode_getname(vp);
		if (name) {
			strlcpy(e.name, name, sizeof(e.name));
			vnode_putname(name);
		}
		int size = sizeof(e.path);
		e.path_error = vn_getpath(vp, e.path, &size);
		if (e.path_error) e.path[0] = '\0';
		vnode_put(vp);
	}
	/* Full vnode component, never truncated p_comm or an ambiguous prefix. */
	e.container = strcmp(e.name, "containermanagerd") == 0 ||
	    strcmp(e.name, "containermanagerd_system") == 0;
	/* Successful exec only; independent of registry quota and PID reuse. */
	if (strcmp(e.name, "lsd") == 0) {
		meizu_lsd_ipc_capture(e.taskgen, e.pid);
		meizu_lsd_ulock_capture(e.taskgen);
	}
	lck_mtx_lock(&meizu_exec_lock);
	if (!meizu_exec_started) {
		meizu_exec_started = mach_absolute_time();
	}
	absolutetime_to_nanoseconds(mach_absolute_time() - meizu_exec_started, &age);
	if (meizu_exec_closed || age >= 300ULL * NSEC_PER_SEC ||
	    meizu_exec_captures >= MEIZU_EXEC_CAPTURES) {
		lck_mtx_unlock(&meizu_exec_lock);
		return;
	}
	unsigned slot = MEIZU_EXEC_SLOTS;
	for (unsigned i = 0; i < MEIZU_EXEC_SLOTS; i++) {
		if (meizu_exec_registry[i].thread && meizu_exec_registry[i].pid == e.pid) {
			slot = i;
			break;
		}
	}
	if (slot == MEIZU_EXEC_SLOTS) {
		slot = meizu_exec_next++ % MEIZU_EXEC_SLOTS;
	}
	thread_reference(thread); /* caller's successful-exec ref is still live */
	old = meizu_exec_registry[slot].thread;
	meizu_exec_registry[slot] = e;
	meizu_exec_captures++;
	bool capped = meizu_exec_captures == MEIZU_EXEC_CAPTURES;
	lck_mtx_unlock(&meizu_exec_lock);
	if (old) {
		thread_deallocate(old);
	}
	if (e.container) {
		unsigned target = meizu_container_ipc_capture(e.taskgen, e.pid);
		meizu_container_ulock_capture(target, e.taskgen);
		printf("meizu: container-capture pid=%d taskgen=%llu slot=%u status=%s\n",
		    e.pid, (unsigned long long)e.taskgen, target,
		    target < 4 ? "armed" : "boot-cap-reached");
	}
	absolutetime_to_nanoseconds(capture_ticks, &capture_ns);
	printf("meizu: exec-map pid=%d taskgen=%llu tid=%llu name=%s path=%s path_error=%d clock=mach_absolute capture_ticks=%llu capture_ns=%llu\n",
	    e.pid, (unsigned long long)e.taskgen, (unsigned long long)e.tid,
	    e.name[0] ? e.name : "?", e.path_error ? "?" : e.path, e.path_error,
	    (unsigned long long)capture_ticks, (unsigned long long)capture_ns);
	if (capped) {
		printf("meizu: exec-map capture-limit=%u slots=%u\n",
		    (unsigned)MEIZU_EXEC_CAPTURES, (unsigned)MEIZU_EXEC_SLOTS);
	}
}

/* One owned reference, replaced at successful SpringBoard exec completion. */
static LCK_MTX_DECLARE_ATTR(meizu_sb_lock, &proc_lck_grp, &proc_lck_attr);
static thread_t meizu_sb_thread = THREAD_NULL;
static pid_t meizu_sb_pid;
static uint64_t meizu_sb_tid, meizu_sb_generation;
static uint64_t meizu_sb_started, meizu_sb_last_sample;
static unsigned int meizu_sb_samples;
extern void meizu_sb_ipc_capture(uint64_t tid, int pid, uint64_t generation);
extern void meizu_sb_worker_ipc_snapshot(uint64_t, unsigned, uint64_t);
extern uint64_t meizu_sb_ulock_owner(uint64_t);
extern void meizu_sb_ipc_drain(void);
extern void nova_lsd_ipc_timing_dump(void);
extern void nova_lsd_bt_timing_dump(void);
extern void meizu_sb_ulock_capture(uint64_t, uint64_t);
extern void meizu_sb_ulock_snapshot(uint64_t, unsigned);

static void
meizu_sb_capture(proc_t p, thread_t thread)
{
	thread_t old;
	thread_reference(thread);
	lck_mtx_lock(&meizu_sb_lock);
	old = meizu_sb_thread;
	meizu_sb_thread = thread;
	meizu_sb_pid = proc_getpid(p);
	meizu_sb_tid = thread_tid(thread);
	meizu_sb_generation++;
	meizu_sb_started = mach_absolute_time();
	meizu_sb_last_sample = 0;
	meizu_sb_samples = 0;
	meizu_sb_ipc_capture(meizu_sb_tid, meizu_sb_pid, meizu_thread_task_generation(thread));
	meizu_sb_ulock_capture(meizu_thread_task_generation(thread), meizu_sb_tid);
	lck_mtx_unlock(&meizu_sb_lock);
	if (old != THREAD_NULL) {
		thread_deallocate(old);
	}
}
#endif

volatile int meizu_spawn_trace;

int

posix_spawn(proc_t ap, struct posix_spawn_args *uap, int32_t *retval)
{
	proc_t p = ap;
	{
		int slot = meizu_spawn_trace;
		char path_buffer[128];
		size_t path_length = 0;
		if (uap->path &&
		    copyinstr(uap->path, path_buffer, sizeof(path_buffer), &path_length) == 0 &&
		    (slot < 20 || strnstr(path_buffer, "SpringBoard", sizeof(path_buffer)) ||
		    strnstr(path_buffer, "backboardd", sizeof(path_buffer)))) {
			printf("meizu: spawn request parent=%d path=%s\n", proc_getpid(p), path_buffer);
		}
		meizu_spawn_trace = slot + 1;
	}
	user_addr_t pid = uap->pid;
	int ival[2];            /* dummy retval for setpgid() */
	char *subsystem_root_path = NULL;
	struct image_params *imgp = NULL;
	struct vnode_attr *vap = NULL;
	struct vnode_attr *origvap = NULL;
	struct uthread  *uthread = 0;   /* compiler complains if not set to 0*/
	int error, sig;
	int is_64 = IS_64BIT_PROCESS(p);
	struct vfs_context context;
	struct user__posix_spawn_args_desc px_args = {};
	struct _posix_spawnattr px_sa = {};
	_posix_spawn_file_actions_t px_sfap = NULL;
	_posix_spawn_port_actions_t px_spap = NULL;
	struct __kern_sigaction vec;
	boolean_t spawn_no_exec = FALSE;
	boolean_t proc_transit_set = TRUE;
	boolean_t proc_signal_set = TRUE;
	boolean_t exec_done = FALSE;
	os_reason_t exec_failure_reason = NULL;

	struct exec_port_actions port_actions = { };
	vm_size_t px_sa_offset = offsetof(struct _posix_spawnattr, psa_ports);
	task_t old_task = current_task();
	task_t new_task = NULL;
	boolean_t should_release_proc_ref = FALSE;
	void *inherit = NULL;
	uint8_t crash_behavior = 0;
	uint64_t crash_behavior_deadline = 0;
#if CONFIG_EXCLAVES
	char *task_conclave_id = NULL;
#endif
#if CONFIG_PERSONAS
	struct _posix_spawn_persona_info *px_persona = NULL;
#endif
	struct _posix_spawn_posix_cred_info *px_pcred_info = NULL;
	struct {
		struct image_params imgp;
		struct vnode_attr va;
		struct vnode_attr origva;
	} *__spawn_data;

	/*
	 * Allocate a big chunk for locals instead of using stack since these
	 * structures are pretty big.
	 */
	__spawn_data = kalloc_type(typeof(*__spawn_data), Z_WAITOK | Z_ZERO);
	if (__spawn_data == NULL) {
		error = ENOMEM;
		goto bad;
	}
	imgp = &__spawn_data->imgp;
	vap = &__spawn_data->va;
	origvap = &__spawn_data->origva;

	/* Initialize the common data in the image_params structure */
	imgp->ip_user_fname = uap->path;
	imgp->ip_user_argv = uap->argv;
	imgp->ip_user_envv = uap->envp;
	imgp->ip_vattr = vap;
	imgp->ip_origvattr = origvap;
	imgp->ip_vfs_context = &context;
	imgp->ip_flags = (is_64 ? IMGPF_WAS_64BIT_ADDR : IMGPF_NONE);
	imgp->ip_seg = (is_64 ? UIO_USERSPACE64 : UIO_USERSPACE32);
	imgp->ip_mac_return = 0;
	imgp->ip_px_persona = NULL;
	imgp->ip_px_pcred_info = NULL;
	imgp->ip_cs_error = OS_REASON_NULL;
	imgp->ip_simulator_binary = IMGPF_SB_DEFAULT;
	imgp->ip_subsystem_root_path = NULL;
	imgp->ip_inherited_shared_region_id = NULL;
	imgp->ip_inherited_jop_pid = 0;
	uthread_set_exec_data(current_uthread(), imgp);

	if (uap->adesc != USER_ADDR_NULL) {
		if (is_64) {
			error = copyin(uap->adesc, &px_args, sizeof(px_args));
		} else {
			struct user32__posix_spawn_args_desc px_args32;

			error = copyin(uap->adesc, &px_args32, sizeof(px_args32));

			/*
			 * Convert arguments descriptor from external 32 bit
			 * representation to internal 64 bit representation
			 */
			px_args.attr_size = px_args32.attr_size;
			px_args.attrp = CAST_USER_ADDR_T(px_args32.attrp);
			px_args.file_actions_size = px_args32.file_actions_size;
			px_args.file_actions = CAST_USER_ADDR_T(px_args32.file_actions);
			px_args.port_actions_size = px_args32.port_actions_size;
			px_args.port_actions = CAST_USER_ADDR_T(px_args32.port_actions);
			px_args.mac_extensions_size = px_args32.mac_extensions_size;
			px_args.mac_extensions = CAST_USER_ADDR_T(px_args32.mac_extensions);
			px_args.coal_info_size = px_args32.coal_info_size;
			px_args.coal_info = CAST_USER_ADDR_T(px_args32.coal_info);
			px_args.persona_info_size = px_args32.persona_info_size;
			px_args.persona_info = CAST_USER_ADDR_T(px_args32.persona_info);
			px_args.posix_cred_info_size = px_args32.posix_cred_info_size;
			px_args.posix_cred_info = CAST_USER_ADDR_T(px_args32.posix_cred_info);
			px_args.subsystem_root_path_size = px_args32.subsystem_root_path_size;
			px_args.subsystem_root_path = CAST_USER_ADDR_T(px_args32.subsystem_root_path);
			px_args.conclave_id_size = px_args32.conclave_id_size;
			px_args.conclave_id = CAST_USER_ADDR_T(px_args32.conclave_id);
		}
		if (error) {
			goto bad;
		}

		if (px_args.attr_size != 0) {
			/*
			 * We are not copying the port_actions pointer,
			 * because we already have it from px_args.
			 * This is a bit fragile: <rdar://problem/16427422>
			 */

			if ((error = copyin(px_args.attrp, &px_sa, px_sa_offset)) != 0) {
				goto bad;
			}

			imgp->ip_px_sa = &px_sa;
		}
		if (px_args.file_actions_size != 0) {
			/* Limit file_actions to allowed number of open files */
			size_t maxfa_size = PSF_ACTIONS_SIZE(proc_limitgetcur_nofile(p));

			if (px_args.file_actions_size < PSF_ACTIONS_SIZE(1) ||
			    maxfa_size == 0 || px_args.file_actions_size > maxfa_size) {
				error = EINVAL;
				goto bad;
			}

			px_sfap = kalloc_data(px_args.file_actions_size, Z_WAITOK);
			if (px_sfap == NULL) {
				error = ENOMEM;
				goto bad;
			}
			imgp->ip_px_sfa = px_sfap;

			if ((error = copyin(px_args.file_actions, px_sfap,
			    px_args.file_actions_size)) != 0) {
				goto bad;
			}

			/* Verify that the action count matches the struct size */
			size_t psfsize = PSF_ACTIONS_SIZE(px_sfap->psfa_act_count);
			if (psfsize == 0 || psfsize != px_args.file_actions_size) {
				error = EINVAL;
				goto bad;
			}
		}
		if (px_args.port_actions_size != 0) {
			/* Limit port_actions to one page of data */
			if (px_args.port_actions_size < PS_PORT_ACTIONS_SIZE(1) ||
			    px_args.port_actions_size > PAGE_SIZE) {
				error = EINVAL;
				goto bad;
			}

			px_spap = kalloc_data(px_args.port_actions_size, Z_WAITOK);
			if (px_spap == NULL) {
				error = ENOMEM;
				goto bad;
			}
			imgp->ip_px_spa = px_spap;

			if ((error = copyin(px_args.port_actions, px_spap,
			    px_args.port_actions_size)) != 0) {
				goto bad;
			}

			/* Verify that the action count matches the struct size */
			size_t pasize = PS_PORT_ACTIONS_SIZE(px_spap->pspa_count);
			if (pasize == 0 || pasize != px_args.port_actions_size) {
				error = EINVAL;
				goto bad;
			}
		}
#if CONFIG_PERSONAS
		/* copy in the persona info */
		if (px_args.persona_info_size != 0 && px_args.persona_info != 0) {
			/* for now, we need the exact same struct in user space */
			if (px_args.persona_info_size != sizeof(*px_persona)) {
				error = ERANGE;
				goto bad;
			}

			px_persona = kalloc_data(px_args.persona_info_size, Z_WAITOK);
			if (px_persona == NULL) {
				error = ENOMEM;
				goto bad;
			}
			imgp->ip_px_persona = px_persona;

			if ((error = copyin(px_args.persona_info, px_persona,
			    px_args.persona_info_size)) != 0) {
				goto bad;
			}
			if ((error = spawn_validate_persona(px_persona)) != 0) {
				goto bad;
			}
		}
#endif
		/* copy in the posix cred info */
		if (px_args.posix_cred_info_size != 0 && px_args.posix_cred_info != 0) {
			/* for now, we need the exact same struct in user space */
			if (px_args.posix_cred_info_size != sizeof(*px_pcred_info)) {
				error = ERANGE;
				goto bad;
			}

			if (!kauth_cred_issuser(kauth_cred_get())) {
				error = EPERM;
				goto bad;
			}

			px_pcred_info = kalloc_data(px_args.posix_cred_info_size, Z_WAITOK);
			if (px_pcred_info == NULL) {
				error = ENOMEM;
				goto bad;
			}
			imgp->ip_px_pcred_info = px_pcred_info;

			if ((error = copyin(px_args.posix_cred_info, px_pcred_info,
			    px_args.posix_cred_info_size)) != 0) {
				goto bad;
			}

			if (px_pcred_info->pspci_flags & POSIX_SPAWN_POSIX_CRED_GROUPS) {
				if (px_pcred_info->pspci_ngroups > NGROUPS_MAX) {
					error = EINVAL;
					goto bad;
				}
			}
		}
#if CONFIG_MACF
		if (px_args.mac_extensions_size != 0) {
			if ((error = spawn_copyin_macpolicyinfo(&px_args, (struct ip_px_smpx_s *)&imgp->ip_px_smpx)) != 0) {
				goto bad;
			}
		}
#endif /* CONFIG_MACF */
		if ((px_args.subsystem_root_path_size > 0) && (px_args.subsystem_root_path_size <= MAXPATHLEN)) {
			/*
			 * If a valid-looking subsystem root has been
			 * specified...
			 */
			if (IOTaskHasEntitlement(old_task, SPAWN_SUBSYSTEM_ROOT_ENTITLEMENT)) {
				/*
				 * ...AND the parent has the entitlement, copy
				 * the subsystem root path in.
				 */
				subsystem_root_path = zalloc_flags(ZV_NAMEI,
				    Z_WAITOK | Z_ZERO | Z_NOFAIL);

				if ((error = copyin(px_args.subsystem_root_path, subsystem_root_path, px_args.subsystem_root_path_size))) {
					goto bad;
				}

				/* Paranoia */
				subsystem_root_path[px_args.subsystem_root_path_size - 1] = 0;
			}
		}
#if CONFIG_EXCLAVES
		if ((px_args.conclave_id_size > 0) && (px_args.conclave_id_size <= MAXCONCLAVENAME) &&
		    (exclaves_get_status() == EXCLAVES_STATUS_AVAILABLE)) {
			if (px_args.conclave_id) {
				if (imgp->ip_px_sa != NULL && (px_sa.psa_flags & POSIX_SPAWN_SETEXEC)) {
					/* Conclave id could be set only for true spawn */
					error = EINVAL;
					goto bad;
				}
				task_conclave_id = kalloc_data(MAXCONCLAVENAME,
				    Z_WAITOK | Z_ZERO | Z_NOFAIL);
				if ((error = copyin(px_args.conclave_id, task_conclave_id, MAXCONCLAVENAME))) {
					goto bad;
				}
				task_conclave_id[MAXCONCLAVENAME - 1] = 0;
			}
		}
#endif
	}

	if (IOTaskHasEntitlement(old_task, SPAWN_SET_PANIC_CRASH_BEHAVIOR)) {
		/* Truncate to uint8_t since we only support 2 flags for now */
		crash_behavior = (uint8_t)px_sa.psa_crash_behavior;
		crash_behavior_deadline = px_sa.psa_crash_behavior_deadline;
	}

	/* set uthread to parent */
	uthread = current_uthread();

	/*
	 * <rdar://6640530>; this does not result in a behaviour change
	 * relative to Leopard, so there should not be any existing code
	 * which depends on it.
	 */

	if (imgp->ip_px_sa != NULL) {
		struct _posix_spawnattr *psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
		if ((psa->psa_options & PSA_OPTION_PLUGIN_HOST_DISABLE_A_KEYS) == PSA_OPTION_PLUGIN_HOST_DISABLE_A_KEYS) {
			imgp->ip_flags |= IMGPF_PLUGIN_HOST_DISABLE_A_KEYS;
		}
#if (DEVELOPMENT || DEBUG)
		if ((psa->psa_options & PSA_OPTION_ALT_ROSETTA) == PSA_OPTION_ALT_ROSETTA) {
			imgp->ip_flags |= (IMGPF_ROSETTA | IMGPF_ALT_ROSETTA);
		}
#endif

		if ((error = exec_validate_spawnattr_policy(psa->psa_apptype)) != 0) {
			goto bad;
		}
	}

	/*
	 * If we don't have the extension flag that turns "posix_spawn()"
	 * into "execve() with options", then we will be creating a new
	 * process which does not inherit memory from the parent process,
	 * which is one of the most expensive things about using fork()
	 * and execve().
	 */
	if (imgp->ip_px_sa == NULL || !(px_sa.psa_flags & POSIX_SPAWN_SETEXEC)) {
		/* Set the new task's coalition, if it is requested.  */
		coalition_t coal[COALITION_NUM_TYPES] = { COALITION_NULL };
#if CONFIG_COALITIONS
		int i, ncoals;
		kern_return_t kr = KERN_SUCCESS;
		struct _posix_spawn_coalition_info coal_info;
		int coal_role[COALITION_NUM_TYPES];

		if (imgp->ip_px_sa == NULL || !px_args.coal_info) {
			goto do_fork1;
		}

		memset(&coal_info, 0, sizeof(coal_info));

		if (px_args.coal_info_size > sizeof(coal_info)) {
			px_args.coal_info_size = sizeof(coal_info);
		}
		error = copyin(px_args.coal_info,
		    &coal_info, px_args.coal_info_size);
		if (error != 0) {
			goto bad;
		}

		ncoals = 0;
		for (i = 0; i < COALITION_NUM_TYPES; i++) {
			uint64_t cid = coal_info.psci_info[i].psci_id;
			if (cid != 0) {
				/*
				 * don't allow tasks which are not in a
				 * privileged coalition to spawn processes
				 * into coalitions other than their own
				 */
				if (!task_is_in_privileged_coalition(proc_task(p), i) &&
				    !IOTaskHasEntitlement(proc_task(p), COALITION_SPAWN_ENTITLEMENT)) {
					coal_dbg("ERROR: %d not in privilegd "
					    "coalition of type %d",
					    proc_getpid(p), i);
					spawn_coalitions_release_all(coal);
					error = EPERM;
					goto bad;
				}

				coal_dbg("searching for coalition id:%llu", cid);
				/*
				 * take a reference and activation on the
				 * coalition to guard against free-while-spawn
				 * races
				 */
				coal[i] = coalition_find_and_activate_by_id(cid);
				if (coal[i] == COALITION_NULL) {
					coal_dbg("could not find coalition id:%llu "
					    "(perhaps it has been terminated or reaped)", cid);
					/*
					 * release any other coalition's we
					 * may have a reference to
					 */
					spawn_coalitions_release_all(coal);
					error = ESRCH;
					goto bad;
				}
				if (coalition_type(coal[i]) != i) {
					coal_dbg("coalition with id:%lld is not of type:%d"
					    " (it's type:%d)", cid, i, coalition_type(coal[i]));
					spawn_coalitions_release_all(coal);
					error = ESRCH;
					goto bad;
				}
				coal_role[i] = coal_info.psci_info[i].psci_role;
				ncoals++;
			}
		}
		if (ncoals < COALITION_NUM_TYPES) {
			/*
			 * If the user is attempting to spawn into a subset of
			 * the known coalition types, then make sure they have
			 * _at_least_ specified a resource coalition. If not,
			 * the following fork1() call will implicitly force an
			 * inheritance from 'p' and won't actually spawn the
			 * new task into the coalitions the user specified.
			 * (also the call to coalitions_set_roles will panic)
			 */
			if (coal[COALITION_TYPE_RESOURCE] == COALITION_NULL) {
				spawn_coalitions_release_all(coal);
				error = EINVAL;
				goto bad;
			}
		}
do_fork1:
#endif /* CONFIG_COALITIONS */

		/*
		 * note that this will implicitly inherit the
		 * caller's persona (if it exists)
		 */
		error = fork1(p, &imgp->ip_new_thread, PROC_CREATE_SPAWN, coal);
		/* returns a thread and task reference */

		if (error == 0) {
			new_task = get_threadtask(imgp->ip_new_thread);
		}
#if CONFIG_COALITIONS
		/* set the roles of this task within each given coalition */
		if (error == 0) {
			kr = coalitions_set_roles(coal, new_task, coal_role);
			if (kr != KERN_SUCCESS) {
				error = EINVAL;
			}
			if (kdebug_debugid_enabled(MACHDBG_CODE(DBG_MACH_COALITION,
			    MACH_COALITION_ADOPT))) {
				for (i = 0; i < COALITION_NUM_TYPES; i++) {
					if (coal[i] != COALITION_NULL) {
						/*
						 * On 32-bit targets, uniqueid
						 * will get truncated to 32 bits
						 */
						KDBG_RELEASE(MACHDBG_CODE(
							    DBG_MACH_COALITION,
							    MACH_COALITION_ADOPT),
						    coalition_id(coal[i]),
						    get_task_uniqueid(new_task));
					}
				}
			}
		}

		/* drop our references and activations - fork1() now holds them */
		spawn_coalitions_release_all(coal);
#endif /* CONFIG_COALITIONS */
		if (error != 0) {
			goto bad;
		}
		imgp->ip_flags |= IMGPF_SPAWN;  /* spawn w/o exec */
		spawn_no_exec = TRUE;           /* used in later tests */
	} else {
		/* Adjust the user proc count */
		(void)chgproccnt(kauth_getruid(), 1);
		/*
		 * For execve case, create a new proc, task and thread
		 * but don't make the proc visible to userland. After
		 * image activation, the new proc would take place of
		 * the old proc in pid hash and other lists that make
		 * the proc visible to the system.
		 */
		imgp->ip_new_thread = cloneproc(old_task, NULL, p, CLONEPROC_EXEC);

		/* task and thread ref returned by cloneproc */
		if (imgp->ip_new_thread == NULL) {
			(void)chgproccnt(kauth_getruid(), -1);
			error = ENOMEM;
			goto bad;
		}

		new_task = get_threadtask(imgp->ip_new_thread);
		imgp->ip_flags |= IMGPF_EXEC;
	}

	p = (proc_t)get_bsdthreadtask_info(imgp->ip_new_thread);

	if (spawn_no_exec) {
		/*
		 * We had to wait until this point before firing the
		 * proc:::create probe, otherwise p would not point to the
		 * child process.
		 */
		DTRACE_PROC1(create, proc_t, p);
	}
	assert(p != NULL);

	if (subsystem_root_path) {
		/* If a subsystem root was specified, swap it in */
		char * old_subsystem_root_path = p->p_subsystem_root_path;
		p->p_subsystem_root_path = subsystem_root_path;
		subsystem_root_path = old_subsystem_root_path;
	}

	p->p_crash_behavior = crash_behavior;
	p->p_crash_behavior_deadline = crash_behavior_deadline;

	p->p_crash_count = px_sa.psa_crash_count;
	p->p_throttle_timeout = px_sa.psa_throttle_timeout;

	/* We'll need the subsystem root for setting up Apple strings */
	imgp->ip_subsystem_root_path = p->p_subsystem_root_path;

	context.vc_thread = imgp->ip_new_thread;
	context.vc_ucred = proc_ucred_unsafe(p);  /* in init */

	/*
	 * Post fdt_fork(), pre exec_handle_sugid() - this is where we want
	 * to handle the file_actions.
	 */

	/* Has spawn file actions? */
	if (imgp->ip_px_sfa != NULL) {
		/*
		 * The POSIX_SPAWN_CLOEXEC_DEFAULT flag
		 * is handled in exec_handle_file_actions().
		 */
#if CONFIG_AUDIT
		/*
		 * The file actions auditing can overwrite the upath of
		 * AUE_POSIX_SPAWN audit record.  Save the audit record.
		 */
		struct kaudit_record *save_uu_ar = uthread->uu_ar;
		uthread->uu_ar = NULL;
#endif
		error = exec_handle_file_actions(imgp,
		    imgp->ip_px_sa != NULL ? px_sa.psa_flags : 0);
#if CONFIG_AUDIT
		/* Restore the AUE_POSIX_SPAWN audit record. */
		uthread->uu_ar = save_uu_ar;
#endif
		if (error != 0) {
			goto bad;
		}
	}

	/* Has spawn port actions? */
	if (imgp->ip_px_spa != NULL) {
#if CONFIG_AUDIT
		/*
		 * Do the same for the port actions as we did for the file
		 * actions.  Save the AUE_POSIX_SPAWN audit record.
		 */
		struct kaudit_record *save_uu_ar = uthread->uu_ar;
		uthread->uu_ar = NULL;
#endif
		error = exec_handle_port_actions(imgp, &port_actions);
#if CONFIG_AUDIT
		/* Restore the AUE_POSIX_SPAWN audit record. */
		uthread->uu_ar = save_uu_ar;
#endif
		if (error != 0) {
			goto bad;
		}
	}

	/* Has spawn attr? */
	if (imgp->ip_px_sa != NULL) {
		/*
		 * Reset UID/GID to parent's RUID/RGID; This works only
		 * because the operation occurs before the call
		 * to exec_handle_sugid() by the image activator called
		 * from exec_activate_image().
		 *
		 * POSIX requires that any setuid/setgid bits on the process
		 * image will take precedence over the spawn attributes
		 * (re)setting them.
		 *
		 * Modifications to p_ucred must be guarded using the
		 * proc's ucred lock. This prevents others from accessing
		 * a garbage credential.
		 */
		if (px_sa.psa_flags & POSIX_SPAWN_RESETIDS) {
			kauth_cred_proc_update(p, PROC_SETTOKEN_NONE,
			    ^bool (kauth_cred_t parent __unused, kauth_cred_t model){
				return kauth_cred_model_setuidgid(model,
				kauth_cred_getruid(parent),
				kauth_cred_getrgid(parent));
			});
		}

		if (imgp->ip_px_pcred_info) {
			if (!spawn_no_exec) {
				error = ENOTSUP;
				goto bad;
			}

			error = spawn_posix_cred_adopt(p, imgp->ip_px_pcred_info);
			if (error != 0) {
				goto bad;
			}
		}

#if CONFIG_PERSONAS
		if (imgp->ip_px_persona != NULL) {
			if (!spawn_no_exec) {
				error = ENOTSUP;
				goto bad;
			}

			/*
			 * If we were asked to spawn a process into a new persona,
			 * do the credential switch now (which may override the UID/GID
			 * inherit done just above). It's important to do this switch
			 * before image activation both for reasons stated above, and
			 * to ensure that the new persona has access to the image/file
			 * being executed.
			 */
			error = spawn_persona_adopt(p, imgp->ip_px_persona);
			if (error != 0) {
				goto bad;
			}
		}
#endif /* CONFIG_PERSONAS */
#if !SECURE_KERNEL
		/*
		 * Disable ASLR for the spawned process.
		 *
		 * But only do so if we are not embedded + RELEASE.
		 * While embedded allows for a boot-arg (-disable_aslr)
		 * to deal with this (which itself is only honored on
		 * DEVELOPMENT or DEBUG builds of xnu), it is often
		 * useful or necessary to disable ASLR on a per-process
		 * basis for unit testing and debugging.
		 */
		if (px_sa.psa_flags & _POSIX_SPAWN_DISABLE_ASLR) {
			OSBitOrAtomic(P_DISABLE_ASLR, &p->p_flag);
		}
#endif /* !SECURE_KERNEL */

		/* Randomize high bits of ASLR slide */
		if (px_sa.psa_flags & _POSIX_SPAWN_HIGH_BITS_ASLR) {
			imgp->ip_flags |= IMGPF_HIGH_BITS_ASLR;
		}

#if !SECURE_KERNEL
		/*
		 * Forcibly disallow execution from data pages for the spawned process
		 * even if it would otherwise be permitted by the architecture default.
		 */
		if (px_sa.psa_flags & _POSIX_SPAWN_ALLOW_DATA_EXEC) {
			imgp->ip_flags |= IMGPF_ALLOW_DATA_EXEC;
		}
#endif /* !SECURE_KERNEL */

#if     __has_feature(ptrauth_calls)
		if (vm_shared_region_reslide_aslr && is_64 && (px_sa.psa_flags & _POSIX_SPAWN_RESLIDE)) {
			imgp->ip_flags |= IMGPF_RESLIDE;
		}
#endif /* __has_feature(ptrauth_calls) */

		if ((px_sa.psa_apptype & POSIX_SPAWN_PROC_TYPE_MASK) ==
		    POSIX_SPAWN_PROC_TYPE_DRIVER) {
			imgp->ip_flags |= IMGPF_DRIVER;
		}
	}

	/*
	 * Disable ASLR during image activation.  This occurs either if the
	 * _POSIX_SPAWN_DISABLE_ASLR attribute was found above or if
	 * P_DISABLE_ASLR was inherited from the parent process.
	 */
	if (p->p_flag & P_DISABLE_ASLR) {
		imgp->ip_flags |= IMGPF_DISABLE_ASLR;
	}

	/*
	 * Clear transition flag so we won't hang if exec_activate_image() causes
	 * an automount (and launchd does a proc sysctl to service it).
	 *
	 * <rdar://problem/6848672>, <rdar://problem/5959568>.
	 */
	proc_transend(p, 0);
	proc_transit_set = 0;

	if (!spawn_no_exec) {
		/*
		 * Clear the signal lock in case of exec, since
		 * image activation uses psignal on child process.
		 */
		proc_signalend(p, 0);
		proc_signal_set = 0;
	}

#if MAC_SPAWN   /* XXX */
	if (uap->mac_p != USER_ADDR_NULL) {
		error = mac_execve_enter(uap->mac_p, imgp);
		if (error) {
			goto bad;
		}
	}
#endif


	/*
	 * Activate the image.
	 * Warning: If activation failed after point of no return, it returns error
	 * as 0 and pretends the call succeeded.
	 */
	error = exec_activate_image(imgp);
#if defined(HAS_APPLE_PAC)
	const uint8_t disable_user_jop = imgp->ip_flags & IMGPF_NOJOP ? TRUE : FALSE;
	ml_task_set_jop_pid_from_shared_region(new_task, disable_user_jop);
	ml_task_set_disable_user_jop(new_task, disable_user_jop);
	ml_thread_set_disable_user_jop(imgp->ip_new_thread, disable_user_jop);
	ml_thread_set_jop_pid(imgp->ip_new_thread, new_task);
#endif


	/*
	 * If you've come here to add support for some new HW feature or some per-process or per-vmmap
	 * or per-pmap flag that needs to be set before the process runs, or are in general lost, here
	 * is some help. This summary was accurate as of Jul 2022. Use git log as needed. This comment
	 * is here to prevent a recurrence of rdar://96307913
	 *
	 * In posix_spawn, following is what happens:
	 * 1. Lots of prep and checking work
	 * 2. Image activation via exec_activate_image(). The new task will get a new pmap here
	 * 3. More prep work. (YOU ARE HERE)
	 * 4. exec_resettextvp() is called
	 * 5. At this point it is safe to check entitlements and code signatures
	 * 6. task_clear_return_wait(get_threadtask(imgp->ip_new_thread), TCRW_CLEAR_INITIAL_WAIT);
	 *    The new thread is allowed to run in kernel. It cannot yet get to userland
	 * 7. More things done here. This is your chance to affect the task before it runs in
	 *    userspace
	 * 8. task_clear_return_wait(get_threadtask(imgp->ip_new_thread), TCRW_CLEAR_FINAL_WAIT);
	 *     The new thread is allowed to run in userland
	 */

	if (error == 0 && !spawn_no_exec) {
		p = proc_exec_switch_task(current_proc(), p, old_task, new_task, imgp, &inherit);
		/* proc ref returned */
		should_release_proc_ref = TRUE;
	}

	if (error == 0) {
		/* process completed the exec, but may have failed after point of no return */
		exec_done = TRUE;
	}

#if CONFIG_EXCLAVES
	if (!error && task_conclave_id != NULL) {
		kern_return_t kr;
		kr = task_add_conclave(new_task, imgp->ip_vp, (int64_t)imgp->ip_arch_offset,
		    task_conclave_id);
		if (kr != KERN_SUCCESS) {
			error = EINVAL;
			goto bad;
		}
	}
#endif

	if (!error && imgp->ip_px_sa != NULL) {
		thread_t child_thread = imgp->ip_new_thread;
		uthread_t child_uthread = get_bsdthread_info(child_thread);

		/*
		 * Because of POSIX_SPAWN_SETEXEC, we need to handle this after image
		 * activation, else when image activation fails (before the point of no
		 * return) would leave the parent process in a modified state.
		 */
		if (px_sa.psa_flags & POSIX_SPAWN_SETPGROUP) {
			struct setpgid_args spga;
			spga.pid = proc_getpid(p);
			spga.pgid = px_sa.psa_pgroup;
			/*
			 * Effectively, call setpgid() system call; works
			 * because there are no pointer arguments.
			 */
			if ((error = setpgid(p, &spga, ival)) != 0) {
				goto bad_px_sa;
			}
		}

		if (px_sa.psa_flags & POSIX_SPAWN_SETSID) {
			error = setsid_internal(p);
			if (error != 0) {
				goto bad_px_sa;
			}
		}

		/*
		 * If we have a spawn attr, and it contains signal related flags,
		 * the we need to process them in the "context" of the new child
		 * process, so we have to process it following image activation,
		 * prior to making the thread runnable in user space.  This is
		 * necessitated by some signal information being per-thread rather
		 * than per-process, and we don't have the new allocation in hand
		 * until after the image is activated.
		 */

		/*
		 * Mask a list of signals, instead of them being unmasked, if
		 * they were unmasked in the parent; note that some signals
		 * are not maskable.
		 */
		if (px_sa.psa_flags & POSIX_SPAWN_SETSIGMASK) {
			child_uthread->uu_sigmask = (px_sa.psa_sigmask & ~sigcantmask);
		}
		/*
		 * Default a list of signals instead of ignoring them, if
		 * they were ignored in the parent.  Note that we pass
		 * spawn_no_exec to setsigvec() to indicate that we called
		 * fork1() and therefore do not need to call proc_signalstart()
		 * internally.
		 */
		if (px_sa.psa_flags & POSIX_SPAWN_SETSIGDEF) {
			vec.sa_handler = SIG_DFL;
			vec.sa_tramp = 0;
			vec.sa_mask = 0;
			vec.sa_flags = 0;
			for (sig = 1; sig < NSIG; sig++) {
				if (px_sa.psa_sigdefault & (1 << (sig - 1))) {
					error = setsigvec(p, child_thread, sig, &vec, spawn_no_exec);
				}
			}
		}

		/*
		 * Activate the CPU usage monitor, if requested. This is done via a task-wide, per-thread CPU
		 * usage limit, which will generate a resource exceeded exception if any one thread exceeds the
		 * limit.
		 *
		 * Userland gives us interval in seconds, and the kernel SPI expects nanoseconds.
		 */
		if ((px_sa.psa_cpumonitor_percent != 0) && (px_sa.psa_cpumonitor_percent < UINT8_MAX)) {
			/*
			 * Always treat a CPU monitor activation coming from spawn as entitled. Requiring
			 * an entitlement to configure the monitor a certain way seems silly, since
			 * whomever is turning it on could just as easily choose not to do so.
			 */
			error = proc_set_task_ruse_cpu(proc_task(p),
			    TASK_POLICY_RESOURCE_ATTRIBUTE_NOTIFY_EXC,
			    (uint8_t)px_sa.psa_cpumonitor_percent,
			    px_sa.psa_cpumonitor_interval * NSEC_PER_SEC,
			    0, TRUE);
		}


		if (px_pcred_info &&
		    (px_pcred_info->pspci_flags & POSIX_SPAWN_POSIX_CRED_LOGIN)) {
			/*
			 * setlogin() must happen after setsid()
			 */
			setlogin_internal(p, px_pcred_info->pspci_login);
		}

bad_px_sa:
		if (error != 0) {
			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_PSATTR, 0, 0);
			exec_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_PSATTR);
		}
	}

bad:

	if (error == 0) {
		/* reset delay idle sleep status if set */
#if CONFIG_DELAY_IDLE_SLEEP
		if ((p->p_flag & P_DELAYIDLESLEEP) == P_DELAYIDLESLEEP) {
			OSBitAndAtomic(~((uint32_t)P_DELAYIDLESLEEP), &p->p_flag);
		}
#endif /* CONFIG_DELAY_IDLE_SLEEP */
		/* upon  successful spawn, re/set the proc control state */
		if (imgp->ip_px_sa != NULL) {
			switch (px_sa.psa_pcontrol) {
			case POSIX_SPAWN_PCONTROL_THROTTLE:
				p->p_pcaction = P_PCTHROTTLE;
				break;
			case POSIX_SPAWN_PCONTROL_SUSPEND:
				p->p_pcaction = P_PCSUSP;
				break;
			case POSIX_SPAWN_PCONTROL_KILL:
				p->p_pcaction = P_PCKILL;
				break;
			case POSIX_SPAWN_PCONTROL_NONE:
			default:
				p->p_pcaction = 0;
				break;
			}
			;
		}
		exec_resettextvp(p, imgp);

		/*
		 * Enable new task IPC access if exec_activate_image() returned an
		 * active task. (Checks active bit in ipc_task_enable() under lock).
		 * Must enable after resettextvp so that task port policies are not evaluated
		 * until the csblob in the textvp is accurately reflected.
		 */
		ipc_task_enable(new_task);

		/* Set task exception ports now that we can check entitlements */
		if (imgp->ip_px_spa != NULL) {
			error = exec_handle_exception_port_actions(imgp, &port_actions);
		}

#if CONFIG_MEMORYSTATUS
		/* Set jetsam priority for DriverKit processes */
		if (px_sa.psa_apptype == POSIX_SPAWN_PROC_TYPE_DRIVER) {
			px_sa.psa_priority = JETSAM_PRIORITY_DRIVER_APPLE;
		}

		/* Has jetsam attributes? */
		if (imgp->ip_px_sa != NULL && (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_SET)) {
			/*
			 * With 2-level high-water-mark support, POSIX_SPAWN_JETSAM_HIWATER_BACKGROUND is no
			 * longer relevant, as background limits are described via the inactive limit slots.
			 *
			 * That said, however, if the POSIX_SPAWN_JETSAM_HIWATER_BACKGROUND is passed in,
			 * we attempt to mimic previous behavior by forcing the BG limit data into the
			 * inactive/non-fatal mode and force the active slots to hold system_wide/fatal mode.
			 */

			if (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_HIWATER_BACKGROUND) {
				memorystatus_update(p, px_sa.psa_priority, 0, FALSE, /* assertion priority */
				    (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_USE_EFFECTIVE_PRIORITY),
				    TRUE,
				    -1, TRUE,
				    px_sa.psa_memlimit_inactive, FALSE);
			} else {
				memorystatus_update(p, px_sa.psa_priority, 0, FALSE, /* assertion priority */
				    (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_USE_EFFECTIVE_PRIORITY),
				    TRUE,
				    px_sa.psa_memlimit_active,
				    (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_MEMLIMIT_ACTIVE_FATAL),
				    px_sa.psa_memlimit_inactive,
				    (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_MEMLIMIT_INACTIVE_FATAL));
			}
		}

		/* Has jetsam relaunch behavior? */
		if (imgp->ip_px_sa != NULL && (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_RELAUNCH_BEHAVIOR_MASK)) {
			/*
			 * Launchd has passed in data indicating the behavior of this process in response to jetsam.
			 * This data would be used by the jetsam subsystem to determine the position and protection
			 * offered to this process on dirty -> clean transitions.
			 */
			int relaunch_flags = P_MEMSTAT_RELAUNCH_UNKNOWN;
			switch (px_sa.psa_jetsam_flags & POSIX_SPAWN_JETSAM_RELAUNCH_BEHAVIOR_MASK) {
			case POSIX_SPAWN_JETSAM_RELAUNCH_BEHAVIOR_LOW:
				relaunch_flags = P_MEMSTAT_RELAUNCH_LOW;
				break;
			case POSIX_SPAWN_JETSAM_RELAUNCH_BEHAVIOR_MED:
				relaunch_flags = P_MEMSTAT_RELAUNCH_MED;
				break;
			case POSIX_SPAWN_JETSAM_RELAUNCH_BEHAVIOR_HIGH:
				relaunch_flags = P_MEMSTAT_RELAUNCH_HIGH;
				break;
			default:
				break;
			}
			memorystatus_relaunch_flags_update(p, relaunch_flags);
		}

#endif /* CONFIG_MEMORYSTATUS */
		if (imgp->ip_px_sa != NULL && px_sa.psa_thread_limit > 0) {
			task_set_thread_limit(new_task, (uint16_t)px_sa.psa_thread_limit);
		}

#if CONFIG_PROC_RESOURCE_LIMITS
		if (imgp->ip_px_sa != NULL && (px_sa.psa_port_soft_limit > 0 || px_sa.psa_port_hard_limit > 0)) {
			task_set_port_space_limits(new_task, (uint32_t)px_sa.psa_port_soft_limit,
			    (uint32_t)px_sa.psa_port_hard_limit);
		}

		if (imgp->ip_px_sa != NULL && (px_sa.psa_filedesc_soft_limit > 0 || px_sa.psa_filedesc_hard_limit > 0)) {
			proc_set_filedesc_limits(p, (int)px_sa.psa_filedesc_soft_limit,
			    (int)px_sa.psa_filedesc_hard_limit);
		}
		if (imgp->ip_px_sa != NULL && (px_sa.psa_kqworkloop_soft_limit > 0 || px_sa.psa_kqworkloop_hard_limit > 0)) {
			proc_set_kqworkloop_limits(p, (int)px_sa.psa_kqworkloop_soft_limit,
			    (int)px_sa.psa_kqworkloop_hard_limit);
		}
#endif /* CONFIG_PROC_RESOURCE_LIMITS */

		/* Disable wakeup monitoring for DriverKit processes */
		if (px_sa.psa_apptype == POSIX_SPAWN_PROC_TYPE_DRIVER) {
			uint32_t      flags = WAKEMON_DISABLE;
			task_wakeups_monitor_ctl(new_task, &flags, NULL);
		}
	}


	/*
	 * If we successfully called fork1() or cloneproc, we always need
	 * to do this. This is because we come back from that call with
	 * signals blocked in the child, and we have to unblock them, for exec
	 * case they are unblocked before activation, but for true spawn case
	 * we want to wait until after we've performed any spawn actions.
	 * This has to happen before process_signature(), which uses psignal.
	 */
	if (proc_transit_set) {
		proc_transend(p, 0);
	}

	/*
	 * Drop the signal lock on the child which was taken on our
	 * behalf by forkproc()/cloneproc() to prevent signals being
	 * received by the child in a partially constructed state.
	 */
	if (proc_signal_set) {
		proc_signalend(p, 0);
	}

	if (error == 0) {
		/*
		 * We need to initialize the bank context behind the protection of
		 * the proc_trans lock to prevent a race with exit. We can't do this during
		 * exec_activate_image because task_bank_init checks entitlements that
		 * aren't loaded until subsequent calls (including exec_resettextvp).
		 */
		error = proc_transstart(p, 0, 0);

		if (error == 0) {
			task_bank_init(new_task);
			proc_transend(p, 0);
		}

#if __arm64__
		proc_footprint_entitlement_hacks(p, new_task);
#endif /* __arm64__ */

#if XNU_TARGET_OS_OSX
#define SINGLE_JIT_ENTITLEMENT "com.apple.security.cs.single-jit"
		if (IOTaskHasEntitlement(new_task, SINGLE_JIT_ENTITLEMENT)) {
			vm_map_single_jit(get_task_map(new_task));
		}
#endif /* XNU_TARGET_OS_OSX */

#if __has_feature(ptrauth_calls)
		task_set_pac_exception_fatal_flag(new_task);
#endif /* __has_feature(ptrauth_calls) */
		task_set_jit_exception_fatal_flag(new_task);
	}

	/* Inherit task role from old task to new task for exec */
	if (error == 0 && !spawn_no_exec) {
		proc_inherit_task_role(new_task, old_task);
	}

#if CONFIG_ARCADE
	if (error == 0) {
		/*
		 * Check to see if we need to trigger an arcade upcall AST now
		 * that the vnode has been reset on the task.
		 */
		arcade_prepare(new_task, imgp->ip_new_thread);
	}
#endif /* CONFIG_ARCADE */

	if (error == 0) {
		proc_apply_jit_and_vm_policies(imgp, p, new_task);
	}

	/* Clear the initial wait on the thread before handling spawn policy */
	if (imgp && imgp->ip_new_thread) {
		task_clear_return_wait(get_threadtask(imgp->ip_new_thread), TCRW_CLEAR_INITIAL_WAIT);
	}

	/*
	 * Apply the spawnattr policy, apptype (which primes the task for importance donation),
	 * and bind any portwatch ports to the new task.
	 * This must be done after the exec so that the child's thread is ready,
	 * and after the in transit state has been released, because priority is
	 * dropped here so we need to be prepared for a potentially long preemption interval
	 *
	 * TODO: Consider splitting this up into separate phases
	 */
	if (error == 0 && imgp->ip_px_sa != NULL) {
		struct _posix_spawnattr *psa = (struct _posix_spawnattr *) imgp->ip_px_sa;

		error = exec_handle_spawnattr_policy(p, imgp->ip_new_thread, psa->psa_apptype, psa->psa_qos_clamp,
		    psa->psa_darwin_role, &port_actions);
	}

	/* Transfer the turnstile watchport boost to new task if in exec */
	if (error == 0 && !spawn_no_exec) {
		task_transfer_turnstile_watchports(old_task, new_task, imgp->ip_new_thread);
	}

	if (error == 0 && imgp->ip_px_sa != NULL) {
		struct _posix_spawnattr *psa = (struct _posix_spawnattr *) imgp->ip_px_sa;

		if (psa->psa_no_smt) {
			task_set_no_smt(new_task);
		}
		if (psa->psa_tecs) {
			task_set_tecs(new_task);
		}
	}

	if (error == 0 && imgp->ip_px_sa != NULL) {
		struct _posix_spawnattr *psa = (struct _posix_spawnattr *) imgp->ip_px_sa;

		if (psa->psa_options & PSA_OPTION_DATALESS_IOPOLICY) {
			struct _iopol_param_t iop_param = {
				.iop_scope = IOPOL_SCOPE_PROCESS,
				.iop_iotype = IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES,
				.iop_policy = psa->psa_dataless_iopolicy,
			};
			error = iopolicysys_vfs_materialize_dataless_files(p, IOPOL_CMD_SET, iop_param.iop_scope,
			    iop_param.iop_policy, &iop_param);
		}
	}

	if (error == 0) {
		/* Apply the main thread qos */
		thread_t main_thread = imgp->ip_new_thread;
		task_set_main_thread_qos(new_task, main_thread);
	}

	/*
	 * Release any ports we kept around for binding to the new task
	 * We need to release the rights even if the posix_spawn has failed.
	 */
	if (imgp->ip_px_spa != NULL) {
		exec_port_actions_destroy(&port_actions);
	}

	/*
	 * We have to delay operations which might throw a signal until after
	 * the signals have been unblocked; however, we want that to happen
	 * after exec_resettextvp() so that the textvp is correct when they
	 * fire.
	 */
	if (error == 0) {
		error = process_signature(p, imgp);

		/*
		 * Pay for our earlier safety; deliver the delayed signals from
		 * the incomplete spawn process now that it's complete.
		 */
		if (imgp != NULL && spawn_no_exec && (p->p_lflag & P_LTRACED)) {
			psignal_vfork(p, proc_task(p), imgp->ip_new_thread, SIGTRAP);
		}

		if (error == 0 && !spawn_no_exec) {
			KDBG(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXEC),
			    proc_getpid(p));
		}
	}

	if (spawn_no_exec) {
		/* flag the 'fork' has occurred */
		proc_knote(p->p_pptr, NOTE_FORK | proc_getpid(p));
	}

	/* flag exec has occurred, notify only if it has not failed due to FP Key error */
	if (!error && ((p->p_lflag & P_LTERM_DECRYPTFAIL) == 0)) {
		proc_knote(p, NOTE_EXEC);
	}

	if (imgp != NULL) {
		uthread_set_exec_data(current_uthread(), NULL);
		if (imgp->ip_vp) {
			vnode_put(imgp->ip_vp);
		}
		if (imgp->ip_scriptvp) {
			vnode_put(imgp->ip_scriptvp);
		}
		if (imgp->ip_strings) {
			execargs_free(imgp);
		}
		if (imgp->ip_free_map) {
			/* Free the map after dropping iocount on vnode to avoid deadlock */
			vm_map_deallocate(imgp->ip_free_map);
		}
		kfree_data(imgp->ip_px_sfa,
		    px_args.file_actions_size);
		kfree_data(imgp->ip_px_spa,
		    px_args.port_actions_size);
#if CONFIG_PERSONAS
		kfree_data(imgp->ip_px_persona,
		    px_args.persona_info_size);
#endif
		kfree_data(imgp->ip_px_pcred_info,
		    px_args.posix_cred_info_size);

		if (subsystem_root_path != NULL) {
			zfree(ZV_NAMEI, subsystem_root_path);
		}
#if CONFIG_MACF
		struct ip_px_smpx_s *px_s = &imgp->ip_px_smpx;
		kfree_data(px_s->array, px_args.mac_extensions_size);
		kfree_data(px_s->data, (vm_size_t)px_s->datalen);

		if (imgp->ip_execlabelp) {
			mac_cred_label_free(imgp->ip_execlabelp);
			imgp->ip_execlabelp = NULL;
		}
		if (imgp->ip_scriptlabelp) {
			mac_vnode_label_free(imgp->ip_scriptlabelp);
			imgp->ip_scriptlabelp = NULL;
		}
		if (imgp->ip_cs_error != OS_REASON_NULL) {
			os_reason_free(imgp->ip_cs_error);
			imgp->ip_cs_error = OS_REASON_NULL;
		}
		if (imgp->ip_inherited_shared_region_id != NULL) {
			kfree_data(imgp->ip_inherited_shared_region_id,
			    strlen(imgp->ip_inherited_shared_region_id) + 1);
			imgp->ip_inherited_shared_region_id = NULL;
		}
#endif
	}

#if CONFIG_DTRACE
	if (spawn_no_exec) {
		/*
		 * In the original DTrace reference implementation,
		 * posix_spawn() was a libc routine that just
		 * did vfork(2) then exec(2).  Thus the proc::: probes
		 * are very fork/exec oriented.  The details of this
		 * in-kernel implementation of posix_spawn() is different
		 * (while producing the same process-observable effects)
		 * particularly w.r.t. errors, and which thread/process
		 * is constructing what on behalf of whom.
		 */
		if (error) {
			DTRACE_PROC1(spawn__failure, int, error);
		} else {
			DTRACE_PROC(spawn__success);
			/*
			 * Some DTrace scripts, e.g. newproc.d in
			 * /usr/bin, rely on the the 'exec-success'
			 * probe being fired in the child after the
			 * new process image has been constructed
			 * in order to determine the associated pid.
			 *
			 * So, even though the parent built the image
			 * here, for compatibility, mark the new thread
			 * so 'exec-success' fires on it as it leaves
			 * the kernel.
			 */
			dtrace_thread_didexec(imgp->ip_new_thread);
		}
	} else {
		if (error) {
			DTRACE_PROC1(exec__failure, int, error);
		} else {
			dtrace_thread_didexec(imgp->ip_new_thread);
		}
	}

	if ((dtrace_proc_waitfor_hook = dtrace_proc_waitfor_exec_ptr) != NULL) {
		(*dtrace_proc_waitfor_hook)(p);
	}
#endif

#if CONFIG_AUDIT
	if (!error && AUDIT_ENABLED() && p) {
		/* Add the CDHash of the new process to the audit record */
		uint8_t *cdhash = cs_get_cdhash(p);
		if (cdhash) {
			AUDIT_ARG(data, cdhash, sizeof(uint8_t), CS_CDHASH_LEN);
		}
	}
#endif

	/* terminate the new task if exec failed  */
	if (new_task != NULL && task_is_exec_copy(new_task)) {
		task_terminate_internal(new_task);
	}

	if (exec_failure_reason && !spawn_no_exec) {
		psignal_with_reason(p, SIGKILL, exec_failure_reason);
		exec_failure_reason = NULL;
	}

	/* Meizu desktop diagnostic: result, not just a launch request. */
	if (p && (strcmp(p->p_comm, "SpringBoard") == 0 ||
	    strcmp(p->p_comm, "backboardd") == 0)) {
		printf("meizu: desktop spawn-result pid=%d comm=%s error=%d exec_done=%d spawn=%d\n",
		    proc_getpid(p), p->p_comm, error, exec_done, spawn_no_exec);
	}
	/* Return to both the parent and the child? */
	if (imgp != NULL && spawn_no_exec) {
		/*
		 * If the parent wants the pid, copy it out
		 */
		if (error == 0 && pid != USER_ADDR_NULL) {
			_Static_assert(sizeof(pid_t) == 4, "posix_spawn() assumes a 32-bit pid_t");
			bool aligned = (pid & 3) == 0;
			if (aligned) {
				(void)copyout_atomic32(proc_getpid(p), pid);
			} else {
				(void)suword(pid, proc_getpid(p));
			}
		}
		retval[0] = error;

		/*
		 * If we had an error, perform an internal reap ; this is
		 * entirely safe, as we have a real process backing us.
		 */
		if (error) {
			proc_list_lock();
			p->p_listflag |= P_LIST_DEADPARENT;
			proc_list_unlock();
			proc_lock(p);
			/* make sure no one else has killed it off... */
			if (p->p_stat != SZOMB && p->exit_thread == NULL) {
				p->exit_thread = current_thread();
				p->p_posix_spawn_failed = true;
				proc_unlock(p);
				exit1(p, 1, (int *)NULL);
			} else {
				/* someone is doing it for us; just skip it */
				proc_unlock(p);
			}
		}
	}

	/*
	 * Do not terminate the current task, if proc_exec_switch_task did not
	 * switch the tasks, terminating the current task without the switch would
	 * result in loosing the SIGKILL status.
	 */
	if (task_did_exec(old_task)) {
		/* Terminate the current task, since exec will start in new task */
		task_terminate_internal(old_task);
	}

	/* Release the thread ref returned by cloneproc/fork1 */
	if (imgp != NULL && imgp->ip_new_thread) {
		/* clear the exec complete flag if there is an error before point of no-return */
		uint32_t clearwait_flags = TCRW_CLEAR_FINAL_WAIT;
		if (!spawn_no_exec && !exec_done && error != 0) {
			clearwait_flags |= TCRW_CLEAR_EXEC_COMPLETE;
		}
#if defined(XNU_OWNER_CACHE_MOUNT) && XNU_OWNER_CACHE_MOUNT
		/* spawn_no_exec distinguishes a new child from SETEXEC, not failed image activation. */
		if (error == 0 && exec_done && !(p->p_lflag & P_LTERM_DECRYPTFAIL)) {
			meizu_exec_capture(p, imgp->ip_new_thread);
			if (strcmp(p->p_comm, "SpringBoard") == 0) {
				meizu_sb_capture(p, imgp->ip_new_thread);
			}
		}
#endif
		/* Capture before the new thread can exec again or exit. */
		task_clear_return_wait(get_threadtask(imgp->ip_new_thread), clearwait_flags);
		thread_deallocate(imgp->ip_new_thread);
		imgp->ip_new_thread = NULL;
	}

	/* Release the ref returned by cloneproc/fork1 */
	if (new_task) {
		task_deallocate(new_task);
		new_task = NULL;
	}

	if (should_release_proc_ref) {
		proc_rele(p);
	}

	kfree_type(typeof(*__spawn_data), __spawn_data);

	if (inherit != NULL) {
		ipc_importance_release(inherit);
	}

#if CONFIG_EXCLAVES
	if (task_conclave_id != NULL) {
		kfree_data(task_conclave_id, MAXCONCLAVENAME);
	}
#endif

	assert(spawn_no_exec || exec_failure_reason == NULL);
	return error;
}

/*
 * proc_exec_switch_task
 *
 * Parameters:  old_proc		proc before exec
 *		new_proc		proc after exec
 *		old_task		task before exec
 *		new_task		task after exec
 *		imgp			image params
 *		inherit			resulting importance linkage
 *
 * Returns: proc.
 *
 * Note: The function will switch proc in pid hash from old proc to new proc.
 * The switch needs to happen after draining all proc refs and inside
 * a proc list lock. In the case of failure to switch the proc, which
 * might happen if the process received a SIGKILL or jetsam killed it,
 * it will make sure that the new tasks terminates. User proc ref returned
 * to caller.
 *
 * This function is called after point of no return, in the case
 * failure to switch, it will terminate the new task and swallow the
 * error and let the terminated process complete exec and die.
 */
proc_t
proc_exec_switch_task(proc_t old_proc, proc_t new_proc, task_t old_task, task_t new_task, struct image_params *imgp, void **inherit)
{
	boolean_t task_active;
	boolean_t proc_active;
	boolean_t thread_active;
	boolean_t reparent_traced_child = FALSE;
	thread_t old_thread = current_thread();
	thread_t new_thread = imgp->ip_new_thread;

	thread_set_exec_promotion(old_thread);
	old_proc = proc_refdrain_will_exec(old_proc);

	new_proc = proc_refdrain_will_exec(new_proc);
	/* extra proc ref returned to the caller */

	assert(get_threadtask(new_thread) == new_task);
	task_active = task_is_active(new_task);
	proc_active = !(old_proc->p_lflag & P_LEXIT);

	/* Check if the current thread is not aborted due to SIGKILL */
	thread_active = thread_is_active(old_thread);

	/*
	 * Do not switch the proc if the new task or proc is already terminated
	 * as a result of error in exec past point of no return
	 */
	if (proc_active && task_active && thread_active) {
		uthread_t new_uthread = get_bsdthread_info(new_thread);
		uthread_t old_uthread = current_uthread();

		/* Clear dispatchqueue and workloop ast offset */
		new_proc->p_dispatchqueue_offset = 0;
		new_proc->p_dispatchqueue_serialno_offset = 0;
		new_proc->p_dispatchqueue_label_offset = 0;
		new_proc->p_return_to_kernel_offset = 0;
		new_proc->p_pthread_wq_quantum_offset = 0;

		/* If old_proc is session leader, change the leader to new proc */
		session_replace_leader(old_proc, new_proc);

		proc_lock(old_proc);

		/* Copy the signal state, dtrace state and set bsd ast on new thread */
		act_set_astbsd(new_thread);
		new_uthread->uu_siglist |= old_uthread->uu_siglist;
		new_uthread->uu_siglist |= old_proc->p_siglist;
		new_uthread->uu_sigwait = old_uthread->uu_sigwait;
		new_uthread->uu_sigmask = old_uthread->uu_sigmask;
		new_uthread->uu_oldmask = old_uthread->uu_oldmask;
		new_uthread->uu_exit_reason = old_uthread->uu_exit_reason;
#if CONFIG_DTRACE
		new_uthread->t_dtrace_sig = old_uthread->t_dtrace_sig;
		new_uthread->t_dtrace_stop = old_uthread->t_dtrace_stop;
		new_uthread->t_dtrace_resumepid = old_uthread->t_dtrace_resumepid;
		assert(new_uthread->t_dtrace_scratch == NULL);
		new_uthread->t_dtrace_scratch = old_uthread->t_dtrace_scratch;

		old_uthread->t_dtrace_sig = 0;
		old_uthread->t_dtrace_stop = 0;
		old_uthread->t_dtrace_resumepid = 0;
		old_uthread->t_dtrace_scratch = NULL;
#endif

#if CONFIG_PROC_UDATA_STORAGE
		new_proc->p_user_data = old_proc->p_user_data;
#endif /* CONFIG_PROC_UDATA_STORAGE */

		/* Copy the resource accounting info */
		thread_copy_resource_info(new_thread, current_thread());

		/* Clear the exit reason and signal state on old thread */
		old_uthread->uu_exit_reason = NULL;
		old_uthread->uu_siglist = 0;

		task_set_did_exec_flag(old_task);
		task_clear_exec_copy_flag(new_task);

		task_copy_fields_for_exec(new_task, old_task);

		/*
		 * Need to transfer pending watch port boosts to the new task
		 * while still making sure that the old task remains in the
		 * importance linkage. Create an importance linkage from old task
		 * to new task, then switch the task importance base of old task
		 * and new task. After the switch the port watch boost will be
		 * boosting the new task and new task will be donating importance
		 * to old task.
		 */
		*inherit = ipc_importance_exec_switch_task(old_task, new_task);

		/* Transfer parent's ptrace state to child */
		new_proc->p_lflag &= ~(P_LTRACED | P_LSIGEXC | P_LNOATTACH);
		new_proc->p_lflag |= (old_proc->p_lflag & (P_LTRACED | P_LSIGEXC | P_LNOATTACH));
		new_proc->p_oppid = old_proc->p_oppid;

		if (old_proc->p_pptr != new_proc->p_pptr) {
			reparent_traced_child = TRUE;
			new_proc->p_lflag |= P_LTRACE_WAIT;
		}

		proc_unlock(old_proc);

		/* Update the list of proc knotes */
		proc_transfer_knotes(old_proc, new_proc);

		/* Update the proc interval timers */
		proc_inherit_itimers(old_proc, new_proc);

		proc_list_lock();

		/* Insert the new proc in child list of parent proc */
		p_reparentallchildren(old_proc, new_proc);

		/* Switch proc in pid hash */
		phash_replace_locked(old_proc, new_proc);

		/* Transfer the shadow flag to old proc */
		os_atomic_andnot(&new_proc->p_refcount, P_REF_SHADOW, relaxed);
		os_atomic_or(&old_proc->p_refcount, P_REF_SHADOW, relaxed);

		/* Change init proc if launchd exec */
		if (old_proc == initproc) {
			/* Take the ref on new proc after proc_refwake_did_exec */
			initproc = new_proc;
			/* Drop the proc ref on old proc */
			proc_rele(old_proc);
		}

		proc_list_unlock();
#if CONFIG_EXCLAVES
		if (task_inherit_conclave(old_task, new_task, imgp->ip_vp,
		    (int64_t)imgp->ip_arch_offset) != KERN_SUCCESS) {
			task_terminate_internal(new_task);
		}
#endif
	} else {
		task_terminate_internal(new_task);
	}

	proc_refwake_did_exec(new_proc);
	proc_refwake_did_exec(old_proc);

	/* Take a ref on initproc if it changed */
	if (new_proc == initproc) {
		initproc = proc_ref(new_proc, false);
		assert(initproc != PROC_NULL);
	}

	thread_clear_exec_promotion(old_thread);
	proc_rele(old_proc);

	if (reparent_traced_child) {
		proc_t pp = proc_parent(old_proc);
		assert(pp != PROC_NULL);

		proc_reparentlocked(new_proc, pp, 1, 0);
		proc_rele(pp);

		proc_lock(new_proc);
		new_proc->p_lflag &= ~P_LTRACE_WAIT;
		proc_unlock(new_proc);
	}

	return new_proc;
}

/*
 * execve
 *
 * Parameters:	uap->fname		File name to exec
 *		uap->argp		Argument list
 *		uap->envp		Environment list
 *
 * Returns:	0			Success
 *	__mac_execve:EINVAL		Invalid argument
 *	__mac_execve:ENOTSUP		Invalid argument
 *	__mac_execve:EACCES		Permission denied
 *	__mac_execve:EINTR		Interrupted function
 *	__mac_execve:ENOMEM		Not enough space
 *	__mac_execve:EFAULT		Bad address
 *	__mac_execve:ENAMETOOLONG	Filename too long
 *	__mac_execve:ENOEXEC		Executable file format error
 *	__mac_execve:ETXTBSY		Text file busy [misuse of error code]
 *	__mac_execve:???
 *
 * TODO:	Dynamic linker header address on stack is copied via suword()
 */
/* ARGSUSED */
volatile int meizu_exec_count;
volatile int meizu_exec_fail;
volatile int meizu_exec_errno;
volatile unsigned long long meizu_exec_entry;
volatile unsigned long long meizu_exec_base;

int
execve(proc_t p, struct execve_args *uap, int32_t *retval)
{
	struct __mac_execve_args muap;
	int err;

	memoryshot(VM_EXECVE, DBG_FUNC_NONE);

	muap.fname = uap->fname;
	muap.argp = uap->argp;
	muap.envp = uap->envp;
	muap.mac_p = USER_ADDR_NULL;
	meizu_exec_count = meizu_exec_count + 1;
	err = __mac_execve(p, &muap, retval);
	if (err) {
		meizu_exec_fail = meizu_exec_fail + 1;
		meizu_exec_errno = err;
		printf("meizu: exec fail pid=%d err=%d\n", proc_getpid(p), err);
	} else {
		extern unsigned long long meizu_thread_user_pc(thread_t);
		extern unsigned long long meizu_thread_user_x0(thread_t);
		meizu_exec_entry = meizu_thread_user_pc(current_thread());
		meizu_exec_base = meizu_thread_user_x0(current_thread());
		printf("meizu: exec ok pid=%d entry=%llx x0=%llx\n", proc_getpid(p),
		    meizu_exec_entry, meizu_exec_base);
	}

	return err;
}

/*
 * __mac_execve
 *
 * Parameters:	uap->fname		File name to exec
 *		uap->argp		Argument list
 *		uap->envp		Environment list
 *		uap->mac_p		MAC label supplied by caller
 *
 * Returns:	0			Success
 *		EINVAL			Invalid argument
 *		ENOTSUP			Not supported
 *		ENOEXEC			Executable file format error
 *	exec_activate_image:EINVAL	Invalid argument
 *	exec_activate_image:EACCES	Permission denied
 *	exec_activate_image:EINTR	Interrupted function
 *	exec_activate_image:ENOMEM	Not enough space
 *	exec_activate_image:EFAULT	Bad address
 *	exec_activate_image:ENAMETOOLONG	Filename too long
 *	exec_activate_image:ENOEXEC	Executable file format error
 *	exec_activate_image:ETXTBSY	Text file busy [misuse of error code]
 *	exec_activate_image:EBADEXEC	The executable is corrupt/unknown
 *	exec_activate_image:???
 *	mac_execve_enter:???
 *
 * TODO:	Dynamic linker header address on stack is copied via suword()
 */
volatile int meizu_mac_exec_count;

int

__mac_execve(proc_t p, struct __mac_execve_args *uap, int32_t *retval __unused)
{
	meizu_mac_exec_count = meizu_mac_exec_count + 1;
	struct image_params *imgp = NULL;
	struct vnode_attr *vap = NULL;
	struct vnode_attr *origvap = NULL;
	int error;
	int is_64 = IS_64BIT_PROCESS(p);
	struct vfs_context context;
	struct uthread  *uthread = NULL;
	task_t old_task = current_task();
	task_t new_task = NULL;
	boolean_t should_release_proc_ref = FALSE;
	boolean_t exec_done = FALSE;
	void *inherit = NULL;
	struct {
		struct image_params imgp;
		struct vnode_attr va;
		struct vnode_attr origva;
	} *__execve_data;

	/* Allocate a big chunk for locals instead of using stack since these
	 * structures a pretty big.
	 */
	__execve_data = kalloc_type(typeof(*__execve_data), Z_WAITOK | Z_ZERO);
	if (__execve_data == NULL) {
		error = ENOMEM;
		goto exit_with_error;
	}
	imgp = &__execve_data->imgp;
	vap = &__execve_data->va;
	origvap = &__execve_data->origva;

	/* Initialize the common data in the image_params structure */
	imgp->ip_user_fname = uap->fname;
	imgp->ip_user_argv = uap->argp;
	imgp->ip_user_envv = uap->envp;
	imgp->ip_vattr = vap;
	imgp->ip_origvattr = origvap;
	imgp->ip_vfs_context = &context;
	imgp->ip_flags = (is_64 ? IMGPF_WAS_64BIT_ADDR : IMGPF_NONE) | ((p->p_flag & P_DISABLE_ASLR) ? IMGPF_DISABLE_ASLR : IMGPF_NONE);
	imgp->ip_seg = (is_64 ? UIO_USERSPACE64 : UIO_USERSPACE32);
	imgp->ip_mac_return = 0;
	imgp->ip_cs_error = OS_REASON_NULL;
	imgp->ip_simulator_binary = IMGPF_SB_DEFAULT;
	imgp->ip_subsystem_root_path = NULL;
	uthread_set_exec_data(current_uthread(), imgp);

#if CONFIG_MACF
	if (uap->mac_p != USER_ADDR_NULL) {
		error = mac_execve_enter(uap->mac_p, imgp);
		if (error) {
			goto exit_with_error;
		}
	}
#endif
	uthread = current_uthread();
	{
		imgp->ip_flags |= IMGPF_EXEC;

		/* Adjust the user proc count */
		(void)chgproccnt(kauth_getruid(), 1);
		/*
		 * For execve case, create a new proc, task and thread
		 * but don't make the proc visible to userland. After
		 * image activation, the new proc would take place of
		 * the old proc in pid hash and other lists that make
		 * the proc visible to the system.
		 */
		imgp->ip_new_thread = cloneproc(old_task, NULL, p, CLONEPROC_EXEC);
		/* task and thread ref returned by cloneproc */
		if (imgp->ip_new_thread == NULL) {
			(void)chgproccnt(kauth_getruid(), -1);
			error = ENOMEM;
			goto exit_with_error;
		}

		new_task = get_threadtask(imgp->ip_new_thread);
	}

	p = (proc_t)get_bsdthreadtask_info(imgp->ip_new_thread);

	context.vc_thread = imgp->ip_new_thread;
	context.vc_ucred = kauth_cred_proc_ref(p);      /* XXX must NOT be kauth_cred_get() */

	imgp->ip_subsystem_root_path = p->p_subsystem_root_path;

	proc_transend(p, 0);
	proc_signalend(p, 0);


	/*
	 * Activate the image.
	 * Warning: If activation failed after point of no return, it returns error
	 * as 0 and pretends the call succeeded.
	 */
	error = exec_activate_image(imgp);
	/* thread and task ref returned for vfexec case */

	if (imgp->ip_new_thread != NULL) {
		/*
		 * task reference might be returned by exec_activate_image
		 * for vfexec.
		 */
		new_task = get_threadtask(imgp->ip_new_thread);
#if defined(HAS_APPLE_PAC)
		ml_task_set_disable_user_jop(new_task, imgp->ip_flags & IMGPF_NOJOP ? TRUE : FALSE);
		ml_thread_set_disable_user_jop(imgp->ip_new_thread, imgp->ip_flags & IMGPF_NOJOP ? TRUE : FALSE);
#endif
	}

	if (!error) {
		p = proc_exec_switch_task(current_proc(), p, old_task, new_task, imgp, &inherit);
		/* proc ref returned */
		should_release_proc_ref = TRUE;
	}

	kauth_cred_unref(&context.vc_ucred);

	if (!error) {
		exec_done = TRUE;
		assert(imgp->ip_new_thread != NULL);

		/* meizu: execve gives pid 1 a new task and thread (cloneproc
		 * CLONEPROC_EXEC). Re-latch so the init probes keep following it;
		 * the held reference keeps the pointer from dangling. */
		if (proc_getpid(p) == 1) {
			extern thread_t meizu_init_thread;
			extern task_t meizu_init_task;
			thread_reference(imgp->ip_new_thread);
			meizu_init_thread = imgp->ip_new_thread;
			meizu_init_task = new_task;
			printf("meizu: init relatch thread=%p task=%p\n",
			    (void *)imgp->ip_new_thread, (void *)new_task);
		}

		exec_resettextvp(p, imgp);
		/*
		 * Enable new task IPC access if exec_activate_image() returned an
		 * active task. (Checks active bit in ipc_task_enable() under lock).
		 * Must enable after resettextvp so that task port policies are not evaluated
		 * until the csblob in the textvp is accurately reflected.
		 */
		ipc_task_enable(new_task);
		error = process_signature(p, imgp);
	}

#if defined(HAS_APPLE_PAC)
	if (imgp->ip_new_thread && !error) {
		ml_task_set_jop_pid_from_shared_region(new_task, imgp->ip_flags & IMGPF_NOJOP);
		ml_thread_set_jop_pid(imgp->ip_new_thread, new_task);
	}
#endif /* defined(HAS_APPLE_PAC) */

	/* flag exec has occurred, notify only if it has not failed due to FP Key error */
	if (exec_done && ((p->p_lflag & P_LTERM_DECRYPTFAIL) == 0)) {
		proc_knote(p, NOTE_EXEC);
	}

	if (imgp->ip_vp != NULLVP) {
		vnode_put(imgp->ip_vp);
	}
	if (imgp->ip_scriptvp != NULLVP) {
		vnode_put(imgp->ip_scriptvp);
	}
	if (imgp->ip_free_map) {
		/* Free the map after dropping iocount on vnode to avoid deadlock */
		vm_map_deallocate(imgp->ip_free_map);
	}
	if (imgp->ip_strings) {
		execargs_free(imgp);
	}
#if CONFIG_MACF
	if (imgp->ip_execlabelp) {
		mac_cred_label_free(imgp->ip_execlabelp);
		imgp->ip_execlabelp = NULL;
	}
	if (imgp->ip_scriptlabelp) {
		mac_vnode_label_free(imgp->ip_scriptlabelp);
		imgp->ip_scriptlabelp = NULL;
	}
#endif
	if (imgp->ip_cs_error != OS_REASON_NULL) {
		os_reason_free(imgp->ip_cs_error);
		imgp->ip_cs_error = OS_REASON_NULL;
	}

	if (!error) {
		/*
		 * We need to initialize the bank context behind the protection of
		 * the proc_trans lock to prevent a race with exit. We can't do this during
		 * exec_activate_image because task_bank_init checks entitlements that
		 * aren't loaded until subsequent calls (including exec_resettextvp).
		 */
		error = proc_transstart(p, 0, 0);
	}

	if (!error) {
		task_bank_init(new_task);
		proc_transend(p, 0);

		// Don't inherit crash behavior across exec
		p->p_crash_behavior = 0;
		p->p_crash_behavior_deadline = 0;

#if __arm64__
		proc_footprint_entitlement_hacks(p, new_task);
#endif /* __arm64__ */

#if XNU_TARGET_OS_OSX
		if (IOTaskHasEntitlement(new_task, SINGLE_JIT_ENTITLEMENT)) {
			vm_map_single_jit(get_task_map(new_task));
		}
#endif /* XNU_TARGET_OS_OSX */

		/* Sever any extant thread affinity */
		thread_affinity_exec(current_thread());

		/* Inherit task role from old task to new task for exec */
		proc_inherit_task_role(new_task, old_task);

		thread_t main_thread = imgp->ip_new_thread;

		task_set_main_thread_qos(new_task, main_thread);

#if __has_feature(ptrauth_calls)
		task_set_pac_exception_fatal_flag(new_task);
#endif /* __has_feature(ptrauth_calls) */
		task_set_jit_exception_fatal_flag(new_task);

#if CONFIG_ARCADE
		/*
		 * Check to see if we need to trigger an arcade upcall AST now
		 * that the vnode has been reset on the task.
		 */
		arcade_prepare(new_task, imgp->ip_new_thread);
#endif /* CONFIG_ARCADE */

		proc_apply_jit_and_vm_policies(imgp, p, new_task);

		if (vm_darkwake_mode == TRUE) {
			/*
			 * This process is being launched when the system
			 * is in darkwake. So mark it specially. This will
			 * cause all its pages to be entered in the background Q.
			 */
			task_set_darkwake_mode(new_task, vm_darkwake_mode);
		}

#if CONFIG_DTRACE
		dtrace_thread_didexec(imgp->ip_new_thread);

		if ((dtrace_proc_waitfor_hook = dtrace_proc_waitfor_exec_ptr) != NULL) {
			(*dtrace_proc_waitfor_hook)(p);
		}
#endif

#if CONFIG_AUDIT
		if (!error && AUDIT_ENABLED() && p) {
			/* Add the CDHash of the new process to the audit record */
			uint8_t *cdhash = cs_get_cdhash(p);
			if (cdhash) {
				AUDIT_ARG(data, cdhash, sizeof(uint8_t), CS_CDHASH_LEN);
			}
		}
#endif
	} else {
		DTRACE_PROC1(exec__failure, int, error);
	}

exit_with_error:

	/* terminate the new task it if exec failed  */
	if (new_task != NULL && task_is_exec_copy(new_task)) {
		task_terminate_internal(new_task);
	}

	if (imgp != NULL) {
		/* Clear the initial wait on the thread transferring watchports */
		if (imgp->ip_new_thread) {
			task_clear_return_wait(get_threadtask(imgp->ip_new_thread), TCRW_CLEAR_INITIAL_WAIT);
		}

		/* Transfer the watchport boost to new task */
		if (!error) {
			task_transfer_turnstile_watchports(old_task,
			    new_task, imgp->ip_new_thread);
		}
		/*
		 * Do not terminate the current task, if proc_exec_switch_task did not
		 * switch the tasks, terminating the current task without the switch would
		 * result in loosing the SIGKILL status.
		 */
		if (task_did_exec(old_task)) {
			/* Terminate the current task, since exec will start in new task */
			task_terminate_internal(old_task);
		}

		/* Release the thread ref returned by cloneproc */
		if (imgp->ip_new_thread) {
			/* clear the exec complete flag if there is an error before point of no-return */
			uint32_t clearwait_flags = TCRW_CLEAR_FINAL_WAIT;
			if (!exec_done && error != 0) {
				clearwait_flags |= TCRW_CLEAR_EXEC_COMPLETE;
			}
#if defined(XNU_OWNER_CACHE_MOUNT) && XNU_OWNER_CACHE_MOUNT
			if (error == 0 && exec_done && !(p->p_lflag & P_LTERM_DECRYPTFAIL)) {
				meizu_exec_capture(p, imgp->ip_new_thread);
				if (strcmp(p->p_comm, "SpringBoard") == 0) {
					meizu_sb_capture(p, imgp->ip_new_thread);
				}
			}
#endif
			/* wake up the new exec thread */
			task_clear_return_wait(get_threadtask(imgp->ip_new_thread), clearwait_flags);
			thread_deallocate(imgp->ip_new_thread);
			imgp->ip_new_thread = NULL;
		}
	}

	/* Release the ref returned by fork_create_child */
	if (new_task) {
		task_deallocate(new_task);
		new_task = NULL;
	}

	if (should_release_proc_ref) {
		proc_rele(p);
	}

	uthread_set_exec_data(current_uthread(), NULL);
	kfree_type(typeof(*__execve_data), __execve_data);

	if (inherit != NULL) {
		ipc_importance_release(inherit);
	}

	return error;
}


/*
 * copyinptr
 *
 * Description:	Copy a pointer in from user space to a user_addr_t in kernel
 *		space, based on 32/64 bitness of the user space
 *
 * Parameters:	froma			User space address
 *		toptr			Address of kernel space user_addr_t
 *		ptr_size		4/8, based on 'froma' address space
 *
 * Returns:	0			Success
 *		EFAULT			Bad 'froma'
 *
 * Implicit returns:
 *		*ptr_size		Modified
 */
static int
copyinptr(user_addr_t froma, user_addr_t *toptr, int ptr_size)
{
	int error;

	if (ptr_size == 4) {
		/* 64 bit value containing 32 bit address */
		unsigned int i = 0;

		error = copyin(froma, &i, 4);
		*toptr = CAST_USER_ADDR_T(i);   /* SAFE */
	} else {
		error = copyin(froma, toptr, 8);
	}
	return error;
}


/*
 * copyoutptr
 *
 * Description:	Copy a pointer out from a user_addr_t in kernel space to
 *		user space, based on 32/64 bitness of the user space
 *
 * Parameters:	ua			User space address to copy to
 *		ptr			Address of kernel space user_addr_t
 *		ptr_size		4/8, based on 'ua' address space
 *
 * Returns:	0			Success
 *		EFAULT			Bad 'ua'
 *
 */
static int
copyoutptr(user_addr_t ua, user_addr_t ptr, int ptr_size)
{
	int error;

	if (ptr_size == 4) {
		/* 64 bit value containing 32 bit address */
		unsigned int i = CAST_DOWN_EXPLICIT(unsigned int, ua);   /* SAFE */

		error = copyout(&i, ptr, 4);
	} else {
		error = copyout(&ua, ptr, 8);
	}
	return error;
}


/*
 * exec_copyout_strings
 *
 * Copy out the strings segment to user space.  The strings segment is put
 * on a preinitialized stack frame.
 *
 * Parameters:	struct image_params *	the image parameter block
 *		int *			a pointer to the stack offset variable
 *
 * Returns:	0			Success
 *		!0			Faiure: errno
 *
 * Implicit returns:
 *		(*stackp)		The stack offset, modified
 *
 * Note:	The strings segment layout is backward, from the beginning
 *		of the top of the stack to consume the minimal amount of
 *		space possible; the returned stack pointer points to the
 *		end of the area consumed (stacks grow downward).
 *
 *		argc is an int; arg[i] are pointers; env[i] are pointers;
 *		the 0's are (void *)NULL's
 *
 * The stack frame layout is:
 *
 *      +-------------+ <- p->user_stack
 *      |     16b     |
 *      +-------------+
 *      | STRING AREA |
 *      |      :      |
 *      |      :      |
 *      |      :      |
 *      +- -- -- -- --+
 *      |  PATH AREA  |
 *      +-------------+
 *      |      0      |
 *      +-------------+
 *      |  applev[n]  |
 *      +-------------+
 *             :
 *             :
 *      +-------------+
 *      |  applev[1]  |
 *      +-------------+
 *      | exec_path / |
 *      |  applev[0]  |
 *      +-------------+
 *      |      0      |
 *      +-------------+
 *      |    env[n]   |
 *      +-------------+
 *             :
 *             :
 *      +-------------+
 *      |    env[0]   |
 *      +-------------+
 *      |      0      |
 *      +-------------+
 *      | arg[argc-1] |
 *      +-------------+
 *             :
 *             :
 *      +-------------+
 *      |    arg[0]   |
 *      +-------------+
 *      |     argc    |
 * sp-> +-------------+
 *
 * Although technically a part of the STRING AREA, we treat the PATH AREA as
 * a separate entity.  This allows us to align the beginning of the PATH AREA
 * to a pointer boundary so that the exec_path, env[i], and argv[i] pointers
 * which preceed it on the stack are properly aligned.
 */
__attribute__((noinline))
static int
exec_copyout_strings(struct image_params *imgp, user_addr_t *stackp)
{
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);
	int     ptr_size = (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) ? 8 : 4;
	int     ptr_area_size;
	void *ptr_buffer_start, *ptr_buffer;
	size_t string_size;

	user_addr_t     string_area;    /* *argv[], *env[] */
	user_addr_t     ptr_area;       /* argv[], env[], applev[] */
	user_addr_t argc_area;  /* argc */
	user_addr_t     stack;
	int error;

	unsigned i;
	struct copyout_desc {
		char    *start_string;
		int             count;
#if CONFIG_DTRACE
		user_addr_t     *dtrace_cookie;
#endif
		boolean_t       null_term;
	} descriptors[] = {
		{
			.start_string = imgp->ip_startargv,
			.count = imgp->ip_argc,
#if CONFIG_DTRACE
			.dtrace_cookie = &p->p_dtrace_argv,
#endif
			.null_term = TRUE
		},
		{
			.start_string = imgp->ip_endargv,
			.count = imgp->ip_envc,
#if CONFIG_DTRACE
			.dtrace_cookie = &p->p_dtrace_envp,
#endif
			.null_term = TRUE
		},
		{
			.start_string = imgp->ip_strings,
			.count = 1,
#if CONFIG_DTRACE
			.dtrace_cookie = NULL,
#endif
			.null_term = FALSE
		},
		{
			.start_string = imgp->ip_endenvv,
			.count = imgp->ip_applec - 1, /* exec_path handled above */
#if CONFIG_DTRACE
			.dtrace_cookie = NULL,
#endif
			.null_term = TRUE
		}
	};

	stack = *stackp;

	/*
	 * All previous contributors to the string area
	 * should have aligned their sub-area
	 */
	if (imgp->ip_strspace % ptr_size != 0) {
		error = EINVAL;
		goto bad;
	}

	/* Grow the stack down for the strings we've been building up */
	string_size = imgp->ip_strendp - imgp->ip_strings;
	stack -= string_size;
	string_area = stack;

	/*
	 * Need room for one pointer for each string, plus
	 * one for the NULLs terminating the argv, envv, and apple areas.
	 */
	ptr_area_size = (imgp->ip_argc + imgp->ip_envc + imgp->ip_applec + 3) * ptr_size;
	stack -= ptr_area_size;
	ptr_area = stack;

	/* We'll construct all the pointer arrays in our string buffer,
	 * which we already know is aligned properly, and ip_argspace
	 * was used to verify we have enough space.
	 */
	ptr_buffer_start = ptr_buffer = (void *)imgp->ip_strendp;

	/*
	 * Need room for pointer-aligned argc slot.
	 */
	stack -= ptr_size;
	argc_area = stack;

	/*
	 * Record the size of the arguments area so that sysctl_procargs()
	 * can return the argument area without having to parse the arguments.
	 */
	proc_lock(p);
	p->p_argc = imgp->ip_argc;
	p->p_argslen = (int)(*stackp - string_area);
	proc_unlock(p);

	/* Return the initial stack address: the location of argc */
	*stackp = stack;

	/*
	 * Copy out the entire strings area.
	 */
	error = copyout(imgp->ip_strings, string_area,
	    string_size);
	if (error) {
		goto bad;
	}

	for (i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); i++) {
		char *cur_string = descriptors[i].start_string;
		int j;

#if CONFIG_DTRACE
		if (descriptors[i].dtrace_cookie) {
			proc_lock(p);
			*descriptors[i].dtrace_cookie = ptr_area + ((uintptr_t)ptr_buffer - (uintptr_t)ptr_buffer_start); /* dtrace convenience */
			proc_unlock(p);
		}
#endif /* CONFIG_DTRACE */

		/*
		 * For each segment (argv, envv, applev), copy as many pointers as requested
		 * to our pointer buffer.
		 */
		for (j = 0; j < descriptors[i].count; j++) {
			user_addr_t cur_address = string_area + (cur_string - imgp->ip_strings);

			/* Copy out the pointer to the current string. Alignment has been verified  */
			if (ptr_size == 8) {
				*(uint64_t *)ptr_buffer = (uint64_t)cur_address;
			} else {
				*(uint32_t *)ptr_buffer = (uint32_t)cur_address;
			}

			ptr_buffer = (void *)((uintptr_t)ptr_buffer + ptr_size);
			cur_string += strlen(cur_string) + 1; /* Only a NUL between strings in the same area */
		}

		if (descriptors[i].null_term) {
			if (ptr_size == 8) {
				*(uint64_t *)ptr_buffer = 0ULL;
			} else {
				*(uint32_t *)ptr_buffer = 0;
			}

			ptr_buffer = (void *)((uintptr_t)ptr_buffer + ptr_size);
		}
	}

	/*
	 * Copy out all our pointer arrays in bulk.
	 */
	error = copyout(ptr_buffer_start, ptr_area,
	    ptr_area_size);
	if (error) {
		goto bad;
	}

	/* argc (int32, stored in a ptr_size area) */
	error = copyoutptr((user_addr_t)imgp->ip_argc, argc_area, ptr_size);
	if (error) {
		goto bad;
	}

bad:
	return error;
}


/*
 * exec_extract_strings
 *
 * Copy arguments and environment from user space into work area; we may
 * have already copied some early arguments into the work area, and if
 * so, any arguments opied in are appended to those already there.
 * This function is the primary manipulator of ip_argspace, since
 * these are the arguments the client of execve(2) knows about. After
 * each argv[]/envv[] string is copied, we charge the string length
 * and argv[]/envv[] pointer slot to ip_argspace, so that we can
 * full preflight the arg list size.
 *
 * Parameters:	struct image_params *	the image parameter block
 *
 * Returns:	0			Success
 *		!0			Failure: errno
 *
 * Implicit returns;
 *		(imgp->ip_argc)		Count of arguments, updated
 *		(imgp->ip_envc)		Count of environment strings, updated
 *		(imgp->ip_argspace)	Count of remaining of NCARGS
 *		(imgp->ip_interp_buffer)	Interpreter and args (mutated in place)
 *
 *
 * Note:	The argument and environment vectors are user space pointers
 *		to arrays of user space pointers.
 */
__attribute__((noinline))
static int
exec_extract_strings(struct image_params *imgp)
{
	int error = 0;
	int     ptr_size = (imgp->ip_flags & IMGPF_WAS_64BIT_ADDR) ? 8 : 4;
	int new_ptr_size = (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) ? 8 : 4;
	user_addr_t     argv = imgp->ip_user_argv;
	user_addr_t     envv = imgp->ip_user_envv;

	/*
	 * Adjust space reserved for the path name by however much padding it
	 * needs. Doing this here since we didn't know if this would be a 32-
	 * or 64-bit process back in exec_save_path.
	 */
	while (imgp->ip_strspace % new_ptr_size != 0) {
		*imgp->ip_strendp++ = '\0';
		imgp->ip_strspace--;
		/* imgp->ip_argspace--; not counted towards exec args total */
	}

	/*
	 * From now on, we start attributing string space to ip_argspace
	 */
	imgp->ip_startargv = imgp->ip_strendp;
	imgp->ip_argc = 0;

	if ((imgp->ip_flags & IMGPF_INTERPRET) != 0) {
		user_addr_t     arg;
		char *argstart, *ch;

		/* First, the arguments in the "#!" string are tokenized and extracted. */
		argstart = imgp->ip_interp_buffer;
		while (argstart) {
			ch = argstart;
			while (*ch && !IS_WHITESPACE(*ch)) {
				ch++;
			}

			if (*ch == '\0') {
				/* last argument, no need to NUL-terminate */
				error = exec_add_user_string(imgp, CAST_USER_ADDR_T(argstart), UIO_SYSSPACE, TRUE);
				argstart = NULL;
			} else {
				/* NUL-terminate */
				*ch = '\0';
				error = exec_add_user_string(imgp, CAST_USER_ADDR_T(argstart), UIO_SYSSPACE, TRUE);

				/*
				 * Find the next string. We know spaces at the end of the string have already
				 * been stripped.
				 */
				argstart = ch + 1;
				while (IS_WHITESPACE(*argstart)) {
					argstart++;
				}
			}

			/* Error-check, regardless of whether this is the last interpreter arg or not */
			if (error) {
				goto bad;
			}
			if (imgp->ip_argspace < new_ptr_size) {
				error = E2BIG;
				goto bad;
			}
			imgp->ip_argspace -= new_ptr_size; /* to hold argv[] entry */
			imgp->ip_argc++;
		}

		if (argv != 0LL) {
			/*
			 * If we are running an interpreter, replace the av[0] that was
			 * passed to execve() with the path name that was
			 * passed to execve() for interpreters which do not use the PATH
			 * to locate their script arguments.
			 */
			error = copyinptr(argv, &arg, ptr_size);
			if (error) {
				goto bad;
			}
			if (arg != 0LL) {
				argv += ptr_size; /* consume without using */
			}
		}

		if (imgp->ip_interp_sugid_fd != -1) {
			char temp[19]; /* "/dev/fd/" + 10 digits + NUL */
			snprintf(temp, sizeof(temp), "/dev/fd/%d", imgp->ip_interp_sugid_fd);
			error = exec_add_user_string(imgp, CAST_USER_ADDR_T(temp), UIO_SYSSPACE, TRUE);
		} else {
			error = exec_add_user_string(imgp, imgp->ip_user_fname, imgp->ip_seg, TRUE);
		}

		if (error) {
			goto bad;
		}
		if (imgp->ip_argspace < new_ptr_size) {
			error = E2BIG;
			goto bad;
		}
		imgp->ip_argspace -= new_ptr_size; /* to hold argv[] entry */
		imgp->ip_argc++;
	}

	while (argv != 0LL) {
		user_addr_t     arg;

		error = copyinptr(argv, &arg, ptr_size);
		if (error) {
			goto bad;
		}

		if (arg == 0LL) {
			break;
		}

		argv += ptr_size;

		/*
		 * av[n...] = arg[n]
		 */
		error = exec_add_user_string(imgp, arg, imgp->ip_seg, TRUE);
		if (error) {
			goto bad;
		}
		if (imgp->ip_argspace < new_ptr_size) {
			error = E2BIG;
			goto bad;
		}
		imgp->ip_argspace -= new_ptr_size; /* to hold argv[] entry */
		imgp->ip_argc++;
	}

	/* Save space for argv[] NULL terminator */
	if (imgp->ip_argspace < new_ptr_size) {
		error = E2BIG;
		goto bad;
	}
	imgp->ip_argspace -= new_ptr_size;

	/* Note where the args ends and env begins. */
	imgp->ip_endargv = imgp->ip_strendp;
	imgp->ip_envc = 0;

	/* Now, get the environment */
	while (envv != 0LL) {
		user_addr_t     env;

		error = copyinptr(envv, &env, ptr_size);
		if (error) {
			goto bad;
		}

		envv += ptr_size;
		if (env == 0LL) {
			break;
		}
		/*
		 * av[n...] = env[n]
		 */
		error = exec_add_user_string(imgp, env, imgp->ip_seg, TRUE);
		if (error) {
			goto bad;
		}
		if (imgp->ip_argspace < new_ptr_size) {
			error = E2BIG;
			goto bad;
		}
		imgp->ip_argspace -= new_ptr_size; /* to hold envv[] entry */
		imgp->ip_envc++;
	}

	/* Save space for envv[] NULL terminator */
	if (imgp->ip_argspace < new_ptr_size) {
		error = E2BIG;
		goto bad;
	}
	imgp->ip_argspace -= new_ptr_size;

	/* Align the tail of the combined argv+envv area */
	while (imgp->ip_strspace % new_ptr_size != 0) {
		if (imgp->ip_argspace < 1) {
			error = E2BIG;
			goto bad;
		}
		*imgp->ip_strendp++ = '\0';
		imgp->ip_strspace--;
		imgp->ip_argspace--;
	}

	/* Note where the envv ends and applev begins. */
	imgp->ip_endenvv = imgp->ip_strendp;

	/*
	 * From now on, we are no longer charging argument
	 * space to ip_argspace.
	 */

bad:
	return error;
}

/*
 * Libc has an 8-element array set up for stack guard values.  It only fills
 * in one of those entries, and both gcc and llvm seem to use only a single
 * 8-byte guard.  Until somebody needs more than an 8-byte guard value, don't
 * do the work to construct them.
 */
#define GUARD_VALUES 1
#define GUARD_KEY "stack_guard="

/*
 * System malloc needs some entropy when it is initialized.
 */
#define ENTROPY_VALUES 2
#define ENTROPY_KEY "malloc_entropy="

/*
 * libplatform needs a random pointer-obfuscation value when it is initialized.
 */
#define PTR_MUNGE_VALUES 1
#define PTR_MUNGE_KEY "ptr_munge="

/*
 * System malloc engages nanozone for UIAPP.
 */
#define NANO_ENGAGE_KEY "MallocNanoZone=1"

/*
 * Used to pass experiment flags up to libmalloc.
 */
#define LIBMALLOC_EXPERIMENT_FACTORS_KEY "MallocExperiment="

/*
 * Passes information about hardened runtime entitlements to libsystem/libmalloc
 */
#define HARDENED_RUNTIME_KEY "HardenedRuntime="

#define PFZ_KEY "pfz="
extern user32_addr_t commpage_text32_location;
extern user64_addr_t commpage_text64_location;

extern uuid_string_t bootsessionuuid_string;
static TUNABLE(uint32_t, exe_boothash_salt, "exe_boothash_salt", 0);

__startup_func
static void
exe_boothash_salt_generate(void)
{
	if (!PE_parse_boot_argn("exe_boothash_salt", NULL, 0)) {
		read_random(&exe_boothash_salt, sizeof(exe_boothash_salt));
	}
}
STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE, exe_boothash_salt_generate);


#define MAIN_STACK_VALUES 4
#define MAIN_STACK_KEY "main_stack="

#define FSID_KEY "executable_file="
#define DYLD_FSID_KEY "dyld_file="
#define CDHASH_KEY "executable_cdhash="
#define DYLD_FLAGS_KEY "dyld_flags="
#define SUBSYSTEM_ROOT_PATH_KEY "subsystem_root_path="
#define APP_BOOT_SESSION_KEY "executable_boothash="
#if __has_feature(ptrauth_calls)
#define PTRAUTH_DISABLED_FLAG "ptrauth_disabled=1"
#define DYLD_ARM64E_ABI_KEY "arm64e_abi="
#endif /* __has_feature(ptrauth_calls) */
#define MAIN_TH_PORT_KEY "th_port="

#define FSID_MAX_STRING "0x1234567890abcdef,0x1234567890abcdef"

#define HEX_STR_LEN 18 // 64-bit hex value "0x0123456701234567"
#define HEX_STR_LEN32 10 // 32-bit hex value "0x01234567"

#if XNU_TARGET_OS_OSX && _POSIX_SPAWN_FORCE_4K_PAGES && PMAP_CREATE_FORCE_4K_PAGES
#define VM_FORCE_4K_PAGES_KEY "vm_force_4k_pages=1"
#endif /* XNU_TARGET_OS_OSX && _POSIX_SPAWN_FORCE_4K_PAGES && PMAP_CREATE_FORCE_4K_PAGES */

static int
exec_add_entropy_key(struct image_params *imgp,
    const char *key,
    int values,
    boolean_t embedNUL)
{
	const int limit = 8;
	uint64_t entropy[limit];
	char str[strlen(key) + (HEX_STR_LEN + 1) * limit + 1];
	if (values > limit) {
		values = limit;
	}

	read_random(entropy, sizeof(entropy[0]) * values);

	if (embedNUL) {
		entropy[0] &= ~(0xffull << 8);
	}

	int len = scnprintf(str, sizeof(str), "%s0x%llx", key, entropy[0]);
	size_t remaining = sizeof(str) - len;
	for (int i = 1; i < values && remaining > 0; ++i) {
		size_t start = sizeof(str) - remaining;
		len = scnprintf(&str[start], remaining, ",0x%llx", entropy[i]);
		remaining -= len;
	}

	return exec_add_user_string(imgp, CAST_USER_ADDR_T(str), UIO_SYSSPACE, FALSE);
}

/*
 * Build up the contents of the apple[] string vector
 */
#if (DEVELOPMENT || DEBUG)
extern uint64_t dyld_flags;
#endif

#if __has_feature(ptrauth_calls)
static inline bool
is_arm64e_running_as_arm64(const struct image_params *imgp)
{
	return (imgp->ip_origcpusubtype & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E &&
	       (imgp->ip_flags & IMGPF_NOJOP);
}
#endif /* __has_feature(ptrauth_calls) */

_Atomic uint64_t libmalloc_experiment_factors = 0;

static int
exec_add_apple_strings(struct image_params *imgp,
    const load_result_t *load_result)
{
	int error;
	int img_ptr_size = (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) ? 8 : 4;
	thread_t new_thread;
	ipc_port_t sright;
	uint64_t local_experiment_factors = 0;

	/* exec_save_path stored the first string */
	imgp->ip_applec = 1;

	/* adding the pfz string */
	{
		char pfz_string[strlen(PFZ_KEY) + HEX_STR_LEN + 1];

		if (img_ptr_size == 8) {
			__assert_only size_t ret = snprintf(pfz_string, sizeof(pfz_string), PFZ_KEY "0x%llx", commpage_text64_location);
			assert(ret < sizeof(pfz_string));
		} else {
			snprintf(pfz_string, sizeof(pfz_string), PFZ_KEY "0x%x", commpage_text32_location);
		}
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(pfz_string), UIO_SYSSPACE, FALSE);
		if (error) {
			printf("Failed to add the pfz string with error %d\n", error);
			goto bad;
		}
		imgp->ip_applec++;
	}

	/* adding the NANO_ENGAGE_KEY key */
	if (imgp->ip_px_sa) {
		struct _posix_spawnattr* psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
		int proc_flags = psa->psa_flags;

		if ((proc_flags & _POSIX_SPAWN_NANO_ALLOCATOR) == _POSIX_SPAWN_NANO_ALLOCATOR) {
			const char *nano_string = NANO_ENGAGE_KEY;
			error = exec_add_user_string(imgp, CAST_USER_ADDR_T(nano_string), UIO_SYSSPACE, FALSE);
			if (error) {
				goto bad;
			}
			imgp->ip_applec++;
		}
	}

	/*
	 * Supply libc with a collection of random values to use when
	 * implementing -fstack-protector.
	 *
	 * (The first random string always contains an embedded NUL so that
	 * __stack_chk_guard also protects against C string vulnerabilities)
	 */
	error = exec_add_entropy_key(imgp, GUARD_KEY, GUARD_VALUES, TRUE);
	if (error) {
		goto bad;
	}
	imgp->ip_applec++;

	/*
	 * Supply libc with entropy for system malloc.
	 */
	error = exec_add_entropy_key(imgp, ENTROPY_KEY, ENTROPY_VALUES, FALSE);
	if (error) {
		goto bad;
	}
	imgp->ip_applec++;

	/*
	 * Supply libpthread & libplatform with a random value to use for pointer
	 * obfuscation.
	 */
	error = exec_add_entropy_key(imgp, PTR_MUNGE_KEY, PTR_MUNGE_VALUES, FALSE);
	if (error) {
		goto bad;
	}
	imgp->ip_applec++;

	/*
	 * Add MAIN_STACK_KEY: Supplies the address and size of the main thread's
	 * stack if it was allocated by the kernel.
	 *
	 * The guard page is not included in this stack size as libpthread
	 * expects to add it back in after receiving this value.
	 */
	if (load_result->unixproc) {
		char stack_string[strlen(MAIN_STACK_KEY) + (HEX_STR_LEN + 1) * MAIN_STACK_VALUES + 1];
		snprintf(stack_string, sizeof(stack_string),
		    MAIN_STACK_KEY "0x%llx,0x%llx,0x%llx,0x%llx",
		    (uint64_t)load_result->user_stack,
		    (uint64_t)load_result->user_stack_size,
		    (uint64_t)load_result->user_stack_alloc,
		    (uint64_t)load_result->user_stack_alloc_size);
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(stack_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}

	if (imgp->ip_vattr) {
		uint64_t fsid    = vnode_get_va_fsid(imgp->ip_vattr);
		uint64_t fsobjid = imgp->ip_vattr->va_fileid;

		char fsid_string[strlen(FSID_KEY) + strlen(FSID_MAX_STRING) + 1];
		snprintf(fsid_string, sizeof(fsid_string),
		    FSID_KEY "0x%llx,0x%llx", fsid, fsobjid);
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(fsid_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}

	if (imgp->ip_dyld_fsid || imgp->ip_dyld_fsobjid) {
		char fsid_string[strlen(DYLD_FSID_KEY) + strlen(FSID_MAX_STRING) + 1];
		snprintf(fsid_string, sizeof(fsid_string),
		    DYLD_FSID_KEY "0x%llx,0x%llx", imgp->ip_dyld_fsid, imgp->ip_dyld_fsobjid);
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(fsid_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}

	uint8_t cdhash[SHA1_RESULTLEN];
	int cdhash_errror = ubc_cs_getcdhash(imgp->ip_vp, imgp->ip_arch_offset, cdhash);
	if (cdhash_errror == 0) {
		char hash_string[strlen(CDHASH_KEY) + 2 * SHA1_RESULTLEN + 1];
		strncpy(hash_string, CDHASH_KEY, sizeof(hash_string));
		char *p = hash_string + sizeof(CDHASH_KEY) - 1;
		for (int i = 0; i < SHA1_RESULTLEN; i++) {
			snprintf(p, 3, "%02x", (int) cdhash[i]);
			p += 2;
		}
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(hash_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;

		/* hash together cd-hash and boot-session-uuid */
		uint8_t sha_digest[SHA256_DIGEST_LENGTH];
		SHA256_CTX sha_ctx;
		SHA256_Init(&sha_ctx);
		SHA256_Update(&sha_ctx, &exe_boothash_salt, sizeof(exe_boothash_salt));
		SHA256_Update(&sha_ctx, bootsessionuuid_string, sizeof(bootsessionuuid_string));
		SHA256_Update(&sha_ctx, cdhash, sizeof(cdhash));
		SHA256_Final(sha_digest, &sha_ctx);
		char app_boot_string[strlen(APP_BOOT_SESSION_KEY) + 2 * SHA1_RESULTLEN + 1];
		strncpy(app_boot_string, APP_BOOT_SESSION_KEY, sizeof(app_boot_string));
		char *s = app_boot_string + sizeof(APP_BOOT_SESSION_KEY) - 1;
		for (int i = 0; i < SHA1_RESULTLEN; i++) {
			snprintf(s, 3, "%02x", (int) sha_digest[i]);
			s += 2;
		}
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(app_boot_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}
#if (DEVELOPMENT || DEBUG)
	if (dyld_flags) {
		char dyld_flags_string[strlen(DYLD_FLAGS_KEY) + HEX_STR_LEN + 1];
		snprintf(dyld_flags_string, sizeof(dyld_flags_string), DYLD_FLAGS_KEY "0x%llx", dyld_flags);
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(dyld_flags_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}
#endif
	if (imgp->ip_subsystem_root_path) {
		size_t buffer_len = MAXPATHLEN + strlen(SUBSYSTEM_ROOT_PATH_KEY);
		char subsystem_root_path_string[buffer_len];
		int required_len = snprintf(subsystem_root_path_string, buffer_len, SUBSYSTEM_ROOT_PATH_KEY "%s", imgp->ip_subsystem_root_path);

		if (((size_t)required_len >= buffer_len) || (required_len < 0)) {
			error = ENAMETOOLONG;
			goto bad;
		}

		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(subsystem_root_path_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}

		imgp->ip_applec++;
	}
#if __has_feature(ptrauth_calls)
	if (is_arm64e_running_as_arm64(imgp)) {
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(PTRAUTH_DISABLED_FLAG), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}

		imgp->ip_applec++;
	}
#endif /* __has_feature(ptrauth_calls) */


#if __has_feature(ptrauth_calls) && defined(XNU_TARGET_OS_OSX)
	{
		char dyld_abi_string[strlen(DYLD_ARM64E_ABI_KEY) + 8];
		strlcpy(dyld_abi_string, DYLD_ARM64E_ABI_KEY, sizeof(dyld_abi_string));
		bool allowAll = bootarg_arm64e_preview_abi;
		strlcat(dyld_abi_string, (allowAll ? "all" : "os"), sizeof(dyld_abi_string));
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(dyld_abi_string), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}

		imgp->ip_applec++;
	}
#endif
	/*
	 * Add main thread mach port name
	 * +1 uref on main thread port, this ref will be extracted by libpthread in __pthread_init
	 * and consumed in _bsdthread_terminate. Leaking the main thread port name if not linked
	 * against libpthread.
	 */
	if ((new_thread = imgp->ip_new_thread) != THREAD_NULL) {
		thread_reference(new_thread);
		sright = convert_thread_to_port_pinned(new_thread);
		task_t new_task = get_threadtask(new_thread);
		mach_port_name_t name = ipc_port_copyout_send(sright, get_task_ipcspace(new_task));
		char port_name_hex_str[strlen(MAIN_TH_PORT_KEY) + HEX_STR_LEN32 + 1];
		snprintf(port_name_hex_str, sizeof(port_name_hex_str), MAIN_TH_PORT_KEY "0x%x", name);

		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(port_name_hex_str), UIO_SYSSPACE, FALSE);
		if (error) {
			goto bad;
		}
		imgp->ip_applec++;
	}

#if XNU_TARGET_OS_OSX && _POSIX_SPAWN_FORCE_4K_PAGES && PMAP_CREATE_FORCE_4K_PAGES
	if (imgp->ip_px_sa != NULL) {
		struct _posix_spawnattr* psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
		if (psa->psa_flags & _POSIX_SPAWN_FORCE_4K_PAGES) {
			const char *vm_force_4k_string = VM_FORCE_4K_PAGES_KEY;
			error = exec_add_user_string(imgp, CAST_USER_ADDR_T(vm_force_4k_string), UIO_SYSSPACE, FALSE);
			if (error) {
				goto bad;
			}
			imgp->ip_applec++;
		}
	}
#endif /* XNU_TARGET_OS_OSX && _POSIX_SPAWN_FORCE_4K_PAGES && PMAP_CREATE_FORCE_4K_PAGES */

	/* adding the libmalloc experiment string */
	local_experiment_factors = os_atomic_load_wide(&libmalloc_experiment_factors, relaxed);
	if (__improbable(local_experiment_factors != 0)) {
		char libmalloc_experiment_factors_string[strlen(LIBMALLOC_EXPERIMENT_FACTORS_KEY) + HEX_STR_LEN + 1];

		snprintf(
			libmalloc_experiment_factors_string,
			sizeof(libmalloc_experiment_factors_string),
			LIBMALLOC_EXPERIMENT_FACTORS_KEY "0x%llx",
			local_experiment_factors);
		error = exec_add_user_string(
			imgp,
			CAST_USER_ADDR_T(libmalloc_experiment_factors_string),
			UIO_SYSSPACE,
			FALSE);
		if (error) {
			printf("Failed to add the libmalloc experiment factors string with error %d\n", error);
			goto bad;
		}
		imgp->ip_applec++;
	}


	/* tell dyld that it can leverage hardware for its read-only/read-write trusted path */
	if (imgp->ip_flags & IMGPF_HW_TPRO) {
		const char *dyld_hw_tpro = "dyld_hw_tpro=1";
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(dyld_hw_tpro), UIO_SYSSPACE, FALSE);
		if (error) {
			printf("Failed to add dyld hw tpro setting with error %d\n", error);
			goto bad;
		}

		imgp->ip_applec++;

	}

	if (load_result->hardened_runtime_binary) {
		const size_t HR_STRING_SIZE = sizeof(HARDENED_RUNTIME_KEY) + HR_FLAGS_NUM_NIBBLES + 2 + 1;
		char hardened_runtime[HR_STRING_SIZE];
		snprintf(hardened_runtime, HR_STRING_SIZE, HARDENED_RUNTIME_KEY"0x%x", load_result->hardened_runtime_binary);
		error = exec_add_user_string(imgp, CAST_USER_ADDR_T(hardened_runtime), UIO_SYSSPACE, FALSE);
		if (error) {
			printf("Failed to add hardened runtime flag with error %d\n", error);
			goto bad;
		}
		imgp->ip_applec++;
	}
	/* Align the tail of the combined applev area */
	while (imgp->ip_strspace % img_ptr_size != 0) {
		*imgp->ip_strendp++ = '\0';
		imgp->ip_strspace--;
	}

bad:
	return error;
}

/*
 * exec_check_permissions
 *
 * Description:	Verify that the file that is being attempted to be executed
 *		is in fact allowed to be executed based on it POSIX file
 *		permissions and other access control criteria
 *
 * Parameters:	struct image_params *	the image parameter block
 *
 * Returns:	0			Success
 *		EACCES			Permission denied
 *		ENOEXEC			Executable file format error
 *		ETXTBSY			Text file busy [misuse of error code]
 *	vnode_getattr:???
 *	vnode_authorize:???
 */
static int
exec_check_permissions(struct image_params *imgp)
{
	struct vnode *vp = imgp->ip_vp;
	struct vnode_attr *vap = imgp->ip_vattr;
	proc_t p = vfs_context_proc(imgp->ip_vfs_context);
	int error;
	kauth_action_t action;

	/* Only allow execution of regular files */
	if (!vnode_isreg(vp)) {
		return EACCES;
	}

	/* Get the file attributes that we will be using here and elsewhere */
	VATTR_INIT(vap);
	VATTR_WANTED(vap, va_uid);
	VATTR_WANTED(vap, va_gid);
	VATTR_WANTED(vap, va_mode);
	VATTR_WANTED(vap, va_fsid);
	VATTR_WANTED(vap, va_fsid64);
	VATTR_WANTED(vap, va_fileid);
	VATTR_WANTED(vap, va_data_size);
	if ((error = vnode_getattr(vp, vap, imgp->ip_vfs_context)) != 0) {
		return error;
	}

	/*
	 * Ensure that at least one execute bit is on - otherwise root
	 * will always succeed, and we don't want to happen unless the
	 * file really is executable.
	 */
	if (!vfs_authopaque(vnode_mount(vp)) && ((vap->va_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)) {
		return EACCES;
	}

	/* Disallow zero length files */
	if (vap->va_data_size == 0) {
		return ENOEXEC;
	}

	imgp->ip_arch_offset = (user_size_t)0;
#if __LP64__
	imgp->ip_arch_size = vap->va_data_size;
#else
	if (vap->va_data_size > UINT32_MAX) {
		return ENOEXEC;
	}
	imgp->ip_arch_size = (user_size_t)vap->va_data_size;
#endif

	/* Disable setuid-ness for traced programs or if MNT_NOSUID */
	if ((vp->v_mount->mnt_flag & MNT_NOSUID) || (p->p_lflag & P_LTRACED)) {
		vap->va_mode &= ~(VSUID | VSGID);
	}

	/*
	 * Disable _POSIX_SPAWN_ALLOW_DATA_EXEC and _POSIX_SPAWN_DISABLE_ASLR
	 * flags for setuid/setgid binaries.
	 */
	if (vap->va_mode & (VSUID | VSGID)) {
		imgp->ip_flags &= ~(IMGPF_ALLOW_DATA_EXEC | IMGPF_DISABLE_ASLR);
	}

#if CONFIG_MACF
	error = mac_vnode_check_exec(imgp->ip_vfs_context, vp, imgp);
	if (error) {
		return error;
	}
#endif

	/* Check for execute permission */
	action = KAUTH_VNODE_EXECUTE;
	/* Traced images must also be readable */
	if (p->p_lflag & P_LTRACED) {
		action |= KAUTH_VNODE_READ_DATA;
	}
	if ((error = vnode_authorize(vp, NULL, action, imgp->ip_vfs_context)) != 0) {
		return error;
	}

#if 0
	/* Don't let it run if anyone had it open for writing */
	vnode_lock(vp);
	if (vp->v_writecount) {
		panic("going to return ETXTBSY %x", vp);
		vnode_unlock(vp);
		return ETXTBSY;
	}
	vnode_unlock(vp);
#endif

	/* XXX May want to indicate to underlying FS that vnode is open */

	return error;
}


/*
 * exec_handle_sugid
 *
 * Initially clear the P_SUGID in the process flags; if an SUGID process is
 * exec'ing a non-SUGID image, then  this is the point of no return.
 *
 * If the image being activated is SUGID, then replace the credential with a
 * copy, disable tracing (unless the tracing process is root), reset the
 * mach task port to revoke it, set the P_SUGID bit,
 *
 * If the saved user and group ID will be changing, then make sure it happens
 * to a new credential, rather than a shared one.
 *
 * Set the security token (this is probably obsolete, given that the token
 * should not technically be separate from the credential itself).
 *
 * Parameters:	struct image_params *	the image parameter block
 *
 * Returns:	void			No failure indication
 *
 * Implicit returns:
 *		<process credential>	Potentially modified/replaced
 *		<task port>		Potentially revoked
 *		<process flags>		P_SUGID bit potentially modified
 *		<security token>	Potentially modified
 */
__attribute__((noinline))
static int
exec_handle_sugid(struct image_params *imgp)
{
	proc_t                  p = vfs_context_proc(imgp->ip_vfs_context);
	kauth_cred_t            cred = vfs_context_ucred(imgp->ip_vfs_context);
	int                     i;
	int                     leave_sugid_clear = 0;
	int                     mac_reset_ipc = 0;
	int                     error = 0;
#if CONFIG_MACF
	int                     mac_transition, disjoint_cred = 0;
	int             label_update_return = 0;

	/*
	 * Determine whether a call to update the MAC label will result in the
	 * credential changing.
	 *
	 * Note:	MAC policies which do not actually end up modifying
	 *		the label subsequently are strongly encouraged to
	 *		return 0 for this check, since a non-zero answer will
	 *		slow down the exec fast path for normal binaries.
	 */
	mac_transition = mac_cred_check_label_update_execve(
		imgp->ip_vfs_context,
		imgp->ip_vp,
		imgp->ip_arch_offset,
		imgp->ip_scriptvp,
		imgp->ip_scriptlabelp,
		imgp->ip_execlabelp,
		p,
		&imgp->ip_px_smpx);
#endif

	OSBitAndAtomic(~((uint32_t)P_SUGID), &p->p_flag);

	/*
	 * Order of the following is important; group checks must go last,
	 * as we use the success of the 'ismember' check combined with the
	 * failure of the explicit match to indicate that we will be setting
	 * the egid of the process even though the new process did not
	 * require VSUID/VSGID bits in order for it to set the new group as
	 * its egid.
	 *
	 * Note:	Technically, by this we are implying a call to
	 *		setegid() in the new process, rather than implying
	 *		it used its VSGID bit to set the effective group,
	 *		even though there is no code in that process to make
	 *		such a call.
	 */
	if (((imgp->ip_origvattr->va_mode & VSUID) != 0 &&
	    kauth_cred_getuid(cred) != imgp->ip_origvattr->va_uid) ||
	    ((imgp->ip_origvattr->va_mode & VSGID) != 0 &&
	    ((kauth_cred_ismember_gid(cred, imgp->ip_origvattr->va_gid, &leave_sugid_clear) || !leave_sugid_clear) ||
	    (kauth_cred_getgid(cred) != imgp->ip_origvattr->va_gid)))) {
#if CONFIG_MACF
/* label for MAC transition and neither VSUID nor VSGID */
handle_mac_transition:
#endif

#if CONFIG_SETUID
		/*
		 * Replace the credential with a copy of itself if euid or
		 * egid change.
		 *
		 * Note:	setuid binaries will automatically opt out of
		 *		group resolver participation as a side effect
		 *		of this operation.  This is an intentional
		 *		part of the security model, which requires a
		 *		participating credential be established by
		 *		escalating privilege, setting up all other
		 *		aspects of the credential including whether
		 *		or not to participate in external group
		 *		membership resolution, then dropping their
		 *		effective privilege to that of the desired
		 *		final credential state.
		 *
		 * Modifications to p_ucred must be guarded using the
		 * proc's ucred lock. This prevents others from accessing
		 * a garbage credential.
		 */

		if (imgp->ip_origvattr->va_mode & VSUID) {
			kauth_cred_proc_update(p, PROC_SETTOKEN_NONE,
			    ^bool (kauth_cred_t parent __unused, kauth_cred_t model) {
				return kauth_cred_model_setresuid(model,
				KAUTH_UID_NONE,
				imgp->ip_origvattr->va_uid,
				imgp->ip_origvattr->va_uid,
				KAUTH_UID_NONE);
			});
		}

		if (imgp->ip_origvattr->va_mode & VSGID) {
			kauth_cred_proc_update(p, PROC_SETTOKEN_NONE,
			    ^bool (kauth_cred_t parent __unused, kauth_cred_t model) {
				return kauth_cred_model_setresgid(model,
				KAUTH_GID_NONE,
				imgp->ip_origvattr->va_gid,
				imgp->ip_origvattr->va_gid);
			});
		}
#endif /* CONFIG_SETUID */

#if CONFIG_MACF
		/*
		 * If a policy has indicated that it will transition the label,
		 * before making the call into the MAC policies, get a new
		 * duplicate credential, so they can modify it without
		 * modifying any others sharing it.
		 */
		if (mac_transition) {
			/*
			 * This hook may generate upcalls that require
			 * importance donation from the kernel.
			 * (23925818)
			 */
			thread_t thread = current_thread();
			thread_enable_send_importance(thread, TRUE);
			kauth_proc_label_update_execve(p,
			    imgp->ip_vfs_context,
			    imgp->ip_vp,
			    imgp->ip_arch_offset,
			    imgp->ip_scriptvp,
			    imgp->ip_scriptlabelp,
			    imgp->ip_execlabelp,
			    &imgp->ip_csflags,
			    &imgp->ip_px_smpx,
			    &disjoint_cred,                     /* will be non zero if disjoint */
			    &label_update_return);
			thread_enable_send_importance(thread, FALSE);

			if (disjoint_cred) {
				/*
				 * If updating the MAC label resulted in a
				 * disjoint credential, flag that we need to
				 * set the P_SUGID bit.  This protects
				 * against debuggers being attached by an
				 * insufficiently privileged process onto the
				 * result of a transition to a more privileged
				 * credential.
				 */
				leave_sugid_clear = 0;
			}

			imgp->ip_mac_return = label_update_return;
		}

		mac_reset_ipc = mac_proc_check_inherit_ipc_ports(p, p->p_textvp, p->p_textoff, imgp->ip_vp, imgp->ip_arch_offset, imgp->ip_scriptvp);

#endif  /* CONFIG_MACF */

		/*
		 * If 'leave_sugid_clear' is non-zero, then we passed the
		 * VSUID and MACF checks, and successfully determined that
		 * the previous cred was a member of the VSGID group, but
		 * that it was not the default at the time of the execve,
		 * and that the post-labelling credential was not disjoint.
		 * So we don't set the P_SUGID or reset mach ports and fds
		 * on the basis of simply running this code.
		 */
		if (mac_reset_ipc || !leave_sugid_clear) {
			/*
			 * Have mach reset the task and thread ports.
			 * We don't want anyone who had the ports before
			 * a setuid exec to be able to access/control the
			 * task/thread after.
			 */
			ipc_task_reset((imgp->ip_new_thread != NULL) ?
			    get_threadtask(imgp->ip_new_thread) : proc_task(p));
			ipc_thread_reset((imgp->ip_new_thread != NULL) ?
			    imgp->ip_new_thread : current_thread());
		}

		if (!leave_sugid_clear) {
			/*
			 * Flag the process as setuid.
			 */
			OSBitOrAtomic(P_SUGID, &p->p_flag);

			/*
			 * Radar 2261856; setuid security hole fix
			 * XXX For setuid processes, attempt to ensure that
			 * stdin, stdout, and stderr are already allocated.
			 * We do not want userland to accidentally allocate
			 * descriptors in this range which has implied meaning
			 * to libc.
			 */
			for (i = 0; i < 3; i++) {
				if (fp_get_noref_locked(p, i) != NULL) {
					continue;
				}

				/*
				 * Do the kernel equivalent of
				 *
				 *      if i == 0
				 *              (void) open("/dev/null", O_RDONLY);
				 *      else
				 *              (void) open("/dev/null", O_WRONLY);
				 */

				struct fileproc *fp;
				int indx;
				int flag;
				struct nameidata *ndp = NULL;

				if (i == 0) {
					flag = FREAD;
				} else {
					flag = FWRITE;
				}

				if ((error = falloc_exec(p, imgp->ip_vfs_context,
				    &fp, &indx)) != 0) {
					continue;
				}

				ndp = kalloc_type(struct nameidata,
				    Z_WAITOK | Z_ZERO | Z_NOFAIL);

				NDINIT(ndp, LOOKUP, OP_OPEN, FOLLOW, UIO_SYSSPACE,
				    CAST_USER_ADDR_T("/dev/null"),
				    imgp->ip_vfs_context);

				if ((error = vn_open(ndp, flag, 0)) != 0) {
					fp_free(p, indx, fp);
					kfree_type(struct nameidata, ndp);
					break;
				}

				struct fileglob *fg = fp->fp_glob;

				fg->fg_flag = flag;
				fg->fg_ops = &vnops;
				fp_set_data(fp, ndp->ni_vp);

				vnode_put(ndp->ni_vp);

				proc_fdlock(p);
				procfdtbl_releasefd(p, indx, NULL);
				fp_drop(p, indx, fp, 1);
				proc_fdunlock(p);

				kfree_type(struct nameidata, ndp);
			}
		}
	}
#if CONFIG_MACF
	else {
		/*
		 * We are here because we were told that the MAC label will
		 * be transitioned, and the binary is not VSUID or VSGID; to
		 * deal with this case, we could either duplicate a lot of
		 * code, or we can indicate we want to default the P_SUGID
		 * bit clear and jump back up.
		 */
		if (mac_transition) {
			leave_sugid_clear = 1;
			goto handle_mac_transition;
		}
	}

#endif  /* CONFIG_MACF */

	/* Update the process' identity version and set the security token */
	proc_setpidversion(p, OSIncrementAtomic(&nextpidversion));
	task_set_uniqueid(proc_task(p));

	/*
	 * Implement the semantic where the effective user and group become
	 * the saved user and group in exec'ed programs.
	 */
	kauth_cred_proc_update(p, PROC_SETTOKEN_ALWAYS,
	    ^bool (kauth_cred_t parent __unused, kauth_cred_t model) {
		posix_cred_t pcred = posix_cred_get(model);

		if (pcred->cr_svuid == pcred->cr_uid &&
		pcred->cr_svgid == pcred->cr_gid) {
		        return false;
		}

		pcred->cr_svuid = pcred->cr_uid;
		pcred->cr_svgid = pcred->cr_gid;
		return true;
	});

	return error;
}


/*
 * create_unix_stack
 *
 * Description:	Set the user stack address for the process to the provided
 *		address.  If a custom stack was not set as a result of the
 *		load process (i.e. as specified by the image file for the
 *		executable), then allocate the stack in the provided map and
 *		set up appropriate guard pages for enforcing administrative
 *		limits on stack growth, if they end up being needed.
 *
 * Parameters:	p			Process to set stack on
 *		load_result		Information from mach-o load commands
 *		map			Address map in which to allocate the new stack
 *
 * Returns:	KERN_SUCCESS		Stack successfully created
 *		!KERN_SUCCESS		Mach failure code
 */
__attribute__((noinline))
static kern_return_t
create_unix_stack(vm_map_t map, load_result_t* load_result,
    proc_t p)
{
	mach_vm_size_t          size, prot_size;
	mach_vm_offset_t        addr, prot_addr;
	kern_return_t           kr;

	mach_vm_address_t       user_stack = load_result->user_stack;

	proc_lock(p);
	p->user_stack = (uintptr_t)user_stack;
	if (load_result->custom_stack) {
		p->p_lflag |= P_LCUSTOM_STACK;
	}
	proc_unlock(p);
	if (vm_map_page_shift(map) < (int)PAGE_SHIFT) {
		DEBUG4K_LOAD("map %p user_stack 0x%llx custom %d user_stack_alloc_size 0x%llx\n", map, user_stack, load_result->custom_stack, load_result->user_stack_alloc_size);
	}

	if (load_result->user_stack_alloc_size > 0) {
		/*
		 * Allocate enough space for the maximum stack size we
		 * will ever authorize and an extra page to act as
		 * a guard page for stack overflows. For default stacks,
		 * vm_initial_limit_stack takes care of the extra guard page.
		 * Otherwise we must allocate it ourselves.
		 */
		if (mach_vm_round_page_overflow(load_result->user_stack_alloc_size, &size)) {
			return KERN_INVALID_ARGUMENT;
		}
		addr = vm_map_trunc_page(load_result->user_stack - size,
		    vm_map_page_mask(map));
		kr = mach_vm_allocate_kernel(map, &addr, size,
		    VM_FLAGS_FIXED, VM_MEMORY_STACK);
		if (kr != KERN_SUCCESS) {
			// Can't allocate at default location, try anywhere
			addr = 0;
			kr = mach_vm_allocate_kernel(map, &addr, size,
			    VM_FLAGS_ANYWHERE, VM_MEMORY_STACK);
			if (kr != KERN_SUCCESS) {
				return kr;
			}

			user_stack = addr + size;
			load_result->user_stack = (user_addr_t)user_stack;

			proc_lock(p);
			p->user_stack = (uintptr_t)user_stack;
			proc_unlock(p);
		}

		load_result->user_stack_alloc = (user_addr_t)addr;

		/*
		 * And prevent access to what's above the current stack
		 * size limit for this process.
		 */
		if (load_result->user_stack_size == 0) {
			load_result->user_stack_size = proc_limitgetcur(p, RLIMIT_STACK);
			prot_size = vm_map_trunc_page(size - load_result->user_stack_size, vm_map_page_mask(map));
		} else {
			prot_size = PAGE_SIZE;
		}

		prot_addr = addr;
		kr = mach_vm_protect(map,
		    prot_addr,
		    prot_size,
		    FALSE,
		    VM_PROT_NONE);
		if (kr != KERN_SUCCESS) {
			(void)mach_vm_deallocate(map, addr, size);
			return kr;
		}
	}

	return KERN_SUCCESS;
}

#include <sys/reboot.h>

/*
 * load_init_program_at_path
 *
 * Description:	Load the "init" program; in most cases, this will be "launchd"
 *
 * Parameters:	p			Process to call execve() to create
 *					the "init" program
 *		scratch_addr		Page in p, scratch space
 *		path			NULL terminated path
 *
 * Returns:	KERN_SUCCESS		Success
 *		!KERN_SUCCESS           See execve/mac_execve for error codes
 *
 * Notes:	The process that is passed in is the first manufactured
 *		process on the system, and gets here via bsd_ast() firing
 *		for the first time.  This is done to ensure that bsd_init()
 *		has run to completion.
 *
 *		The address map of the first manufactured process matches the
 *		word width of the kernel. Once the self-exec completes, the
 *		initproc might be different.
 */
/* Probe globals live outside the owner-cache guard: the bootstrap pass compiles
 * this file without -DXNU_OWNER_CACHE_MOUNT and still references them. */
volatile int meizu_probe_err[16];
volatile int meizu_probe_kr[3];
unsigned long long meizu_premapped_cache = 0;
unsigned long long meizu_uc_size;
int meizu_uc_read = -99;
unsigned int meizu_uc_hdr;
volatile int meizu_touch_state = 0;

#if defined(XNU_OWNER_CACHE_MOUNT) && XNU_OWNER_CACHE_MOUNT
#if !DEVELOPMENT && !DEBUG
#error The offline owner cache mount is a development instrument
#endif
extern int native_verified_block_volume_device(const char *, dev_t *, char *, size_t);
#if defined(XNU_OWNER_BOOT_MANIFEST) && XNU_OWNER_BOOT_MANIFEST
extern int native_owner_publish_boot_manifest(dev_t, const char *, dev_t, const char *);
#endif
#if defined(XNU_OWNER_CACHE_STARTUP) && XNU_OWNER_CACHE_STARTUP
extern void commpage_update_dyld_flags(uint64_t);
volatile uint64_t native_owner_cache_dyld_flags = 0;
#endif
extern int native_verified_block_root_authorized(dev_t);
volatile int native_owner_cache_stage = 0;
volatile int native_owner_cache_status = -1;
volatile int native_owner_cache_mount_status = -1;
volatile int native_owner_cache_readonly = 0;
volatile int native_owner_cache_authorized = 0;
volatile int native_owner_cache_root_recheck = 0;
volatile uint64_t native_owner_cache_actual_mount_flags = 0;
volatile uintptr_t native_owner_cache_original_root = 0;
volatile uintptr_t native_owner_cache_current_root = 0;
volatile int native_owner_cache_native_union = 0;
volatile dev_t native_owner_cache_selected_device = NODEV;
volatile dev_t native_owner_cache_mounted_device = NODEV;
char native_owner_cache_device_path[64];
void native_owner_cache_checkpoint(int, int);
__attribute__((noinline)) void
native_owner_cache_checkpoint(int status, int authorized)
{
	__asm__ volatile("" : : "r"(status), "r"(authorized) : "memory");
}


/* Private diagnostic contract; not an exported kernel interface. */
#include "../../osfmk/console/meizu_log.h"
static char meizu_dump[MEIZU_LOG_DUMP_SIZE];
static int meizu_dump_busy;
static int meizu_dump_tick;
/* Protected by meizu_dump_busy. Keep probe drains at hz/20, but allow at
 * most one full UFS snapshot per second after the previous attempt ends.
 * An explicit flush shares this bound. The first attempt is never delayed.
 * With a running writer and successful I/O, reset can lose the latest ~1s
 * plus scheduling/I/O delay. Stalls/errors have no wall-clock loss bound;
 * in-place multi-chunk writes are not power-loss atomic. */
static int meizu_dump_attempted;
static int meizu_dump_write_ok;
static uint64_t meizu_dump_last_end;
static uint64_t meizu_dump_saved_total;
static uint32_t meizu_dump_saved_blocks;
volatile int meizu_init_quiet_seconds;
volatile int meizu_init_progress_state;

/* Nova R7: counters are protected by meizu_dump_busy. Only real snapshot
 * attempts update/report them, so a clean no-op never logs solely to trigger
 * another write. Reports are at most once per 10s, 31 times within 300s.
 * Recount/copy/write durations overlap other CPU work: no additive boot budget. */
static uint64_t nova_logio_attempts, nova_logio_writes, nova_logio_ok;
static uint64_t nova_logio_bytes, nova_logio_copy, nova_logio_write;
static uint64_t nova_logio_attempt, nova_logio_max_write, nova_logio_errors;
static uint64_t nova_logio_started, nova_logio_last_report;
static unsigned nova_logio_reports;

static void
nova_logio_add_elapsed(uint64_t begin, uint64_t end, uint64_t *sum, uint64_t *maximum)
{
	if (end < begin || end - begin > UINT64_MAX - *sum) {
		nova_logio_errors++;
		return;
	}
	uint64_t delta = end - begin;
	*sum += delta;
	if (maximum && delta > *maximum) *maximum = delta;
}

static void
nova_logio_report(uint64_t now)
{
	uint64_t age_ns = 0, gap_ns = 0;
	if (!nova_logio_attempts || nova_logio_reports >= 31) return;
	if (now < nova_logio_started || (nova_logio_reports && now < nova_logio_last_report)) {
		nova_logio_errors++;
		return;
	}
	absolutetime_to_nanoseconds(now - nova_logio_started, &age_ns);
	if (age_ns > 300ULL * NSEC_PER_SEC) return;
	if (nova_logio_reports) {
		absolutetime_to_nanoseconds(now - nova_logio_last_report, &gap_ns);
		if (gap_ns < 10ULL * NSEC_PER_SEC) return;
	}
	uint64_t ns = 0, copy_ns = 0, write_ns = 0, attempt_ns = 0, max_write_ns = 0;
	absolutetime_to_nanoseconds(now, &ns);
	absolutetime_to_nanoseconds(nova_logio_copy, &copy_ns);
	absolutetime_to_nanoseconds(nova_logio_write, &write_ns);
	absolutetime_to_nanoseconds(nova_logio_attempt, &attempt_ns);
	absolutetime_to_nanoseconds(nova_logio_max_write, &max_write_ns);
	nova_logio_last_report = now;
	nova_logio_reports++;
	printf("nova: autologio v=1 clock=mach_absolute ticks=%llu ns=%llu attempts=%llu writes=%llu ok=%llu bytes=%llu copy_ns=%llu write_ns=%llu attempt_ns=%llu max_write_ns=%llu clock_errors=%llu dump=%u\n",
	    (unsigned long long)now, (unsigned long long)ns,
	    (unsigned long long)nova_logio_attempts, (unsigned long long)nova_logio_writes,
	    (unsigned long long)nova_logio_ok, (unsigned long long)nova_logio_bytes,
	    (unsigned long long)copy_ns, (unsigned long long)write_ns,
	    (unsigned long long)attempt_ns, (unsigned long long)max_write_ns,
	    (unsigned long long)nova_logio_errors, nova_logio_reports);
#if defined(__arm64__) || defined(__aarch64__)
	/* Keep a bracket, not a guessed shared epoch. No frequency-policy writes. */
	uint64_t vbefore, vafter, frequency, mach, mach_ns = 0;
	__asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(vbefore) :: "memory");
	mach = mach_absolute_time();
	__asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(vafter) :: "memory");
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency) :: "memory");
	absolutetime_to_nanoseconds(mach, &mach_ns);
	printf("nova: clockcal v=1 cntvct_before=%llu mach_ticks=%llu mach_ns=%llu cntvct_after=%llu timer_hz=%llu dump=%u\n",
	    (unsigned long long)vbefore, (unsigned long long)mach,
	    (unsigned long long)mach_ns, (unsigned long long)vafter,
	    (unsigned long long)frequency, nova_logio_reports);
#endif
}

enum nova_policy_save_result {
	NOVA_POLICY_SAVE_NORMAL = 0,
	NOVA_POLICY_SAVE_SYNCED = 1,
	NOVA_POLICY_SAVE_BUSY = 2,
	NOVA_POLICY_SAVE_ALREADY_ATTEMPTED = 3,
	NOVA_POLICY_SAVE_OPEN_FAILED = 4,
	NOVA_POLICY_SAVE_CAPACITY_FAILED = 5,
	NOVA_POLICY_SAVE_WRITE_FAILED = 6,
	NOVA_POLICY_SAVE_SYNC_FAILED = 7
};

/* The failure-only mode retains at most the newest 64 KiB. It bypasses only
 * normal cadence/no-change checks, never the busy guard or transport locks. */
static int
meizu_autolog_dump_mode(int force)
{
	extern int native_ufs_log_open(uint64_t *, uint64_t *, const char **);
	extern int native_ufs_log_write(uint64_t, uint32_t, const void *);
	extern int native_ufs_log_sync(void);
	extern void native_ufs_log_read_stats(void);
	extern uint64_t meizu_log_snapshot(char *, uint64_t, uint64_t *);
	extern uint64_t meizu_log_total(void);
	extern uint64_t native_blog_boot_video_format(char *, uint64_t);
	uint64_t logfirst = 0, logblocks = 0;
	const char *why = "";
	struct {
		char magic[8];
		uint64_t total;
		uint64_t bytes;
		uint32_t dropped;
		uint32_t reserved;
	} hdr;
	uint64_t n, prefix, snapshot_total, retained;
	uint32_t blocks;
	int save_result = NOVA_POLICY_SAVE_NORMAL;

	static int opened;
	static uint64_t available_blocks;
	/* Flushes can be called recursively and from multiple threads. */
	if (__atomic_exchange_n(&meizu_dump_busy, 1, __ATOMIC_ACQUIRE)) {
		return NOVA_POLICY_SAVE_BUSY;
	}
	uint64_t attempt_begin = mach_absolute_time();
	uint64_t elapsed_ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time() - meizu_dump_last_end, &elapsed_ns);
	if (!force && meizu_dump_attempted && elapsed_ns < NSEC_PER_SEC) {
		goto heartbeat;
	}
	/* Only a successful write retires this captured total. In particular an
	 * empty first snapshot and a failed write must not be mistaken for clean. */
	if (!force && meizu_dump_write_ok && meizu_log_total() == meizu_dump_saved_total) {
		goto heartbeat;
	}
	meizu_dump_write_ok = 0;
	if (!opened) {
		if (!native_ufs_log_open(&logfirst, &logblocks, &why)) {
			save_result = NOVA_POLICY_SAVE_OPEN_FAILED;
			goto attempted;
		}
		opened = 1;
		available_blocks = logblocks;
	}
	/* Do not infer capacity from the fixed tail offset: NativeUfs owns it. */
	if (available_blocks < sizeof(meizu_dump) / MEIZU_LOG_BLOCK_SIZE) {
		save_result = NOVA_POLICY_SAVE_CAPACITY_FAILED;
		goto attempted;
	}
	/* The firmware reader blanks the header page; end that padded line so
	 * the raw record is visible instead of being truncated by the viewer. */
	prefix = native_blog_boot_video_format(meizu_dump + 4097, MEIZU_LOG_PREFIX_CAP - 1);
	if (prefix) {
		meizu_dump[4096] = '\n';
		prefix++;
	}
	uint64_t copy_begin = mach_absolute_time();
	retained = meizu_log_snapshot(meizu_dump + 4096 + prefix,
	    force ? 64U * 1024U : MEIZU_LOG_RING_SIZE, &snapshot_total);
	nova_logio_add_elapsed(copy_begin, mach_absolute_time(), &nova_logio_copy, NULL);
	n = prefix + retained;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, "MEIZULOG", 8);
	hdr.total = snapshot_total;
	hdr.bytes = n;
	/* Historical writers left this zero even after wrap. Count lost console
	 * bytes (excluding the synthetic prefix), saturating the existing u32. */
	hdr.dropped = snapshot_total - retained > 0xffffffffULL ? 0xffffffffU : (uint32_t)(snapshot_total - retained);
	memset(meizu_dump, 0, MEIZU_LOG_BLOCK_SIZE);
	memcpy(meizu_dump, &hdr, sizeof(hdr));
	blocks = (uint32_t)((4096 + n + 4095) / 4096);
	memset(meizu_dump + 4096 + n, 0, blocks * MEIZU_LOG_BLOCK_SIZE - 4096 - n);
	uint64_t write_begin = mach_absolute_time();
	int write_ok = native_ufs_log_write(0, blocks, meizu_dump);
	nova_logio_add_elapsed(write_begin, mach_absolute_time(), &nova_logio_write, &nova_logio_max_write);
	nova_logio_writes++;
	int sync_ok = 1;
	if (force) {
		if (write_ok) sync_ok = native_ufs_log_sync();
		save_result = !write_ok ? NOVA_POLICY_SAVE_WRITE_FAILED :
		    (sync_ok ? NOVA_POLICY_SAVE_SYNCED : NOVA_POLICY_SAVE_SYNC_FAILED);
	}
	if (write_ok && sync_ok) {
		meizu_dump_write_ok = 1;
		meizu_dump_saved_total = snapshot_total;
		meizu_dump_saved_blocks = blocks;
		nova_logio_ok++;
		nova_logio_bytes += (uint64_t)blocks * MEIZU_LOG_BLOCK_SIZE;
	}
attempted:
	meizu_dump_attempted = 1;
	meizu_dump_last_end = mach_absolute_time();
	if (!nova_logio_attempts) nova_logio_started = attempt_begin;
	nova_logio_attempts++;
	nova_logio_add_elapsed(attempt_begin, meizu_dump_last_end, &nova_logio_attempt, NULL);
	nova_logio_report(meizu_dump_last_end);
	if (force) goto release;
heartbeat:
	/* Global cumulative root I/O timing; do not print from a block read or
	 * while holding the UFS transport lock. Call count is not elapsed time:
	 * the snapshot carries its own architectural timer and frequency. */
	if ((meizu_dump_tick % 200) == 0) {
		native_ufs_log_read_stats();
	}
	/* Preserve the first/every-20-calls heartbeat independently of UFS
	 * cadence. It describes the last attempt, not proof of kernel progress. */
	if (meizu_dump_write_ok) {
		if (meizu_dump_tick == 0 || (meizu_dump_tick % 20) == 0) {
			printf("meizu: hb tick=%d total=%llu blocks=%u prog=%d\n",
			    meizu_dump_tick, (unsigned long long)meizu_dump_saved_total,
			    meizu_dump_saved_blocks,
			    meizu_init_progress_state);
		}
	} else if (meizu_dump_tick == 0 || (meizu_dump_tick % 20) == 0) {
		printf("meizu: hb write failed tick=%d\n", meizu_dump_tick);
	}
	meizu_dump_tick = (meizu_dump_tick == 0x7fffffff) ? 0 : meizu_dump_tick + 1;
release:
	__atomic_store_n(&meizu_dump_busy, 0, __ATOMIC_RELEASE);
	return save_result;
}

static void
meizu_autolog_dump(void)
{
	(void)meizu_autolog_dump_mode(0);
}

#if defined(XNU_MEIZU_WATCHDOG_POLICY) && XNU_MEIZU_WATCHDOG_POLICY
static int nova_policy_force_attempted;
static int
nova_policy_failure_save_once(void)
{
	/* Consumed even if the normal writer is busy. No sleep or retry here.
	 * UFS lock/command latency remains transport-defined, not hard realtime. */
	if (__atomic_exchange_n(&nova_policy_force_attempted, 1, __ATOMIC_ACQ_REL)) {
		return NOVA_POLICY_SAVE_ALREADY_ATTEMPTED;
	}
	return meizu_autolog_dump_mode(1);
}
#endif

void
meizu_autolog_flush(void)
{
	meizu_autolog_dump();
}

thread_t meizu_child_thread = THREAD_NULL;

static void
meizu_sample_pid22(void)
{
	static unsigned int requests;
	/* Rearm only from the periodic thread, never inside bsd_ast().
	 * The held thread reference is established by the syscall hook. */
	if (meizu_child_thread != THREAD_NULL && requests < 30) {
		requests++;
		act_set_astbsd(meizu_child_thread);
	}
}

/* Returns -1 when busy, 0 when inactive, 1 when active; prints after unlock. */
extern int meizu_sb_thread_snapshot(thread_t, int, uint64_t, bool, uint64_t, uint64_t);
extern bool meizu_sb_ipc_receiver(int *, uint64_t *, uint64_t *);
extern void meizu_sb_workers_snapshot(thread_t, int, uint64_t, uint64_t, unsigned);
extern void meizu_sb_queue_snapshot(thread_t, int, uint64_t, uint64_t, unsigned);

/* Single autolog caller. One boot-wide six-round budget at selection,
 * +10/+30/+60/+120/+180s; dependency or exec churn never resets it. Each round
 * follows at most two exact-generation container peers, all 32 lsd slots are
 * inspected so a busy non-container worker cannot hide the selected peer. */
static void
meizu_sample_containers(uint64_t lsd_generation)
{
	static uint64_t started;
	static unsigned round;
	static const unsigned seconds[] = { 0, 10, 30, 60, 120, 180 };
	uint64_t now = mach_absolute_time(), age;
	uint64_t seen[2] = { 0 };
	unsigned count = 0;
	if (round >= sizeof(seconds) / sizeof(seconds[0])) return;
	absolutetime_to_nanoseconds(now - started, &age);
	if (started && age < (uint64_t)seconds[round] * NSEC_PER_SEC) return;
	for (unsigned slot = 0; slot < 32 && count < 2; slot++) {
		int pid;
		uint64_t generation, tid, sequence;
		struct meizu_exec_entry e = { 0 };
		if (!meizu_lsd_ipc_dependency(lsd_generation, slot, &pid, &generation,
		    &tid, &sequence) || generation == seen[0]) continue;
		if (!lck_mtx_try_lock(&meizu_exec_lock)) continue;
		if (!meizu_exec_closed) {
			for (unsigned i = 0; i < MEIZU_EXEC_SLOTS; i++) {
				struct meizu_exec_entry *candidate = &meizu_exec_registry[i];
				if (candidate->thread && candidate->pid == pid &&
				    candidate->taskgen == generation && candidate->container) {
					e = *candidate;
					thread_reference(e.thread);
					break;
				}
			}
		}
		lck_mtx_unlock(&meizu_exec_lock);
		if (!e.thread) continue;
		if (!started) started = now;
		seen[count++] = generation;
		printf("meizu: container-map lsdgen=%llu lsdTID=%llu seq=%llu pid=%d taskgen=%llu name=%s path=%s path_error=%d round=%u\n",
		    (unsigned long long)lsd_generation, (unsigned long long)tid,
		    (unsigned long long)sequence, pid, (unsigned long long)generation,
		    e.name, e.path_error ? "?" : e.path, e.path_error, round + 1);
		meizu_container_ipc_snapshot(generation, round + 1);
		meizu_sb_ulock_snapshot(generation, round + 1);
		meizu_sb_queue_snapshot(e.thread, pid, generation, sequence, round + 1);
		meizu_sb_workers_snapshot(e.thread, pid, generation, sequence, round + 1);
		thread_deallocate(e.thread);
	}
	/* Once selected, even a disconnected dependency consumes its due round. */
	if (started) round++;
}

/* Single autolog caller. IPC lock is dropped before registry lock; all target
 * inspection and puts occur after registry unlock. Exact taskgen match only. */
static void
meizu_sample_receiver(void)
{
	static uint64_t last_sample, last_report, last_gen, last_seq;
	static uint64_t last_tid;
	uint64_t now = mach_absolute_time(), elapsed, age, gen = 0, seq = 0;
	int pid = -1;
	thread_t retired[MEIZU_EXEC_SLOTS];
	unsigned nretired = 0;
	struct meizu_exec_entry e = { 0 };
	absolutetime_to_nanoseconds(now - last_sample, &elapsed);
	if (last_sample && elapsed < NSEC_PER_SEC) {
		return;
	}
	last_sample = now;
	bool valid = meizu_sb_ipc_receiver(&pid, &gen, &seq);
	if (!lck_mtx_try_lock(&meizu_exec_lock)) {
		return;
	}
	now = mach_absolute_time();
	absolutetime_to_nanoseconds(now - meizu_exec_started, &age);
	if (meizu_exec_started && age >= 300ULL * NSEC_PER_SEC && !meizu_exec_closed) {
		meizu_exec_closed = true;
		for (unsigned i = 0; i < MEIZU_EXEC_SLOTS; i++) {
			if (meizu_exec_registry[i].thread) {
				retired[nretired++] = meizu_exec_registry[i].thread;
				meizu_exec_registry[i].thread = THREAD_NULL;
			}
		}
	}
	if (valid && !meizu_exec_closed) {
		for (unsigned i = 0; i < MEIZU_EXEC_SLOTS; i++) {
			if (meizu_exec_registry[i].thread && meizu_exec_registry[i].pid == pid &&
			    meizu_exec_registry[i].taskgen == gen) {
				e = meizu_exec_registry[i];
				thread_reference(e.thread);
				break;
			}
		}
	}
	lck_mtx_unlock(&meizu_exec_lock);
	for (unsigned i = 0; i < nretired; i++) {
		thread_deallocate(retired[i]);
	}
	if (nretired) {
		printf("meizu: exec-map closed released=%u\n", nretired);
	}
	if (!valid) {
		return;
	}
	absolutetime_to_nanoseconds(now - last_report, &elapsed);
	if (!last_report || gen != last_gen || seq != last_seq || e.tid != last_tid ||
	    elapsed >= 10ULL * NSEC_PER_SEC) {
		printf("meizu: receiver-map seq=%llu pid=%d taskgen=%llu tid=%llu name=%s status=%s\n",
		    (unsigned long long)seq, pid, (unsigned long long)gen,
		    (unsigned long long)e.tid, e.thread ? e.name : "?",
		    e.thread ? "matched" : "unavailable");
		last_report = now; last_gen = gen; last_seq = seq; last_tid = e.tid;
	}
	if (e.thread) {
		/* Global boot budget, not reset by sequence/task churn. Each round
		 * samples the currently selected endpoint through selection/+180s. */
		static uint64_t rounds_started;
		static unsigned rounds;
		bool target_lsd = strcmp(e.name, "lsd") == 0;
		if (target_lsd) meizu_sample_containers(gen);
		if (target_lsd && !rounds_started) {
			rounds_started = now;
		}
		absolutetime_to_nanoseconds(now - rounds_started, &elapsed);
		static const unsigned seconds[] = { 0, 10, 30, 60, 120, 180 };
		if (target_lsd && rounds < 6 &&
		    elapsed >= (uint64_t)seconds[rounds] * NSEC_PER_SEC) {
			rounds++;
			meizu_lsd_ipc_snapshot(gen, rounds);
			meizu_sb_ulock_snapshot(gen, rounds);
			meizu_sb_queue_snapshot(e.thread, pid, gen, seq, rounds);
			meizu_sb_workers_snapshot(e.thread, pid, gen, seq, rounds);
		}
		thread_deallocate(e.thread);
	}
}

static void
meizu_sample_springboard(void)
{
	thread_t thread, retired = THREAD_NULL;
	pid_t pid;
	uint64_t tid, generation, now, age_ns, elapsed_ns;
	int active;

	/* Never delay the log writer waiting for a capture or target thread. */
	if (!lck_mtx_try_lock(&meizu_sb_lock)) {
		return;
	}
	thread = meizu_sb_thread;
	if (thread == THREAD_NULL) {
		lck_mtx_unlock(&meizu_sb_lock);
		return;
	}
	now = mach_absolute_time();
	absolutetime_to_nanoseconds(now - meizu_sb_started, &age_ns);
	if (age_ns >= 300ULL * NSEC_PER_SEC || meizu_sb_samples >= 300) {
		meizu_sb_thread = THREAD_NULL;
		lck_mtx_unlock(&meizu_sb_lock);
		thread_deallocate(thread);
		return;
	}
	absolutetime_to_nanoseconds(now - meizu_sb_last_sample, &elapsed_ns);
	if (meizu_sb_last_sample != 0 && elapsed_ns < NSEC_PER_SEC) {
		lck_mtx_unlock(&meizu_sb_lock);
		return;
	}
	meizu_sb_last_sample = now;
	meizu_sb_samples++;
	pid = meizu_sb_pid;
	tid = meizu_sb_tid;
	generation = meizu_sb_generation;
	thread_reference(thread);
	if (meizu_sb_samples == 300) {
		retired = thread;
		meizu_sb_thread = THREAD_NULL;
	}
	lck_mtx_unlock(&meizu_sb_lock);

	active = meizu_sb_thread_snapshot(thread, pid, tid, false, generation, 0);
	/* Independent boot quota: arm only after 5 seconds continuously observed
	 * waiting on the same event/hint. Busy/running samples reset qualification.
	 * A new exec cannot spend the old generation's remaining rounds. */
	extern bool meizu_sb_sustained_wait(thread_t, uint64_t);
	static uint64_t workers_started, workers_generation;
	static unsigned workers_rounds;
	if (workers_rounds < 3 && meizu_sb_sustained_wait(thread, generation)) {
		if (!workers_started) {
			workers_started = now;
			workers_generation = generation;
		}
		absolutetime_to_nanoseconds(now - workers_started, &elapsed_ns);
		if (workers_generation == generation && (workers_rounds == 0 ||
		    elapsed_ns >= (workers_rounds == 1 ? 10ULL : 30ULL) * NSEC_PER_SEC)) {
			uint64_t taskgen = meizu_thread_task_generation(thread);
			workers_rounds++;
			printf("meizu: sb-workers pid=%d taskgen=%llu round=%u\n",
			    pid, (unsigned long long)taskgen, workers_rounds);
			meizu_sb_ulock_snapshot(taskgen, workers_rounds);
			meizu_sb_worker_ipc_snapshot(taskgen, workers_rounds, meizu_sb_ulock_owner(taskgen));
			meizu_sb_workers_snapshot(thread, pid, taskgen, 0, workers_rounds);
		}
	}
	if (active == 0 && retired == THREAD_NULL && lck_mtx_try_lock(&meizu_sb_lock)) {
		if (meizu_sb_thread == thread && meizu_sb_generation == generation) {
			retired = thread;
			meizu_sb_thread = THREAD_NULL;
		}
		lck_mtx_unlock(&meizu_sb_lock);
	}
	if (retired != THREAD_NULL) {
		thread_deallocate(retired);
	}
	thread_deallocate(thread);
}

/* Nova R8: capture only. A post-Setup delay is not a Hello/clean-filesystem
 * assertion. The worker exports an atomic block-device cut for offline
 * inspection, and this kernel never restores it. Called by the one autolog
 * thread with no Setup, log, RamBlock or UFS lock held. */
extern int native_ram_block_capture_warm(uint64_t) __attribute__((weak_import));
static void __attribute__((noinline))
nova_data_capture_poll(void)
{
	static int configured, enabled, attempted;
	if (!configured) {
		uint32_t option = 0;
		enabled = PE_parse_boot_argn("nova-data-capture", &option, sizeof(option)) && option == 1;
		configured = 1;
		if (enabled) printf("nova: data-capture trigger=post-Setup delay_seconds=90 restore=0\n");
	}
	if (!enabled || attempted) return;
	if (!lck_mtx_try_lock(&meizu_setup_lock)) return;
	uint64_t anchor = nova_capture_setup_first;
	lck_mtx_unlock(&meizu_setup_lock);
	uint64_t now = mach_absolute_time(), elapsed_ns = 0;
	if (!anchor || now < anchor) return;
	absolutetime_to_nanoseconds(now - anchor, &elapsed_ns);
	if (elapsed_ns < 90ULL * NSEC_PER_SEC) return;
	attempted = 1;
	printf("nova: data-capture trigger-fired capture_id=%llu since_setup_ns=%llu clock=mach_absolute\n",
	    (unsigned long long)anchor, (unsigned long long)elapsed_ns);
	int status = &native_ram_block_capture_warm ? native_ram_block_capture_warm(anchor) : -1;
	printf("nova: data-capture trigger-return capture_id=%llu status=%d restore=0\n",
	    (unsigned long long)anchor, status);
}

static void
meizu_autolog_thread(void *arg, wait_result_t wr)
{
	(void)arg;
	(void)wr;
	int meizu_sample_n = 0;
	int last_init_sys_count = -1;
	int quiet_seconds = 0;
	uint64_t last_activity_time = mach_absolute_time();
	uint64_t last_report_time = 0;
	unsigned long long last_init_pc = 0;
	unsigned long long init_irq_seen = 0;
	unsigned long long init_el0_seen = 0;
	unsigned long long init_el1_seen = 0;
	unsigned long long last_init_kpc = 0;
	extern volatile int meizu_init_sys_count;
	extern volatile int meizu_init_last_syscall;
	extern volatile unsigned long long meizu_init_el0_pc;
	extern volatile unsigned long long meizu_init_irq_count;
	extern volatile unsigned long long meizu_init_el0_sample_count;
	extern volatile unsigned long long meizu_init_el1_pc;
	extern volatile unsigned long long meizu_init_el1_sample_count;
	extern unsigned long long native_blog_irq_count(void);
	extern void native_blog_status_timing_dump(void);
#if defined(XNU_MEIZU_CPU_FREQ_PROBE) && XNU_MEIZU_CPU_FREQ_PROBE
	extern void nova_cpufreq_probe_dump_once(void);
#endif
	extern volatile int meizu_init_np_stage;
	extern volatile unsigned long long meizu_init_np_sr_flags;
	extern volatile unsigned long long meizu_init_np_map_in_progress;
	extern volatile int meizu_init_np_kr;
	for (;;) {
		meizu_sample_setup();
		meizu_sample_springboard();
		meizu_sample_receiver();
		meizu_sb_ipc_drain();
		/* Nova S03: scalar summaries, independently time/budget bounded. */
		nova_lsd_ipc_timing_dump();
		nova_lsd_bt_timing_dump();
		native_blog_status_timing_dump();
#if defined(XNU_MEIZU_CPU_FREQ_PROBE) && XNU_MEIZU_CPU_FREQ_PROBE
		nova_cpufreq_probe_dump_once();
#endif
		meizu_autolog_dump();
		nova_data_capture_poll();
		if ((meizu_sample_n++ % 1) == 0) {
			meizu_sample_pid22();
		}
		if ((meizu_sample_n % 20) == 0) {
			int count = meizu_init_sys_count;
			uint64_t now = mach_absolute_time();
			uint64_t elapsed_ns = 0;
			int second;
			absolutetime_to_nanoseconds(now - last_activity_time, &elapsed_ns);
			second = (int)(elapsed_ns / NSEC_PER_SEC);
			unsigned long long pc = meizu_init_el0_pc;
			unsigned long long irq = native_blog_irq_count();
			unsigned long long init_irq = meizu_init_irq_count;
			unsigned long long init_el0 = meizu_init_el0_sample_count;
			unsigned long long init_el1 = meizu_init_el1_sample_count;
			unsigned long long kpc = meizu_init_el1_pc;
			int state;
			if (count != last_init_sys_count) {
				quiet_seconds = 0;
				last_activity_time = now;
				last_init_sys_count = count;
				state = 1;
			} else {
				quiet_seconds = (int)(elapsed_ns / NSEC_PER_SEC);
				if (init_el0 > init_el0_seen && pc != 0 && pc != last_init_pc) {
					state = 2;
				} else if (init_el0 > init_el0_seen && pc != 0) {
					state = 3;
				} else if (init_el1 > init_el1_seen && kpc != 0 && kpc != last_init_kpc) {
					state = 4;
				} else if (init_el1 > init_el1_seen && kpc != 0) {
					state = 5;
				} else if (init_irq > init_irq_seen) {
					state = 6;
				} else {
					state = 7;
				}
			}
			if (pc != 0) last_init_pc = pc;
			if (kpc != 0) last_init_kpc = kpc;
			init_irq_seen = init_irq;
			init_el0_seen = init_el0;
			init_el1_seen = init_el1;
			meizu_init_quiet_seconds = quiet_seconds;
			meizu_init_progress_state = state;
			/* meizu: unslid kernel PC of init plus its scheduler state, so the
			 * panel names the function init is in without knowing the slide. */
			{
				extern vm_offset_t vm_kernel_slide;
				extern volatile unsigned long long meizu_init_kpc_unslid;
				extern volatile unsigned long long meizu_init_kpc_hist[4];
				extern volatile unsigned int meizu_init_tstate;
				extern volatile unsigned long long meizu_init_wait_event;
				static unsigned int hist_n;
				unsigned long long u = kpc ? (unsigned long long)(kpc - vm_kernel_slide) : 0;
				meizu_init_kpc_unslid = u;
				if (u != 0) {
					int dup = 0;
					for (int h = 0; h < 4; h++) {
						if (meizu_init_kpc_hist[h] == u) {
							dup = 1;
						}
					}
					if (!dup) {
						meizu_init_kpc_hist[hist_n & 3] = u;
						hist_n++;
					}
				}
				{
					extern void meizu_init_sched_snapshot(void);
					meizu_init_sched_snapshot();
				}
				if (state >= 4 && quiet_seconds > 0 && (quiet_seconds % 10) == 0) {
					printf("meizu: init ku=%llx h=%llx,%llx,%llx,%llx ts=%x we=%llx slide=%llx\n",
					    u, meizu_init_kpc_hist[0], meizu_init_kpc_hist[1],
					    meizu_init_kpc_hist[2], meizu_init_kpc_hist[3],
					    meizu_init_tstate,
					    meizu_init_wait_event ? meizu_init_wait_event - vm_kernel_slide : 0ULL,
					    (unsigned long long)vm_kernel_slide);
				}
			}
			uint64_t report_elapsed_ns = 0;
			absolutetime_to_nanoseconds(now - last_report_time, &report_elapsed_ns);
			if (last_report_time == 0 || report_elapsed_ns >= NSEC_PER_SEC) {
				static const char *states[] = {"UNKNOWN", "ACTIVE", "USER-PC", "SAMPLE-SAME", "KERNEL-PC", "KSAMPLE-SAME", "KERNEL-IRQ", "NO-IRQ"};
				printf("meizu: progress state=%s quiet=%d init_sys=%d last_sys=%d init_pc=%llx irq=%llu np=%d nf=%llx nm=%llx nk=%d\n",
				    states[state], quiet_seconds, count, meizu_init_last_syscall, pc, init_irq,
				    meizu_init_np_stage, meizu_init_np_sr_flags,
				    meizu_init_np_map_in_progress, meizu_init_np_kr);
				last_report_time = now;
			}
		}
		tsleep((caddr_t)&meizu_dump_tick, PZERO, "meizu_autolog", hz / 20);
	}
}

static void
meizu_autolog_start(void)
{
	thread_t thread;
	if (kernel_thread_start(meizu_autolog_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
		printf("meizu: autolog thread started\n");
	}
}

/**
 * Map the dyld shared cache header at the shared region base in the *current* address
 * space. Called lazily from shared_region_check_np so the mapping lands in the map the
 * exec created, not in the pre-exec one. Returns the base address, 0 on failure.
 */
unsigned long long
meizu_premap_cache(void)
{
	extern unsigned long long meizu_premapped_cache;
	vnode_t vp = NULLVP;
	vm_map_t map = current_map();
	mach_vm_offset_t at = 0x0000000180000000ULL;
	mach_vm_size_t size = 0x60000;
	kern_return_t kr;
	uint8_t *buf;
	uio_t uio;
	int err;

	if (meizu_premapped_cache != 0) {
		return meizu_premapped_cache;
	}
	if (vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",
	    0, &vp, vfs_context_current()) != 0 || vp == NULLVP) {
		return 0;
	}
	if (VNOP_OPEN(vp, FREAD, vfs_context_current()) != 0) {
		vnode_put(vp);
		return 0;
	}
	buf = kalloc_data(size, Z_WAITOK);
	if (buf == NULL) {
		(void)VNOP_CLOSE(vp, FREAD, vfs_context_current());
		vnode_put(vp);
		return 0;
	}
	uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
	uio_addiov(uio, CAST_USER_ADDR_T(buf), size);
	err = VNOP_READ(vp, uio, 0, vfs_context_current());
	uio_free(uio);
	(void)VNOP_CLOSE(vp, FREAD, vfs_context_current());
	vnode_put(vp);
	if (err != 0) {
		kfree_data(buf, size);
		return 0;
	}
	kr = mach_vm_allocate_kernel(map, &at, size, VM_FLAGS_FIXED, VM_KERN_MEMORY_NONE);
	if (kr == KERN_SUCCESS) {
		if (copyout(buf, at, size) == 0 &&
		    mach_vm_protect(map, at, size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) == KERN_SUCCESS) {
			meizu_premapped_cache = (unsigned long long)at;
			printf("meizu: premapped cache at %llx (lazy)\n", (unsigned long long)at);
		}
	} else {
		printf("meizu: premap allocate kr=%d\n", kr);
	}
	kfree_data(buf, size);
	return meizu_premapped_cache;
}

static int
native_owner_cache_prepare(proc_t p, user_addr_t scratch, user_addr_t *envp)
{
#ifndef XNU_OWNER_CACHE_VOLUME_UUID
#define XNU_OWNER_CACHE_VOLUME_UUID "8846FE78-2910-4297-AF97-D59BB6F8FC16"
#endif
	const char *volume = XNU_OWNER_CACHE_VOLUME_UUID;
#if defined(XNU_OWNER_UNION_SYSTEMOS) && XNU_OWNER_UNION_SYSTEMOS
#if !CONFIG_UNION_MOUNTS
#error The owner composition requires the original native union-mount implementation
#endif
	const char *mountpoint = "/";
#else
	const char *mountpoint = "/private/preboot";
#endif
	static char meizu_cache_env[128];
	static const char *meizu_cache_dirs[] = { "/private/preboot/System/Library/Caches/com.apple.dyld",
	    "/System/Library/Caches/com.apple.dyld", "/cores/System/Library/Caches/com.apple.dyld",
	    "/Volumes/System/Library/Caches/com.apple.dyld", "/private/tmp/System/Library/Caches/com.apple.dyld",
	    "/tmp/System/Library/Caches/com.apple.dyld" };
	int meizu_env_index = (meizu_probe_err[13] >= 0 && meizu_probe_err[13] < 5) ? meizu_probe_err[13] : 5;
	snprintf(meizu_cache_env, sizeof(meizu_cache_env), "DYLD_SHARED_CACHE_DIR=%s", meizu_cache_dirs[meizu_env_index]);
	const char *environment = meizu_cache_env;
	vnode_t original_os_root = rootvnode;
	native_owner_cache_original_root = (uintptr_t)original_os_root;
	mount_t root_mount = vnode_mount(original_os_root);
	vnode_t device_vnode = vfs_devvp(root_mount);
	dev_t root_device = device_vnode ? vnode_specrdev(device_vnode) : NODEV;
	dev_t selected = NODEV;
	vnode_t mounted_root = NULLVP;
	int error = EAUTH, root_authorized = 0, cache_authorized = 0;
	int32_t result = 0;
	user64_addr_t spec = scratch + 0x160;
	char device_path[64] = {0};
	uint8_t mount_data[0x150] = {0};
	uint64_t mount_flags = MNT_RDONLY;
	uint16_t mount_mode = 1;
	uint32_t anonymous_owner = 99;
#if 1
#if defined(XNU_OWNER_CRYPTEX_FILES) && XNU_OWNER_CRYPTEX_FILES
	const char *fallback = "DYLD_FALLBACK_LIBRARY_PATH=/private/preboot/usr/lib";
	user64_addr_t environment_vector[3] = {scratch + 0x400, scratch + 0x600, USER_ADDR_NULL};
#else
	user64_addr_t environment_vector[3] = {scratch + 0x400, scratch + 0x480, USER_ADDR_NULL};
#endif
#endif
	struct mount_args request = {.type = scratch + 0x100, .path = scratch + 0x120,
	    .flags = MNT_RDONLY, .data = scratch + 0x200};
	request.flags = (int)mount_flags;
#if defined(XNU_OWNER_UNION_SYSTEMOS) && XNU_OWNER_UNION_SYSTEMOS
	request.flags |= MNT_UNION;
#endif
	if (device_vnode) vnode_put(device_vnode);
	/* meizu: choose the no-ignition dyld path before anything else in this chain. dyld
	 * otherwise asks the kernel to map the shared cache through the ignition payload and
	 * gives up silently when that mapping is unavailable. */
	dyld_flags |= UINT64_C(8);
	commpage_update_dyld_flags(dyld_flags);
	printf("meizu: commpage dyld_flags=%llx (skip ignition)\n", (unsigned long long)dyld_flags);
	root_authorized = (vfs_flags(root_mount) & MNT_RDONLY) && root_device != NODEV &&
	    native_verified_block_root_authorized(root_device);
	if (!root_authorized) goto done;
	native_owner_cache_stage = 1;
	if (!native_verified_block_volume_device(volume, &selected, device_path,
	    sizeof(device_path))) goto done;
	strlcpy(native_owner_cache_device_path, device_path, sizeof(native_owner_cache_device_path));
	native_owner_cache_selected_device = selected;
	native_owner_cache_stage = 2;
	/* Exact user-mount payload already qualified against this original APFS. */
	memcpy(mount_data, &spec, sizeof(spec));
	memcpy(mount_data + 8, &mount_flags, sizeof(mount_flags));
	memcpy(mount_data + 16, &mount_mode, sizeof(mount_mode));
	memcpy(mount_data + 0x144, &anonymous_owner, sizeof(anonymous_owner));
	memcpy(mount_data + 0x148, &anonymous_owner, sizeof(anonymous_owner));
	printf("meizu: cache writes begin type=%llx path=%llx spec=%llx data=%llx\n",
	    (unsigned long long)request.type, (unsigned long long)request.path,
	    (unsigned long long)spec, (unsigned long long)request.data);
	error = copyout("apfs", request.type, 5);
	printf("meizu: cache copy type err=%d\n", error);
	if (!error) {
		error = copyout(mountpoint, request.path, strlen(mountpoint) + 1);
		printf("meizu: cache copy path err=%d\n", error);
	}
	if (!error) {
		error = copyout(device_path, spec, strlen(device_path) + 1);
		printf("meizu: cache copy dev err=%d\n", error);
	}
	if (!error) {
		error = copyout(mount_data, request.data, sizeof(mount_data));
		printf("meizu: cache copy data err=%d\n", error);
	}
	if (error) goto done;
	native_owner_cache_stage = 3;
	printf("meizu: owner cache mount enter dev=%x type=%.5s path=%s\n",
	    (unsigned)selected, "apfs", mountpoint);
	error = mount(p, &request, &result);
	printf("meizu: owner cache mount done err=%d result=%d\n", error, result);
	native_owner_cache_mount_status = error;
	if (error) goto done;
	native_owner_cache_stage = 4;
	if (!strcmp(mountpoint, "/") && original_os_root->v_mountedhere) {
		extern lck_rw_t rootvnode_rw_lock;
		vnode_t newdp = NULLVP;
		if (VFS_ROOT(original_os_root->v_mountedhere, &newdp, vfs_context_current()) == 0 && newdp) {
			vnode_t tvp;
			vnode_ref(newdp);
			lck_rw_lock_exclusive(&rootvnode_rw_lock);
			tvp = rootvnode;
			rootvnode = newdp;
			lck_rw_unlock_exclusive(&rootvnode_rw_lock);
			vnode_rele(tvp);
			vnode_put(newdp);
			printf("meizu: rootvnode swapped to union root\n");
		} else {
			printf("meizu: rootvnode swap failed\n");
		}
	}
	error = vnode_lookup(mountpoint, 0, &mounted_root, vfs_context_current());
	if (error) goto done;
	mount_t cache_mount = vnode_mount(mounted_root);
	device_vnode = vfs_devvp(cache_mount);
	dev_t actual = device_vnode ? vnode_specrdev(device_vnode) : NODEV;
	if (device_vnode) vnode_put(device_vnode);
	native_owner_cache_mounted_device = actual;
	int readonly = (vfs_flags(cache_mount) & MNT_RDONLY) != 0;
	native_owner_cache_readonly = readonly;
	native_owner_cache_actual_mount_flags = vfs_flags(cache_mount);
	strlcpy(meizu_cache_mntfrom, vfs_statfs(cache_mount)->f_mntfromname, sizeof(meizu_cache_mntfrom));
	strlcpy(meizu_cache_fstype, vfs_statfs(cache_mount)->f_fstypename, sizeof(meizu_cache_fstype));
	printf("meizu: sysos dev=%x sel=%x from=%s fs=%s ro=%d\n", (unsigned)actual, (unsigned)selected,
	    meizu_cache_mntfrom, meizu_cache_fstype, readonly);
	native_owner_cache_current_root = (uintptr_t)rootvnode;
	int native_union = (vfs_flags(cache_mount) & MNT_UNION) &&
	    vnode_mountedhere(original_os_root) == cache_mount && rootvnode == mounted_root;
	native_owner_cache_native_union = native_union;
	/* meizu: APFS publishes a synthesized volume device for the window it mounts, so the
	 * mounted dev_t differs from the authenticated window dev_t on this board. The BSD
	 * name from the authenticated lookup is the trustworthy anchor: when the mount sits
	 * on that exact device the volume is the one we verified. */
	int same_device = 0;
	{
		extern int native_verified_block_device_authenticated(dev_t, const char *);
		same_device = (actual == selected) ||
		    native_verified_block_device_authenticated(actual, volume);
		cache_authorized = readonly && same_device &&
		    !strcmp(vfs_statfs(cache_mount)->f_fstypename, "apfs");
	}
#if defined(XNU_OWNER_UNION_SYSTEMOS) && XNU_OWNER_UNION_SYSTEMOS
	cache_authorized = cache_authorized && native_union;
#endif
	/* meizu: the probes below never ran because this early return skipped them,
	 * which is why every vfs/vfs4 field kept its initial value on every boot.
	 * Report the four terms that decide it, then run the probes regardless. */
	printf("meizu: auth ro=%d same_dev=%d is_apfs=%d native_union=%d -> authorized=%d unionflags=%x mountedhere=%d rootmatch=%d\n",
	    readonly, same_device, !strcmp(vfs_statfs(cache_mount)->f_fstypename, "apfs"),
	    native_union, cache_authorized, (unsigned)(vfs_flags(cache_mount) & MNT_UNION),
	    vnode_mountedhere(original_os_root) == cache_mount, rootvnode == mounted_root);
	if (!cache_authorized) { error = EAUTH; }
	native_owner_cache_stage = 5;
	{
		vnode_t v = NULLVP;
		int e;
		e = vnode_lookup("/System", 0, &v, vfs_context_current()); meizu_probe_err[5] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/private/preboot", 0, &v, vfs_context_current()); meizu_probe_err[6] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/usr/lib/libSystem.B.dylib", 0, &v, vfs_context_current()); meizu_probe_err[0] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/System/Library/Caches/com.apple.dyld", 0, &v, vfs_context_current()); meizu_probe_err[1] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64", 0, &v, vfs_context_current()); meizu_probe_err[4] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e", 0, &v, vfs_context_current()); meizu_probe_err[2] = e;
		if (e == 0 && v) { vnode_put(v); }
		v = NULLVP;
		e = vnode_lookup("/sbin/launchd", 0, &v, vfs_context_current()); meizu_probe_err[3] = e;
		if (e == 0 && v) { vnode_put(v); }
		{
			static const char *cand[] = { "/System/Cryptexes/OS", "/cores", "/Volumes", "/private/tmp", "/tmp", NULL };
			int ci;
			meizu_probe_err[13] = -1;
			meizu_probe_err[14] = -1;
			for (ci = 0; cand[ci]; ci++) {
				vnode_t dv = NULLVP;
				int de = vnode_lookup(cand[ci], 0, &dv, vfs_context_current());
				if (de == 0 && dv != NULLVP) {
					struct mount_args req3 = request;
					int mres = 0;
					int me2 = copyout(cand[ci], request.path, strlen(cand[ci]) + 1);
					if (me2 == 0) {
						req3.flags = MNT_RDONLY;
						me2 = mount(p, &req3, &mres);
					}
					meizu_probe_err[14] = me2;
					vnode_put(dv);
					if (me2 == 0) {
						meizu_probe_err[13] = ci;
						break;
					}
				}
			}
		}
		{
			vnode_t cv = NULLVP;
			int ce = vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64", 0, &cv, vfs_context_current());
			meizu_probe_err[9] = ce;
			if (ce == 0 && cv) {
				struct vnode_attr va;
				uint8_t hdr[8];
				uio_t uio;
				int rr;
				VATTR_INIT(&va);
				VATTR_WANTED(&va, va_data_size);
				if (vnode_getattr(cv, &va, vfs_context_current()) == 0) {
					meizu_probe_err[10] = (int)(va.va_data_size >> 20);
				} else {
					meizu_probe_err[10] = -1;
				}
				memset(hdr, 0, sizeof(hdr));
				uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
				uio_addiov(uio, CAST_USER_ADDR_T(hdr), sizeof(hdr));
				rr = VNOP_READ(cv, uio, 0, vfs_context_current());
				uio_free(uio);
				meizu_probe_err[11] = rr;
				{
					static const char *szp[] = { "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",
					    "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.01", NULL };
					{
						vnode_t mv = NULLVP;
						struct vnode_attr mva;
						int me = vnode_lookup("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",
						    0, &mv, vfs_context_current());
						if (me == 0 && mv != NULLVP) {
							VATTR_INIT(&mva);
							VATTR_WANTED(&mva, va_data_size);
							if (vnode_getattr(mv, &mva, vfs_context_current()) == 0) {
								meizu_uc_size = (unsigned long long)mva.va_data_size;
								printf("meizu: union cache size=%llu bytes\n",
								    (unsigned long long)mva.va_data_size);
							} else {
								printf("meizu: union cache getattr failed\n");
							}
							{
								static uint8_t ub[0x4000];
								uio_t uu = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
								int ur;
								memset(ub, 0, sizeof(ub));
								uio_addiov(uu, CAST_USER_ADDR_T(ub), 0x4000);
								ur = VNOP_READ(mv, uu, 0, vfs_context_current());
								uio_free(uu);
								meizu_uc_read = ur;
								meizu_uc_hdr = ((unsigned)ub[0] << 24) | ((unsigned)ub[1] << 16) |
								    ((unsigned)ub[2] << 8) | (unsigned)ub[3];
								printf("meizu: union cache read=%d got=%u hdr=%02x%02x%02x%02x\n",
								    ur, (unsigned)(0x4000 - uio_resid(uu)), ub[0], ub[1], ub[2], ub[3]);
							}
							vnode_put(mv);
						} else {
							printf("meizu: union cache lookup=%d\n", me);
						}
					}
					int si;
					for (si = 0; szp[si]; si++) {
						vnode_t sv = NULLVP;
						struct vnode_attr sva;
						int se = vnode_lookup(szp[si], 0, &sv, vfs_context_current());
						if (se == 0 && sv != NULLVP) {
							VATTR_INIT(&sva);
							VATTR_WANTED(&sva, va_data_size);
							VATTR_WANTED(&sva, va_flags);
							VATTR_WANTED(&sva, va_filerev);
							if (vnode_getattr(sv, &sva, vfs_context_current()) == 0) {
								meizu_probe_err[0 + si] = (int)(sva.va_data_size >> 10);
								meizu_probe_err[4 + si] = (int)(sva.va_flags & 0xffff);
								meizu_probe_err[6 + si] = (int)(sva.va_filerev & 0xffffff);
							} else {
								meizu_probe_err[0 + si] = -1;
							}
							vnode_put(sv);
						} else {
							meizu_probe_err[0 + si] = -2;
						}
					}
					meizu_probe_err[3] = 0;
				}
				meizu_probe_err[15] = ((int)hdr[0] << 24) | ((int)hdr[1] << 16) | ((int)hdr[2] << 8) | (int)hdr[3];
				vnode_put(cv);
			} else {
				meizu_probe_err[10] = -2;
				meizu_probe_err[11] = -2;
				meizu_probe_err[15] = -2;
			}
		}
	}
	/* Union mode must not inject DYLD_SHARED_CACHE_DIR: dyld does not fall back when the
	 * override directory fails to open, so a wrong env hides the real default path. */
#if !defined(XNU_OWNER_UNION_SYSTEMOS) || !XNU_OWNER_UNION_SYSTEMOS
	if ((error = copyout(environment, environment_vector[0], strlen(environment) + 1)) != 0 ||
	    (error = copyout("DYLD_PRINT_LIBRARIES=1", environment_vector[1], 23)) != 0 ||
	    (error = copyout(environment_vector, scratch + 0x500, sizeof(environment_vector))) != 0) goto done;
#if defined(XNU_OWNER_CRYPTEX_FILES) && XNU_OWNER_CRYPTEX_FILES
	if ((error = copyout(fallback, scratch + 0x600, strlen(fallback) + 1)) != 0) goto done;
#endif
	*envp = scratch + 0x500;
#else
	*envp = USER_ADDR_NULL;
#endif
	native_owner_cache_stage = 6;
done:
	if (mounted_root) vnode_put(mounted_root);
	printf("meizu: cache done enter error=%d stage=%d\n", error, native_owner_cache_stage);
	int root_recheck = root_authorized && native_verified_block_root_authorized(root_device);
	printf("meizu: cache done recheck=%d\n", root_recheck);
	native_owner_cache_root_recheck = root_recheck;
	if (!root_recheck) error = EAUTH;
	if (error) {
		printf("meizu: owner cache fail stage=%d err=%d root_auth=%d recheck=%d cache_auth=%d union=%d mount=%d readonly=%d sel=%x mounted=%x path=%s\n",
		    native_owner_cache_stage, error, root_authorized, root_recheck, cache_authorized,
		    native_owner_cache_native_union, native_owner_cache_mount_status, native_owner_cache_readonly,
		    (unsigned)native_owner_cache_selected_device, (unsigned)native_owner_cache_mounted_device,
		    native_owner_cache_device_path);
	}
#if defined(XNU_OWNER_BOOT_MANIFEST) && XNU_OWNER_BOOT_MANIFEST
	if (!error && cache_authorized && root_recheck &&
	    !native_owner_publish_boot_manifest(root_device, "DADD93F8-DBBF-4B17-A1DF-0F11E64172F7",
	        selected, volume)) {
		extern volatile int native_owner_boot_manifest_status;
		printf("meizu: boot manifest publish failed status=%d root=%x cache=%x\n",
		    native_owner_boot_manifest_status, (unsigned)root_device, (unsigned)selected);
		error = EAUTH;
	}
#endif
#if defined(XNU_OWNER_CACHE_STARTUP) && XNU_OWNER_CACHE_STARTUP
	if (!error && cache_authorized && root_recheck) {
		/* Original dyld bit 3 selects no ignition; the owner chain and actual
		 * readonly mounts above supply image authentication in this mode. */
		dyld_flags |= UINT64_C(8);
		commpage_update_dyld_flags(dyld_flags);
		native_owner_cache_dyld_flags = dyld_flags;
	}
#endif
	native_owner_cache_authorized = cache_authorized;
	native_owner_cache_status = error;
	if (error) {
		extern volatile int native_owner_boot_manifest_status;
		printf("meizu: owner cache end stage=%d err=%d root_auth=%d recheck=%d cache_auth=%d union=%d mount=%d manifest=%d\n",
		    native_owner_cache_stage, error, root_authorized, root_recheck, cache_authorized,
		    native_owner_cache_native_union, native_owner_cache_mount_status,
		    native_owner_boot_manifest_status);
	}
	native_owner_cache_checkpoint(error, cache_authorized);
	return error;
}
#endif

#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT
#if !defined(XNU_OWNER_BOOT_MANIFEST) || !XNU_OWNER_BOOT_MANIFEST
#error Scratch Data mounting requires the authenticated original OS startup path
#endif
#if defined(XNU_NATIVE_RAM_BLOCK) && XNU_NATIVE_RAM_BLOCK
#ifndef XNU_OWNER_DATA_VOLUME_UUID
#error Supply the observed scratch volume UUID for the IOKit backend
#endif
extern int native_ram_block_volume_seed(const char *, dev_t *, char *, size_t, uint64_t *, uint64_t *, uint8_t *);
#endif
extern dev_t mdevlookup(int);
extern int mdevgetrange(int, uint64_t *, uint64_t *);
volatile int native_owner_data_status = -1;
volatile int native_owner_data_stage = 0;
static int meizu_data_point_probe = -99999;
static int meizu_data_node_probe = -99999;
static int meizu_data_node_type = -1;
static unsigned meizu_data_node_dev = 0xffffffffu;
static unsigned meizu_data_seed_head = 0;
static int meizu_data_seed_nxsb = -1;
static int meizu_data_point_type = -1;
static int meizu_data_read_error = -99999;
static unsigned meizu_data_read_head = 0;
volatile int native_owner_data_readonly = -1;
volatile dev_t native_owner_data_device = NODEV;
uint64_t native_owner_data_seed_base, native_owner_data_seed_size;
uint8_t native_owner_data_seed_sha256[32];
char native_owner_data_device_path[64];

/* Boot-time read-only union views of owner-provided GPU addon content over sealed system directories, mounted after the Data mount and
 * before launchd starts: the Adreno Metal plugin bundle over /System/Library/Extensions (the original Metal discovery loads
 * /System/Library/Extensions/<MetalPluginName>.bundle) and an owner launchd job over /System/Library/LaunchDaemons (a process outside
 * every sandbox profile). Only with boot-arg sheng-gpu-addon=1. Never panics: a missing source directory or a refused mount only
 * records its status. The backing directories are owner Data (not an authenticated immutable volume); code in them still has to
 * pass AMFI through the loaded trust cache. */
extern volatile int native_bindfs_boot_mount;
volatile int native_gpu_addon_stage = 0;
volatile int native_gpu_addon_status = -1;
volatile int native_gpu_addon2_stage = 0;
volatile int native_gpu_addon2_status = -1;
volatile int native_gpu_addon3_stage = 0;
volatile int native_gpu_addon3_status = -1;
static void
native_gpu_addon_mount(proc_t p, user_addr_t scratch, user_addr_t offset, const char *source, size_t source_size,
    const char *point, size_t point_size, const char *visible, const char *own, volatile int *stage, volatile int *status)
{
	vnode_t probe = NULLVP;
	int error;
	*stage = 1;
	error = vnode_lookup(source, 0, &probe, vfs_context_current());
	if (error) {
		*status = error;
		return;
	}
	vnode_put(probe);
	probe = NULLVP;
	*stage = 2;
	struct mount_args request = {.type = scratch + offset, .path = scratch + offset + 0x20,
	    .flags = MNT_RDONLY | MNT_UNION, .data = scratch + offset + 0x100};
	int32_t result = 0;
	if ((error = copyout("bindfs", request.type, 7)) || (error = copyout(point, request.path, point_size)) ||
	    (error = copyout(source, request.data, source_size))) {
		*status = error;
		return;
	}
	native_bindfs_boot_mount = 1;
	error = mount(p, &request, &result);
	native_bindfs_boot_mount = 0;
	if (error) {
		*status = error;
		return;
	}
	*stage = 3;
	/* The union must still show a sealed lower entry next to the owner's. */
	error = vnode_lookup(visible, 0, &probe, vfs_context_current());
	if (!error) {
		vnode_put(probe);
		probe = NULLVP;
		*stage = 4;
		/* ... and the owner's own entry must be visible through it (stage 5). */
		error = vnode_lookup(own, 0, &probe, vfs_context_current());
		if (!error) {
			vnode_put(probe);
			probe = NULLVP;
			*stage = 5;
		}
	}
	*status = error;
}
/* The GPU addon of the boot payload (scripts/make-gpu-addon.py, handed over by the loader as /chosen/memory-map/GpuAddon): every file
 * whose content or mode differs from the mounted Data volume is written there before the addon mounts, so the plugin no longer has to
 * be part of the Data seed and the seed's persistent store survives a plugin rebuild. Missing directories below /private/var/ngpu are
 * created. A malformed addon is refused as a whole; a file that cannot be written keeps its old content.
 * Counters for the boot log: files in the addon (-1 none), files written, files already equal, failures and the last error. */
extern int native_ram_block_gpu_addon(const uint8_t **, uint64_t *) __attribute__((weak_import));
#include <libkern/zlib.h>
/* zlib's state, from kalloc with its size in front (the default allocator of the kernel's zlib is not meant for this). */
static voidpf
native_gpu_addon_zalloc(voidpf opaque __unused, uInt items, uInt size)
{
	size_t bytes = (size_t)items * size + 16;
	uint8_t *block = kalloc_data(bytes, Z_WAITOK | Z_ZERO);
	if (!block) {
		return Z_NULL;
	}
	memcpy(block, &bytes, sizeof(bytes));
	return block + 16;
}
static void
native_gpu_addon_zfree(voidpf opaque __unused, voidpf address)
{
	uint8_t *block = (uint8_t *)address - 16;
	size_t bytes = 0;
	memcpy(&bytes, block, sizeof(bytes));
	kfree_data(block, bytes);
}
static int
native_gpu_addon_inflate(const uint8_t *packed, uint64_t packed_size, uint8_t *data, uint64_t size)
{
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	stream.zalloc = native_gpu_addon_zalloc;
	stream.zfree = native_gpu_addon_zfree;
	stream.next_in = (Bytef *)(uintptr_t)packed;
	stream.avail_in = (uInt)packed_size;
	stream.next_out = data;
	stream.avail_out = (uInt)size;
	if (inflateInit(&stream) != Z_OK) {
		return ENOMEM;
	}
	int result = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	return result == Z_STREAM_END && stream.total_out == size && !stream.avail_in ? 0 : EINVAL;
}
volatile int native_gpu_addon_files = -1;
volatile int native_gpu_addon_written = 0;
volatile int native_gpu_addon_same = 0;
volatile int native_gpu_addon_failed = 0;
volatile int native_gpu_addon_error = 0;
static int
native_gpu_addon_equal(vnode_t vp, const uint8_t *data, uint64_t size, uint32_t mode, vfs_context_t ctx)
{
	struct vnode_attr va;
	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	VATTR_WANTED(&va, va_mode);
	if (vnode_getattr(vp, &va, ctx) || va.va_data_size != size || (va.va_mode & 07777) != mode) {
		return 0;
	}
	const uint32_t chunk = 1024 * 1024;
	uint8_t *buffer = kalloc_data(chunk, Z_WAITOK);
	if (!buffer) {
		return 0;
	}
	int equal = 1;
	for (uint64_t at = 0; equal && at < size; at += chunk) {
		int length = (int)(size - at < chunk ? size - at : chunk), resid = 0;
		if (vn_rdwr(UIO_READ, vp, (caddr_t)buffer, length, (off_t)at, UIO_SYSSPACE, IO_NOAUTH, vfs_context_ucred(ctx), &resid,
		    vfs_context_proc(ctx)) || resid || memcmp(buffer, data + at, length)) {
			equal = 0;
		}
	}
	kfree_data(buffer, chunk);
	return equal;
}
static int
native_gpu_addon_write(const char *path, const uint8_t *data, uint64_t size, uint32_t mode, vfs_context_t ctx)
{
	vnode_t vp = NULLVP;
	int error = vnode_open(path, FREAD | O_NOFOLLOW, 0, 0, &vp, ctx);
	if (!error) {
		int equal = native_gpu_addon_equal(vp, data, size, mode, ctx);
		vnode_close(vp, FREAD, ctx);
		vp = NULLVP;
		if (equal) {
			return -1;
		}
	}
	error = vnode_open(path, O_CREAT | O_TRUNC | FWRITE | O_NOFOLLOW, mode, 0, &vp, ctx);
	if (error) {
		return error;
	}
	int resid = 0;
	error = vn_rdwr(UIO_WRITE, vp, (caddr_t)(uintptr_t)data, (int)size, 0, UIO_SYSSPACE, IO_NOAUTH | IO_UNIT, vfs_context_ucred(ctx),
	    &resid, vfs_context_proc(ctx));
	if (!error && resid) {
		error = EIO;
	}
	if (!error) {
		struct vnode_attr va;
		VATTR_INIT(&va);
		VATTR_SET(&va, va_mode, mode);
		error = vnode_setattr(vp, &va, ctx);
	}
	if (!error) {
		error = VNOP_FSYNC(vp, MNT_WAIT, ctx);
	}
	int close_error = vnode_close(vp, FWRITE, ctx);
	return error ? error : close_error;
}
/* Creates the missing parent directories of an addon path with the mkdir syscall. The scratch is one 4 KiB page; the mount arguments
 * end below 0xa00. */
static int
native_gpu_addon_parents(proc_t p, user_addr_t scratch, const char *path)
{
	char parent[192];
	strlcpy(parent, path, sizeof(parent));
	for (size_t i = sizeof("/private/var/ngpu/") - 1; parent[i]; ++i) {
		if (parent[i] != '/') {
			continue;
		}
		parent[i] = 0;
		struct mkdir_args request = {.path = scratch + 0xc00, .mode = 0755};
		int32_t result = 0;
		int error = copyout(parent, request.path, i + 1);
		if (!error) {
			error = mkdir(p, &request, &result);
		}
		parent[i] = '/';
		if (error && error != EEXIST) {
			return error;
		}
	}
	return 0;
}
static void
native_gpu_addon_install(proc_t p, user_addr_t scratch)
{
	const uint8_t *addon = NULL;
	uint64_t bytes = 0;
	if (!native_ram_block_gpu_addon || !native_ram_block_gpu_addon(&addon, &bytes)) {
		return;
	}
	uint32_t count = 0;
	uint64_t total = 0;
	if (bytes < 24 || memcmp(addon, "NGPUADD2", 8)) {
		native_gpu_addon_error = EINVAL;
		return;
	}
	memcpy(&count, addon + 8, 4);
	memcpy(&total, addon + 16, 8);
	if (total != bytes || !count || count > 4096) {
		native_gpu_addon_error = EINVAL;
		return;
	}
	/* Validate the whole addon before writing anything. */
	uint64_t at = 24;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t name_bytes = 0, mode = 0;
		uint64_t size = 0, packed = 0;
		if (bytes - at < 24) {
			native_gpu_addon_error = EINVAL;
			return;
		}
		memcpy(&name_bytes, addon + at, 4);
		memcpy(&mode, addon + at + 4, 4);
		memcpy(&size, addon + at + 8, 8);
		memcpy(&packed, addon + at + 16, 8);
		at += 24;
		uint64_t name_padded = (name_bytes + 7) & ~7ULL, size_padded = (packed + 7) & ~7ULL;
		const char *name = (const char *)(addon + at);
		if (name_bytes < 2 || name_bytes > 160 || bytes - at < name_padded || name[name_bytes - 1] || strnlen(name, name_bytes) != name_bytes - 1 ||
		    name[0] == '/' || (mode & ~0777) || size > INT32_MAX || !packed || packed > INT32_MAX || bytes - at - name_padded < size_padded) {
			native_gpu_addon_error = EINVAL;
			return;
		}
		for (uint32_t k = 0; k + 1 < name_bytes - 1; ++k) {
			if (name[k] == '.' && name[k + 1] == '.') {
				native_gpu_addon_error = EINVAL;
				return;
			}
		}
		at += name_padded + size_padded;
	}
	if (at != bytes) {
		native_gpu_addon_error = EINVAL;
		return;
	}
	native_gpu_addon_files = (int)count;
	vfs_context_t ctx = vfs_context_current();
	char path[192];
	at = 24;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t name_bytes = 0, mode = 0;
		uint64_t size = 0, packed = 0;
		memcpy(&name_bytes, addon + at, 4);
		memcpy(&mode, addon + at + 4, 4);
		memcpy(&size, addon + at + 8, 8);
		memcpy(&packed, addon + at + 16, 8);
		at += 24;
		const char *name = (const char *)(addon + at);
		at += (name_bytes + 7) & ~7ULL;
		snprintf(path, sizeof(path), "/private/var/ngpu/%s", name);
		uint8_t *data = kalloc_data(size ? size : 1, Z_WAITOK);
		int error = data ? native_gpu_addon_inflate(addon + at, packed, data, size) : ENOMEM;
		if (!error) {
			error = native_gpu_addon_write(path, data, size, mode, ctx);
			if (error == ENOENT && !(error = native_gpu_addon_parents(p, scratch, path))) {
				error = native_gpu_addon_write(path, data, size, mode, ctx);
			}
		}
		if (data) {
			kfree_data(data, size ? size : 1);
		}
		if (error < 0) {
			native_gpu_addon_same++;
		} else if (error) {
			native_gpu_addon_failed++;
			native_gpu_addon_error = error;
		} else {
			native_gpu_addon_written++;
		}
		at += (packed + 7) & ~7ULL;
	}
}
static void
native_gpu_addon_prepare(proc_t p, user_addr_t scratch)
{
	uint32_t enabled = 0;
	if (!PE_parse_boot_argn("sheng-gpu-addon", &enabled, sizeof(enabled)) || enabled != 1) {
		return;
	}
	native_gpu_addon_install(p, scratch);
	static const char extensions_source[] = "/private/var/ngpu/addons", extensions_point[] = "/System/Library/Extensions";
	static const char daemons_source[] = "/private/var/ngpu/daemons", daemons_point[] = "/System/Library/LaunchDaemons";
	native_gpu_addon_mount(p, scratch, 0x400, extensions_source, sizeof(extensions_source), extensions_point,
	    sizeof(extensions_point), "/System/Library/Extensions/AGXMetalA10.bundle",
	    "/System/Library/Extensions/NativeAdrenoMetal.bundle/Info.plist", &native_gpu_addon_stage,
	    &native_gpu_addon_status);
	native_gpu_addon_mount(p, scratch, 0x600, daemons_source, sizeof(daemons_source), daemons_point, sizeof(daemons_point),
	    "/System/Library/LaunchDaemons/com.apple.backboardd.plist",
	    "/System/Library/LaunchDaemons/org.blu-sharky.adrenoprobed.plist", &native_gpu_addon2_stage, &native_gpu_addon2_status);
	/* The default iPadOS 17 wallpaper's layered animation with PNG images in place of its HEIC ones (no hardware HEVC decoder here).
	 * The overlay carries the animation's documents and the PNG images; the sealed assets directory stays visible below it. */
	static const char wallpaper_source[] = "/private/var/ngpu/wallpaper",
	    wallpaper_point[] = "/Library/Wallpaper/Collections/iPadOS_17~ipad.wallpaperCollection/Wallpapers/"
	    "7570.iPadOS_17_Flat-1024w-1366h@2x~ipad.wallpaper/7570.iPadOS_17_Flat_Background-1024w-1366h@2x~ipad.ca";
	native_gpu_addon_mount(p, scratch, 0x800, wallpaper_source, sizeof(wallpaper_source), wallpaper_point, sizeof(wallpaper_point),
	    "/Library/Wallpaper/Collections/iPadOS_17~ipad.wallpaperCollection/Wallpapers/7570.iPadOS_17_Flat-1024w-1366h@2x~ipad.wallpaper/"
	    "7570.iPadOS_17_Flat_Background-1024w-1366h@2x~ipad.ca/assets/J120.HEIC",
	    "/Library/Wallpaper/Collections/iPadOS_17~ipad.wallpaperCollection/Wallpapers/7570.iPadOS_17_Flat-1024w-1366h@2x~ipad.wallpaper/"
	    "7570.iPadOS_17_Flat_Background-1024w-1366h@2x~ipad.ca/J120.png", &native_gpu_addon3_stage, &native_gpu_addon3_status);
}

/* The deletable system apps (Weather, Notes, Calendar, ...) ship in the OS volume's /private/var/staged_system_apps. A restore copies
 * them into the Data volume, where SystemAppMigrator installs them during the first-boot data migration; until then only the
 * placeholders in /System/Library/AppPlaceholders exist and the widgets of those apps stay blank. The owner Data mount hides the OS
 * directory, so with boot-arg sheng-staged-apps=1 it is copied once, through the vnode the Data mount covers, into Data's
 * /private/var/staged_system_apps before launchd starts. /private/var/db/.sheng-staged-apps marks a complete copy. Directories and
 * regular files only (the 21H461 set has nothing else), owned by mobile (uid and gid 501): installcoordinationd and the migration plugin,
 * which move and remove them, run as mobile.
 * Counters for the boot log: stage (1 copying, 2 copied, 3 copied before, -1 no
 * source, -2 out of memory), directories, files, MiB, failures and the last error. */
#include <sys/dirent.h>
volatile int native_staged_apps_stage = 0;
volatile int native_staged_apps_dirs = 0;
volatile int native_staged_apps_files = 0;
volatile int native_staged_apps_mib = 0;
volatile int native_staged_apps_failed = 0;
volatile int native_staged_apps_error = 0;
static uint64_t native_staged_apps_bytes;
char native_staged_apps_path[96];   /* the file being copied, for the boot log */
static const char native_staged_apps_marker[] = "/private/var/db/.sheng-staged-apps";
#define NATIVE_STAGED_APPS_OWNER 501
static void
native_staged_apps_fail(int error)
{
	native_staged_apps_failed++;
	native_staged_apps_error = error;
}
static int
native_staged_apps_mkdir(proc_t p, user_addr_t scratch, const char *path)
{
	/* The scratch is one 4 KiB page; the mount arguments above end below 0xa00. */
	struct mkdir_args request = {.path = scratch + 0xa00, .mode = 0755};
	int32_t result = 0;
	int error = copyout(path, request.path, strlen(path) + 1);
	if (!error) {
		error = mkdir(p, &request, &result);
	}
	if (error && error != EEXIST) {
		return error;
	}
	vnode_t vp = NULLVP;
	error = vnode_lookup(path, VNODE_LOOKUP_NOFOLLOW, &vp, vfs_context_current());
	if (!error) {
		struct vnode_attr va;
		VATTR_INIT(&va);
		VATTR_SET(&va, va_uid, NATIVE_STAGED_APPS_OWNER);
		VATTR_SET(&va, va_gid, NATIVE_STAGED_APPS_OWNER);
		error = vnode_setattr(vp, &va, vfs_context_current());
		vnode_put(vp);
	}
	return error;
}
/* Reads to the end of the source rather than trusting a size: most of these files are compressed on the OS volume. */
static int
native_staged_apps_file(vnode_t source, const char *path, int mode, uint8_t *buffer, uint32_t chunk, vfs_context_t ctx)
{
	int error = VNOP_OPEN(source, FREAD, ctx);
	if (error) {
		return error;
	}
	vnode_t vp = NULLVP;
	size_t length = strlen(path);
	strlcpy(native_staged_apps_path, path + (length > sizeof(native_staged_apps_path) - 1 ? length - (sizeof(native_staged_apps_path) - 1) : 0),
	    sizeof(native_staged_apps_path));
	error = vnode_open(path, O_CREAT | O_TRUNC | FWRITE | O_NOFOLLOW, mode, 0, &vp, ctx);
	for (off_t at = 0; !error;) {
		int resid = 0;
		error = vn_rdwr(UIO_READ, source, (caddr_t)buffer, (int)chunk, at, UIO_SYSSPACE, IO_NOAUTH, vfs_context_ucred(ctx), &resid,
		    vfs_context_proc(ctx));
		int length = (int)chunk - resid;
		if (error || !length) {
			break;
		}
		error = vn_rdwr(UIO_WRITE, vp, (caddr_t)buffer, length, at, UIO_SYSSPACE, IO_NOAUTH | IO_UNIT, vfs_context_ucred(ctx), &resid,
		    vfs_context_proc(ctx));
		if (!error && resid) {
			error = EIO;
		}
		at += length;
		native_staged_apps_bytes += (uint64_t)length;
	}
	if (vp) {
		if (!error) {
			struct vnode_attr va;
			VATTR_INIT(&va);
			VATTR_SET(&va, va_mode, mode);
			VATTR_SET(&va, va_uid, NATIVE_STAGED_APPS_OWNER);
			VATTR_SET(&va, va_gid, NATIVE_STAGED_APPS_OWNER);
			error = vnode_setattr(vp, &va, ctx);
		}
		/* Each file goes to the medium before the next: without it the copy piled up dirty pages until it stalled (int-220). */
		if (!error) {
			error = VNOP_FSYNC(vp, MNT_WAIT, ctx);
		}
		int close_error = vnode_close(vp, FWRITE, ctx);
		if (!error) {
			error = close_error;
		}
	}
	VNOP_CLOSE(source, FREAD, ctx);
	return error;
}
/* Directories still to copy, as paths relative to the staged directory: the walk is iterative, because APFS operations need much of the
 * kernel stack and a recursion as deep as the app bundles overflowed it (int-221). */
struct native_staged_apps_pending {
	char *pool;
	uint32_t *at;
	uint32_t used, count;
};
#define NATIVE_STAGED_APPS_POOL (2 * 1024 * 1024)
#define NATIVE_STAGED_APPS_DIRS 16384
/* Copies the entries of DIR into the Data directory PATH (appending each name to PATH and cutting it off again) and queues its
 * subdirectories, whose source-relative paths start at REL inside PATH. */
static void
native_staged_apps_dir(proc_t p, user_addr_t scratch, vnode_t dir, char *path, size_t path_size, size_t rel,
    struct native_staged_apps_pending *pending, uint8_t *entries, uint32_t entries_size, uint8_t *buffer, uint32_t chunk, vfs_context_t ctx)
{
	size_t base = strlen(path);
	int error = VNOP_OPEN(dir, FREAD, ctx);
	if (error) {
		native_staged_apps_fail(error);
		return;
	}
	off_t offset = 0;
	for (int eof = 0; !error && !eof;) {
		uio_t uio = uio_create(1, offset, UIO_SYSSPACE, UIO_READ);
		if (!uio) {
			error = ENOMEM;
			break;
		}
		uio_addiov(uio, CAST_USER_ADDR_T(entries), entries_size);
		int count = 0;
		error = vnode_readdir64(dir, uio, VNODE_READDIR_EXTENDED, &eof, &count, ctx);
		uint32_t filled = entries_size - (uint32_t)uio_resid(uio);
		offset = uio_offset(uio);
		uio_free(uio);
		if (error || !filled) {
			break;
		}
		for (uint32_t at = 0; at + offsetof(struct direntry, d_name) < filled;) {
			struct direntry *entry = (struct direntry *)(entries + at);
			if (!entry->d_reclen || at + entry->d_reclen > filled) {
				error = EINVAL;
				break;
			}
			at += entry->d_reclen;
			const char *name = entry->d_name;
			if (!entry->d_ino || !strcmp(name, ".") || !strcmp(name, "..")) {
				continue;
			}
			if (base + 1 + strlen(name) >= path_size) {
				native_staged_apps_fail(ENAMETOOLONG);
				continue;
			}
			path[base] = '/';
			strlcpy(path + base + 1, name, path_size - base - 1);
			vnode_t child = NULLVP;
			int child_error = vnode_lookupat(name, VNODE_LOOKUP_NOFOLLOW | VNODE_LOOKUP_NOCROSSMOUNT, &child, ctx, dir);
			if (!child_error) {
				struct vnode_attr va;
				VATTR_INIT(&va);
				VATTR_WANTED(&va, va_mode);
				child_error = vnode_getattr(child, &va, ctx);
				int mode = VATTR_IS_SUPPORTED(&va, va_mode) ? (int)(va.va_mode & 0777) : 0644;
				if (child_error) {
				} else if (vnode_vtype(child) == VDIR) {
					child_error = native_staged_apps_mkdir(p, scratch, path);
					size_t length = strlen(path + rel) + 1;
					if (!child_error && (pending->count == NATIVE_STAGED_APPS_DIRS || NATIVE_STAGED_APPS_POOL - pending->used < length)) {
						child_error = ENOSPC;
					}
					if (!child_error) {
						memcpy(pending->pool + pending->used, path + rel, length);
						pending->at[pending->count++] = pending->used;
						pending->used += (uint32_t)length;
						native_staged_apps_dirs++;
					}
				} else if (vnode_vtype(child) == VREG) {
					child_error = native_staged_apps_file(child, path, mode, buffer, chunk, ctx);
					if (!child_error) {
						native_staged_apps_files++;
						native_staged_apps_mib = (int)(native_staged_apps_bytes >> 20);
					}
				} else {
					child_error = ENOTSUP;
				}
				vnode_put(child);
			}
			if (child_error) {
				native_staged_apps_fail(child_error);
			}
			path[base] = '\0';
		}
	}
	if (error) {
		native_staged_apps_fail(error);
	}
	VNOP_CLOSE(dir, FREAD, ctx);
}
/* Copies the tree below SOURCE into the Data directory PATH, which exists. */
static void
native_staged_apps_walk(proc_t p, user_addr_t scratch, vnode_t source, char *path, size_t path_size, uint8_t *buffer, uint32_t chunk,
    vfs_context_t ctx)
{
	const uint32_t entries_size = 8192;
	struct native_staged_apps_pending pending = {
		.pool = kalloc_data(NATIVE_STAGED_APPS_POOL, Z_WAITOK),
		.at = kalloc_data(NATIVE_STAGED_APPS_DIRS * sizeof(uint32_t), Z_WAITOK),
	};
	uint8_t *entries = kalloc_data(entries_size, Z_WAITOK);
	size_t root = strlen(path);
	if (pending.pool && pending.at && entries) {
		native_staged_apps_dir(p, scratch, source, path, path_size, root + 1, &pending, entries, entries_size, buffer, chunk, ctx);
		while (pending.count) {
			const char *rel = pending.pool + pending.at[--pending.count];
			if (root + 1 + strlen(rel) >= path_size) {
				native_staged_apps_fail(ENAMETOOLONG);
				continue;
			}
			path[root] = '/';
			strlcpy(path + root + 1, rel, path_size - root - 1);
			vnode_t dir = NULLVP;
			int error = vnode_lookupat(rel, VNODE_LOOKUP_NOFOLLOW | VNODE_LOOKUP_NOCROSSMOUNT, &dir, ctx, source);
			if (!error) {
				native_staged_apps_dir(p, scratch, dir, path, path_size, root + 1, &pending, entries, entries_size, buffer, chunk, ctx);
				vnode_put(dir);
			} else {
				native_staged_apps_fail(error);
			}
			path[root] = '\0';
		}
	} else {
		native_staged_apps_fail(ENOMEM);
	}
	if (entries) {
		kfree_data(entries, entries_size);
	}
	if (pending.at) {
		kfree_data(pending.at, NATIVE_STAGED_APPS_DIRS * sizeof(uint32_t));
	}
	if (pending.pool) {
		kfree_data(pending.pool, NATIVE_STAGED_APPS_POOL);
	}
}
static void
native_staged_apps_prepare(proc_t p, user_addr_t scratch, mount_t data_mount)
{
	uint32_t enabled = 0;
	if (!PE_parse_boot_argn("sheng-staged-apps", &enabled, sizeof(enabled)) || enabled != 1) {
		return;
	}
	vfs_context_t ctx = vfs_context_current();
	vnode_t vp = NULLVP, covered = data_mount->mnt_vnodecovered;
	if (!vnode_lookup(native_staged_apps_marker, VNODE_LOOKUP_NOFOLLOW, &vp, ctx)) {
		vnode_put(vp);
		native_staged_apps_stage = 3;
		return;
	}
	int error = covered ? vnode_getwithref(covered) : ENOENT;
	if (!error) {
		error = vnode_lookupat("staged_system_apps", VNODE_LOOKUP_NOFOLLOW | VNODE_LOOKUP_NOCROSSMOUNT, &vp, ctx, covered);
		vnode_put(covered);
	}
	if (!error && vnode_vtype(vp) != VDIR) {
		vnode_put(vp);
		error = ENOTDIR;
	}
	if (error) {
		native_staged_apps_error = error;
		native_staged_apps_stage = -1;
		return;
	}
	const uint32_t chunk = 1024 * 1024;
	uint8_t *buffer = kalloc_data(chunk, Z_WAITOK);
	char *path = kalloc_data(MAXPATHLEN, Z_WAITOK);
	if (buffer && path) {
		native_staged_apps_stage = 1;
		strlcpy(path, "/private/var/staged_system_apps", MAXPATHLEN);
		error = native_staged_apps_mkdir(p, scratch, path);
		if (error) {
			native_staged_apps_fail(error);
		} else {
			native_staged_apps_walk(p, scratch, vp, path, MAXPATHLEN, buffer, chunk, ctx);
		}
		if (!native_staged_apps_failed) {
			vnode_t marker = NULLVP;
			error = vnode_open(native_staged_apps_marker, O_CREAT | O_TRUNC | FWRITE | O_NOFOLLOW, 0644, 0, &marker, ctx);
			if (!error) {
				error = vnode_close(marker, FWRITE, ctx);
			}
			if (error) {
				native_staged_apps_fail(error);
			} else {
				native_staged_apps_stage = 2;
			}
		}
	} else {
		native_staged_apps_stage = -2;
	}
	if (buffer) {
		kfree_data(buffer, chunk);
	}
	if (path) {
		kfree_data(path, MAXPATHLEN);
	}
	vnode_put(vp);
}

static int
native_owner_data_prepare(proc_t p, user_addr_t scratch)
{
	uint64_t base = 0, size = 0;
	dev_t device = NODEV;
	int error = EINVAL;
	vnode_t data_root = NULLVP;
	char device_path[64] = "/dev/md0";
#if defined(XNU_NATIVE_RAM_BLOCK) && XNU_NATIVE_RAM_BLOCK
	/* The typed provider supplies logical seed identity for both dense and
	 * sparse media. A sparse disk has no contiguous logical-byte address. */
	if (!native_ram_block_volume_seed(XNU_OWNER_DATA_VOLUME_UUID, &device, device_path,
	    sizeof(device_path), &base, &size, native_owner_data_seed_sha256)) { error = ENOENT; goto done; }
	/* The typed provider's disk may be far larger than 2 GiB (a persistent Data disk keeps only its seed's nonzero pages). */
	if (!size || size > UINT64_C(0x10000000000) || (size & 4095)) goto done;
#else
	device = mdevlookup(0);
	if (device == NODEV || mdevgetrange(0, &base, &size) || base < VM_MIN_KERNEL_ADDRESS ||
	    !size || size > UINT64_C(0x80000000) || (base & 4095) || (size & 4095)) goto done;
	SHA256_CTX context;
	SHA256_Init(&context);
	SHA256_Update(&context, (const void *)base, size);
	SHA256_Final(native_owner_data_seed_sha256, &context);
#endif
	native_owner_data_seed_base = base;
	native_owner_data_seed_size = size;
	native_owner_data_stage = 1;
	strlcpy(native_owner_data_device_path, device_path, sizeof(native_owner_data_device_path));
	uint8_t payload[0x150] = {0};
	user64_addr_t spec = scratch + 0x160;
	uint16_t mode = 1;
	uint32_t anonymous_owner = 99;
	memcpy(payload, &spec, sizeof(spec));
	memcpy(payload + 16, &mode, sizeof(mode));
	memcpy(payload + 0x144, &anonymous_owner, sizeof(anonymous_owner));
	memcpy(payload + 0x148, &anonymous_owner, sizeof(anonymous_owner));
	struct mount_args request = {.type = scratch + 0x100, .path = scratch + 0x120,
	    .flags = 0, .data = scratch + 0x200};
	int32_t result = 0;
	if ((error = copyout("apfs", request.type, 5)) ||
	    (error = copyout("/private/var", request.path, 13)) ||
	    (error = copyout(device_path, spec, strlen(device_path) + 1)) ||
	    (error = copyout(payload, request.data, sizeof(payload)))) goto done;
	native_owner_data_stage = 2;
	{
		vnode_t probe = NULLVP;
		int point_error = vnode_lookup("/private/var", 0, &probe, vfs_context_current());
		if (!point_error) {
			meizu_data_point_type = vnode_vtype(probe);
			vnode_put(probe);
		}
		probe = NULLVP;
		int node_error = vnode_lookup(device_path, 0, &probe, vfs_context_current());
		if (!node_error) {
			uint8_t read_buffer[64];
			int read_resid = 0;
			meizu_data_node_type = vnode_vtype(probe);
			meizu_data_node_dev = (unsigned)vnode_specrdev(probe);
			meizu_data_read_error = vn_rdwr(UIO_READ, probe, (caddr_t)read_buffer,
			    (int)sizeof(read_buffer), 0, UIO_SYSSPACE, IO_NODELOCKED,
			    vfs_context_ucred(vfs_context_current()), &read_resid, p);
			if (!meizu_data_read_error) {
				meizu_data_read_head = ((unsigned)read_buffer[0x20] << 24) | ((unsigned)read_buffer[0x21] << 16) |
				    ((unsigned)read_buffer[0x22] << 8) | read_buffer[0x23];
			}
			vnode_put(probe);
		}
		meizu_data_point_probe = point_error;
		meizu_data_node_probe = node_error;
	}
#if !defined(XNU_NATIVE_RAM_BLOCK) || !XNU_NATIVE_RAM_BLOCK
	{
		const uint8_t *head = (const uint8_t *)base;
		meizu_data_seed_head = ((unsigned)head[0] << 24) | ((unsigned)head[1] << 16) |
		    ((unsigned)head[2] << 8) | head[3];
		meizu_data_seed_nxsb = (head[0x20] == 'N' && head[0x21] == 'X' && head[0x22] == 'S' && head[0x23] == 'B');
	}
#endif
	error = mount(p, &request, &result);
	printf("meizu: data mount rw error=%d result=%d\n", error, result);
	if (error) {
		int32_t readonly_result = 0;
		request.flags = MNT_RDONLY;
		int readonly_error = mount(p, &request, &readonly_result);
		printf("meizu: data mount ro error=%d result=%d\n", readonly_error, readonly_result);
		if (!readonly_error) {
			request.flags = 0;
			error = 0;
		} else {
			int32_t probe_result = 0;
			uint8_t alt_payload[0x150] = {0};
			user64_addr_t alt_spec = scratch + 0x160;
			uint16_t mode_zero = 0;
			(void)copyout("/private/var", request.path, 13);
			memcpy(alt_payload, &alt_spec, sizeof(alt_spec));
			memcpy(alt_payload + 16, &mode_zero, sizeof(mode_zero));
			memcpy(alt_payload + 0x144, &anonymous_owner, sizeof(anonymous_owner));
			memcpy(alt_payload + 0x148, &anonymous_owner, sizeof(anonymous_owner));
			(void)copyout(device_path, alt_spec, strlen(device_path) + 1);
			(void)copyout(alt_payload, request.data, sizeof(alt_payload));
			int mode_zero_error = mount(p, &request, &probe_result);
			printf("meizu: data mount mode0 error=%d result=%d\n", mode_zero_error, probe_result);
			(void)copyout("md0", alt_spec, 4);
			int bare_error = mount(p, &request, &probe_result);
			printf("meizu: data mount devname=md0 error=%d result=%d\n", bare_error, probe_result);
		}
	}
	if (error) goto done;
	native_owner_data_stage = 3;
	error = vnode_lookup("/private/var", 0, &data_root, vfs_context_current());
	if (error) goto done;
	mount_t data_mount = vnode_mount(data_root);
	vnode_t device_node = vfs_devvp(data_mount);
	dev_t actual = device_node ? vnode_specrdev(device_node) : NODEV;
	if (device_node) vnode_put(device_node);
	native_owner_data_device = actual;
	native_owner_data_readonly = (vfs_flags(data_mount) & MNT_RDONLY) != 0;
	if (actual != device || (vfs_flags(data_mount) & MNT_RDONLY) ||
	    strcmp(vfs_statfs(data_mount)->f_fstypename, "apfs")) error = EINVAL;
	if (!error) native_owner_data_stage = 4;
	if (!error) native_gpu_addon_prepare(p, scratch);
	if (!error) native_staged_apps_prepare(p, scratch, data_mount);
done:
	if (data_root) vnode_put(data_root);
	native_owner_data_status = error;
	if (error) {
		printf("meizu: owner data fail stage=%d err=%d dev=%x base=%llx size=%llx readonly=%d point=%d node=%d path=%s\n",
		    native_owner_data_stage, error, (unsigned)device, (unsigned long long)base,
		    (unsigned long long)size, native_owner_data_readonly, meizu_data_point_probe,
		    meizu_data_node_probe, native_owner_data_device_path);
		printf("meizu: data node type=%d dev=%x point_type=%d seed_head=%x nxsb=%d read_err=%d read_head=%x\n",
		    meizu_data_node_type, meizu_data_node_dev, meizu_data_point_type, meizu_data_seed_head,
		    meizu_data_seed_nxsb, meizu_data_read_error, meizu_data_read_head);
	}
	return error;
}
#endif

static int
load_init_program_at_path(proc_t p, user_addr_t scratch_addr, const char* path, user_addr_t envp)
{
	int retval[2];
	int error;
	struct execve_args init_exec_args;
	user_addr_t argv0 = USER_ADDR_NULL, argv1 = USER_ADDR_NULL;

	/*
	 * Validate inputs and pre-conditions
	 */
	assert(p);
	assert(scratch_addr);
	assert(path);

	/*
	 * Copy out program name.
	 */
	size_t path_length = strlen(path) + 1;
	argv0 = scratch_addr;
	printf("meizu: init argv sink=%llx len=%llu path=%s\n",
	    (unsigned long long)argv0, (unsigned long long)path_length, path);
	error = copyout(path, argv0, path_length);
	if (error) {
		return error;
	}

	scratch_addr = USER_ADDR_ALIGN(scratch_addr + path_length, sizeof(user_addr_t));

	/*
	 * Put out first (and only) argument, similarly.
	 * Assumes everything fits in a page as allocated above.
	 */
	if (boothowto & RB_SINGLE) {
		const char *init_args = "-s";
		size_t init_args_length = strlen(init_args) + 1;

		argv1 = scratch_addr;
		error = copyout(init_args, argv1, init_args_length);
		if (error) {
			return error;
		}

		scratch_addr = USER_ADDR_ALIGN(scratch_addr + init_args_length, sizeof(user_addr_t));
	}

	if (proc_is64bit(p)) {
		user64_addr_t argv64bit[3] = {};

		argv64bit[0] = argv0;
		argv64bit[1] = argv1;
		argv64bit[2] = USER_ADDR_NULL;

		error = copyout(argv64bit, scratch_addr, sizeof(argv64bit));
		if (error) {
			return error;
		}
	} else {
		user32_addr_t argv32bit[3] = {};

		argv32bit[0] = (user32_addr_t)argv0;
		argv32bit[1] = (user32_addr_t)argv1;
		argv32bit[2] = USER_ADDR_NULL;

		error = copyout(argv32bit, scratch_addr, sizeof(argv32bit));
		if (error) {
			return error;
		}
	}

	/*
	 * Set up argument block for fake call to execve.
	 */
	printf("meizu: init exec fname=%llx argp=%llx envp=%llx is64=%d\n",
	    (unsigned long long)argv0, (unsigned long long)scratch_addr,
	    (unsigned long long)envp, proc_is64bit(p));
	init_exec_args.fname = argv0;
	init_exec_args.argp = scratch_addr;
	init_exec_args.envp = envp;

	/*
	 * So that init task is set with uid,gid 0 token
	 *
	 * The access to the cred is safe:
	 * the proc isn't running yet, it's stable.
	 */
	set_security_token(p, proc_ucred_unsafe(p));

	return execve(p, &init_exec_args, retval);
}

static const char * init_programs[] = {
#if DEBUG
	"/usr/appleinternal/sbin/launchd.debug",
#endif
#if DEVELOPMENT || DEBUG
	"/usr/appleinternal/sbin/launchd.development",
#endif
	"/sbin/launchd",
};

/*
 * load_init_program
 *
 * Description:	Load the "init" program; in most cases, this will be "launchd"
 *
 * Parameters:	p			Process to call execve() to create
 *					the "init" program
 *
 * Returns:	(void)
 *
 * Notes:	The process that is passed in is the first manufactured
 *		process on the system, and gets here via bsd_ast() firing
 *		for the first time.  This is done to ensure that bsd_init()
 *		has run to completion.
 *
 *		In DEBUG & DEVELOPMENT builds, the launchdsuffix boot-arg
 *		may be used to select a specific launchd executable. As with
 *		the kcsuffix boot-arg, setting launchdsuffix to "" or "release"
 *		will force /sbin/launchd to be selected.
 *
 *              Search order by build:
 *
 * DEBUG	DEVELOPMENT	RELEASE		PATH
 * ----------------------------------------------------------------------------------
 * 1		1		NA		/usr/appleinternal/sbin/launchd.$LAUNCHDSUFFIX
 * 2		NA		NA		/usr/appleinternal/sbin/launchd.debug
 * 3		2		NA		/usr/appleinternal/sbin/launchd.development
 * 4		3		1		/sbin/launchd
 */
void
load_init_program(proc_t p)
{
	extern volatile int meizu_load_init_count;
	meizu_load_init_count = meizu_load_init_count + 1;
	boot_progress_stage("load_init_program enter");
	uint32_t i;
	int error;
	vm_map_t map = current_map();
	mach_vm_offset_t scratch_addr = 0;
	mach_vm_size_t map_page_size = vm_map_page_size(map);
	mach_vm_size_t scratch_size = 16 * map_page_size;
	kern_return_t scratch_kr;

	/* The init scratch area carries the path, the argv block and the environment
	 * strings the owner chain stages for launchd. One page, or an allocation that
	 * lands at 0, leaves that first exec writing past the end of it. */
	{
		mach_vm_offset_t probe[3];
		int pi;
		probe[0] = (mach_vm_offset_t)0x00000000F0000000ULL;
		probe[1] = (mach_vm_offset_t)0x0000000100000000ULL;
		probe[2] = (mach_vm_offset_t)0x0000000180000000ULL;
		for (pi = 0; pi < 3; pi++) {
			mach_vm_offset_t at = probe[pi];
			kern_return_t pkr = mach_vm_allocate_kernel(map, &at, scratch_size, VM_FLAGS_FIXED, VM_KERN_MEMORY_NONE);
			printf("meizu: probe%d want=%llx got=%llx kr=%d\n", pi,
			    (unsigned long long)probe[pi], (unsigned long long)at, pkr);
			meizu_probe_kr[pi] = pkr;
			meizu_probe_err[13 + pi] = pkr;
			if (pkr == KERN_SUCCESS) {
				(void)mach_vm_deallocate(map, at, scratch_size);
			}
		}
	}
	scratch_addr = (mach_vm_offset_t)0x0000000100000000ULL;
	scratch_kr = mach_vm_allocate_kernel(map, &scratch_addr, scratch_size, VM_FLAGS_FIXED, VM_KERN_MEMORY_NONE);
	if (scratch_kr != KERN_SUCCESS) {
		scratch_addr = 0;
		scratch_kr = mach_vm_allocate_kernel(map, &scratch_addr, scratch_size, VM_FLAGS_ANYWHERE, VM_KERN_MEMORY_NONE);
	}
	printf("meizu: is64=%d\n", (int)proc_is64bit(p));
	printf("meizu: scratch2 kr=%d addr=%llx size=%llx page=%llx\n",
	    scratch_kr, (unsigned long long)scratch_addr, (unsigned long long)scratch_size,
	    (unsigned long long)map_page_size);
	{
		int te1, te2;
		meizu_touch_state = 1;
		printf("meizu: touch0 addr=%llx\n", (unsigned long long)scratch_addr);
		meizu_touch_state = 2;
		te1 = copyout("apfs", scratch_addr, 5);
		meizu_touch_state = 3;
		printf("meizu: touch1 err=%d\n", te1);
		te2 = copyout("apfs", scratch_addr, 5);
		meizu_touch_state = 4;
		printf("meizu: touch2 err=%d\n", te2);
	}
	{
		extern int openat_internal(vfs_context_t, user_addr_t, int, int, int, enum uio_seg, int *);
		int pi8, pfd = -1;
		if (copyout("/dev/null", (user_addr_t)(scratch_addr + 0x800), 10) == 0) {
			for (pi8 = 0; pi8 < 3; pi8++) {
				int pr = openat_internal(vfs_context_current(), (user_addr_t)(scratch_addr + 0x800),
				    O_RDWR, 0, AT_FDCWD, UIO_USERSPACE, &pfd);
				printf("meizu: preopen console err=%d fd=%d\n", pr, pfd);
				if (pr != 0) {
					break;
				}
			}
		}
	}
#if defined(XNU_MEIZU_WATCHDOG_POLICY) && XNU_MEIZU_WATCHDOG_POLICY
	/* Persist an unmistakable kernel/path identity in the first snapshot,
	 * before owner preparation or the policy can fail. No full bootarg dump. */
	{
		int warm_value = 0;
		int warm_found = PE_parse_boot_argn("nova-data-warm", &warm_value, sizeof(warm_value));
		extern void native_ufs_log_read_stats(void);
		printf("nova: policy-diag version=11 phase=pre-data invocation=%d warm_found=%d warm_value=%d\n",
		    meizu_load_init_count, warm_found, warm_value);
		native_ufs_log_read_stats();
	}
#endif
	meizu_autolog_start();
	user_addr_t init_envp = USER_ADDR_NULL;
#if defined(XNU_OWNER_CACHE_MOUNT) && XNU_OWNER_CACHE_MOUNT
	boot_progress_stage("SystemOS mount enter");
	extern uint64_t nova_boot_phase_begin(const char *);
	extern void nova_boot_phase_end(const char *, uint64_t, int);
	uint64_t systemos_phase = nova_boot_phase_begin("systemos_prepare");
	int cache_error = native_owner_cache_prepare(p, (user_addr_t)scratch_addr, &init_envp);
	nova_boot_phase_end("systemos_prepare", systemos_phase, cache_error);
	boot_progress_stage("SystemOS mount returned");
	if (cache_error) {
		panic("sysos fail %d st=%d au=%d sel=%x mnt=%x",
		    cache_error, native_owner_cache_stage, native_owner_cache_authorized,
		    (unsigned)native_owner_cache_selected_device,
		    (unsigned)native_owner_cache_mounted_device);
	}
#endif

#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT
	boot_progress_stage("Data mount enter");
	extern uint64_t nova_boot_phase_begin(const char *);
	extern void nova_boot_phase_end(const char *, uint64_t, int);
	uint64_t data_phase = nova_boot_phase_begin("data_prepare");
	int data_error = native_owner_data_prepare(p, (user_addr_t)scratch_addr);
	nova_boot_phase_end("data_prepare", data_phase, data_error);
#if defined(XNU_MEIZU_AKS_NOTIFY) && XNU_MEIZU_AKS_NOTIFY
	/* PID 1, after the Data mount and before launchd starts SpringBoard. */
	extern void meizu_keystore_notification_publish(int);
	meizu_keystore_notification_publish(data_error);
#endif
	boot_progress_stage("Data mount returned");
	if (data_error) {
		printf("meizu: Data mount failed %d, continuing without /private/var\n", data_error);
	}
#if defined(XNU_MEIZU_WATCHDOG_POLICY) && XNU_MEIZU_WATCHDOG_POLICY
	if (!data_error) {
		extern int meizu_boot_watchdog_policy(void *, uint64_t, int);
		int policy_error = meizu_boot_watchdog_policy(p, (uint64_t)scratch_addr, data_error);
		if (policy_error) {
			extern const char *meizu_watchdog_policy_diagnostic(int *, int *, int *, int *);
			extern void native_ufs_log_read_stats(void);
			int warm_found = -1, warm_value = -1, branch = -1, op = -1;
			const char *stage = meizu_watchdog_policy_diagnostic(&warm_found, &warm_value, &branch, &op);
			printf("nova: policy-diag version=11 phase=failed invocation=%d warm_found=%d warm_value=%d branch=%d op=%d stage=%s errno=%d save=attempting\n",
			    meizu_load_init_count, warm_found, warm_value, branch, op, stage, policy_error);
			native_ufs_log_read_stats();
			int save = nova_policy_failure_save_once();
			/* The save result is known only after the snapshot, so the panic
			 * reports it without claiming that it is in that saved snapshot. */
			panic("nova-r11 policy fail e=%d b=%d op=%d save=%d", policy_error, branch, op, save);
		}
	}
#endif
#if defined(XNU_BOOT_PROGRESS) && XNU_BOOT_PROGRESS
	extern int sheng_boot_watchdog_policy(void *, uint64_t);
	boot_progress_stage("temporary watchdog launch policy enter");
	int policy_error=sheng_boot_watchdog_policy(p, (uint64_t)scratch_addr);
	if (policy_error) panic("sheng watchdog launch policy failed: %d", policy_error);
	boot_progress_stage("temporary watchdog launch policy ready");
#endif
#endif
#if CONFIG_MEMORYSTATUS
	(void) memorystatus_init_at_boot_snapshot();
#endif /* CONFIG_MEMORYSTATUS */

#if (DEBUG || DEVELOPMENT) && defined(XNU_NATIVE_AIR_INIT) && XNU_NATIVE_AIR_INIT
	int offline_air = 0;
	if (PE_parse_boot_argn("offline-air-init", &offline_air, sizeof(offline_air)) && offline_air) {
		if (offline_air != 1) panic("offline AIR init invalid selector");
		error = load_init_program_at_path(p, (user_addr_t)scratch_addr,
		    "/private/var/tmp/air-native", init_envp);
		if (error) panic("offline signed AIR instrument exec failed: %d", error);
		return;
	}
#endif

#if DEBUG || DEVELOPMENT
	/* Check for boot-arg suffix first */
	char launchd_suffix[64];
	if (PE_parse_boot_argn("launchdsuffix", launchd_suffix, sizeof(launchd_suffix))) {
		char launchd_path[128];
		boolean_t is_release_suffix = ((launchd_suffix[0] == 0) ||
		    (strcmp(launchd_suffix, "release") == 0));

		if (is_release_suffix) {
			printf("load_init_program: attempting to load /sbin/launchd\n");
			error = load_init_program_at_path(p, (user_addr_t)scratch_addr, "/sbin/launchd", init_envp);
			if (!error) {
				return;
			}

			panic("Process 1 exec of launchd.release failed, errno %d", error);
		} else {
			strlcpy(launchd_path, "/usr/appleinternal/sbin/launchd.", sizeof(launchd_path));
			strlcat(launchd_path, launchd_suffix, sizeof(launchd_path));

			printf("load_init_program: attempting to load %s\n", launchd_path);
			error = load_init_program_at_path(p, (user_addr_t)scratch_addr, launchd_path, init_envp);
			if (!error) {
				return;
			} else if (error != ENOENT) {
				printf("load_init_program: failed loading %s: errno %d\n", launchd_path, error);
			}
		}
	}
#endif

	error = ENOENT;
	for (i = 0; i < sizeof(init_programs) / sizeof(init_programs[0]); i++) {
		printf("load_init_program: attempting to load %s\n", init_programs[i]);
		error = load_init_program_at_path(p, (user_addr_t)scratch_addr, init_programs[i], init_envp);
		if (!error) {
			return;
		} else if (error != ENOENT) {
			printf("load_init_program: failed loading %s: errno %d\n", init_programs[i], error);
		}
	}

	panic("Process 1 exec of %s failed, errno %d", ((i == 0) ? "<null>" : init_programs[i - 1]), error);
}

/*
 * load_return_to_errno
 *
 * Description:	Convert a load_return_t (Mach error) to an errno (BSD error)
 *
 * Parameters:	lrtn			Mach error number
 *
 * Returns:	(int)			BSD error number
 *		0			Success
 *		EBADARCH		Bad architecture
 *		EBADMACHO		Bad Mach object file
 *		ESHLIBVERS		Bad shared library version
 *		ENOMEM			Out of memory/resource shortage
 *		EACCES			Access denied
 *		ENOENT			Entry not found (usually "file does
 *					does not exist")
 *		EIO			An I/O error occurred
 *		EBADEXEC		The executable is corrupt/unknown
 */
static int
load_return_to_errno(load_return_t lrtn)
{
	switch (lrtn) {
	case LOAD_SUCCESS:
		return 0;
	case LOAD_BADARCH:
		return EBADARCH;
	case LOAD_BADMACHO:
	case LOAD_BADMACHO_UPX:
		return EBADMACHO;
	case LOAD_SHLIB:
		return ESHLIBVERS;
	case LOAD_NOSPACE:
	case LOAD_RESOURCE:
		return ENOMEM;
	case LOAD_PROTECT:
		return EACCES;
	case LOAD_ENOENT:
		return ENOENT;
	case LOAD_IOERROR:
		return EIO;
	case LOAD_DECRYPTFAIL:
		return EAUTH;
	case LOAD_FAILURE:
	default:
		return EBADEXEC;
	}
}

#include <mach/mach_types.h>
#include <mach/vm_prot.h>
#include <mach/semaphore.h>
#include <mach/sync_policy.h>
#include <kern/clock.h>
#include <mach/kern_return.h>

/*
 * execargs_alloc
 *
 * Description:	Allocate the block of memory used by the execve arguments.
 *		At the same time, we allocate a page so that we can read in
 *		the first page of the image.
 *
 * Parameters:	struct image_params *	the image parameter block
 *
 * Returns:	0			Success
 *		EINVAL			Invalid argument
 *		EACCES			Permission denied
 *		EINTR			Interrupted function
 *		ENOMEM			Not enough space
 *
 * Notes:	This is a temporary allocation into the kernel address space
 *		to enable us to copy arguments in from user space.  This is
 *		necessitated by not mapping the process calling execve() into
 *		the kernel address space during the execve() system call.
 *
 *		We assemble the argument and environment, etc., into this
 *		region before copying it as a single block into the child
 *		process address space (at the top or bottom of the stack,
 *		depending on which way the stack grows; see the function
 *		exec_copyout_strings() for details).
 *
 *		This ends up with a second (possibly unnecessary) copy compared
 *		with assembing the data directly into the child address space,
 *		instead, but since we cannot be guaranteed that the parent has
 *		not modified its environment, we can't really know that it's
 *		really a block there as well.
 */


static int execargs_waiters = 0;
static LCK_MTX_DECLARE_ATTR(execargs_cache_lock, &proc_lck_grp, &proc_lck_attr);

static void
execargs_lock_lock(void)
{
	lck_mtx_lock_spin(&execargs_cache_lock);
}

static void
execargs_lock_unlock(void)
{
	lck_mtx_unlock(&execargs_cache_lock);
}

static wait_result_t
execargs_lock_sleep(void)
{
	return lck_mtx_sleep(&execargs_cache_lock, LCK_SLEEP_DEFAULT, &execargs_free_count, THREAD_INTERRUPTIBLE);
}

static kern_return_t
execargs_purgeable_allocate(char **execarg_address)
{
	mach_vm_offset_t addr = 0;
	kern_return_t kr = mach_vm_allocate_kernel(bsd_pageable_map, &addr,
	    BSD_PAGEABLE_SIZE_PER_EXEC, VM_FLAGS_ANYWHERE | VM_FLAGS_PURGABLE,
	    VM_KERN_MEMORY_NONE);
	*execarg_address = (char *)addr;
	assert(kr == KERN_SUCCESS);
	return kr;
}

static kern_return_t
execargs_purgeable_reference(void *execarg_address)
{
	int state = VM_PURGABLE_NONVOLATILE;
	kern_return_t kr = vm_purgable_control(bsd_pageable_map, (vm_offset_t) execarg_address, VM_PURGABLE_SET_STATE, &state);

	assert(kr == KERN_SUCCESS);
	return kr;
}

static kern_return_t
execargs_purgeable_volatilize(void *execarg_address)
{
	int state = VM_PURGABLE_VOLATILE | VM_PURGABLE_ORDERING_OBSOLETE;
	kern_return_t kr;
	kr = vm_purgable_control(bsd_pageable_map, (vm_offset_t) execarg_address, VM_PURGABLE_SET_STATE, &state);

	assert(kr == KERN_SUCCESS);

	return kr;
}

static void
execargs_wakeup_waiters(void)
{
	thread_wakeup(&execargs_free_count);
}

static int
execargs_alloc(struct image_params *imgp)
{
	kern_return_t kret;
	wait_result_t res;
	int i, cache_index = -1;

	execargs_lock_lock();

	while (execargs_free_count == 0) {
		execargs_waiters++;
		res = execargs_lock_sleep();
		execargs_waiters--;
		if (res != THREAD_AWAKENED) {
			execargs_lock_unlock();
			return EINTR;
		}
	}

	execargs_free_count--;

	for (i = 0; i < execargs_cache_size; i++) {
		vm_offset_t element = execargs_cache[i];
		if (element) {
			cache_index = i;
			imgp->ip_strings = (char *)(execargs_cache[i]);
			execargs_cache[i] = 0;
			break;
		}
	}

	assert(execargs_free_count >= 0);

	execargs_lock_unlock();

	if (cache_index == -1) {
		kret = execargs_purgeable_allocate(&imgp->ip_strings);
	} else {
		kret = execargs_purgeable_reference(imgp->ip_strings);
	}

	assert(kret == KERN_SUCCESS);
	if (kret != KERN_SUCCESS) {
		return ENOMEM;
	}

	/* last page used to read in file headers */
	imgp->ip_vdata = imgp->ip_strings + (NCARGS + PAGE_SIZE);
	imgp->ip_strendp = imgp->ip_strings;
	imgp->ip_argspace = NCARGS;
	imgp->ip_strspace = (NCARGS + PAGE_SIZE);

	return 0;
}

/*
 * execargs_free
 *
 * Description:	Free the block of memory used by the execve arguments and the
 *		first page of the executable by a previous call to the function
 *		execargs_alloc().
 *
 * Parameters:	struct image_params *	the image parameter block
 *
 * Returns:	0			Success
 *		EINVAL			Invalid argument
 *		EINTR			Oeration interrupted
 */
static int
execargs_free(struct image_params *imgp)
{
	kern_return_t kret;
	int i;
	boolean_t needs_wakeup = FALSE;

	kret = execargs_purgeable_volatilize(imgp->ip_strings);

	execargs_lock_lock();
	execargs_free_count++;

	for (i = 0; i < execargs_cache_size; i++) {
		vm_offset_t element = execargs_cache[i];
		if (element == 0) {
			execargs_cache[i] = (vm_offset_t) imgp->ip_strings;
			imgp->ip_strings = NULL;
			break;
		}
	}

	assert(imgp->ip_strings == NULL);

	if (execargs_waiters > 0) {
		needs_wakeup = TRUE;
	}

	execargs_lock_unlock();

	if (needs_wakeup == TRUE) {
		execargs_wakeup_waiters();
	}

	return kret == KERN_SUCCESS ? 0 : EINVAL;
}

void
uthread_set_exec_data(struct uthread *uth, struct image_params *imgp)
{
	uth->uu_save.uus_exec_data.imgp = imgp;
}

size_t
thread_get_current_exec_path(char *path, size_t size)
{
	struct uthread *uth = current_uthread();
	struct image_params *imgp = uth->uu_save.uus_exec_data.imgp;
	size_t string_size = 0;
	char *exec_path;

	if (path == NULL || imgp == NULL || imgp->ip_strings == NULL) {
		return 0;
	}

	exec_path = imgp->ip_strings + strlen(EXECUTABLE_KEY);
	string_size = imgp->ip_strendp - exec_path;
	string_size = MIN(MAXPATHLEN, string_size);
	string_size = MIN(size, string_size);

	string_size = strlcpy(path, exec_path, string_size);
	return string_size;
}
static void
exec_resettextvp(proc_t p, struct image_params *imgp)
{
	vnode_t vp;
	off_t offset;
	vnode_t tvp  = p->p_textvp;
	int ret;

	vp = imgp->ip_vp;
	offset = imgp->ip_arch_offset;

	if (vp == NULLVP) {
		panic("exec_resettextvp: expected valid vp");
	}

	ret = vnode_ref(vp);
	proc_lock(p);
	if (ret == 0) {
		p->p_textvp = vp;
		p->p_textoff = offset;
	} else {
		p->p_textvp = NULLVP;   /* this is paranoia */
		p->p_textoff = 0;
	}
	proc_unlock(p);

	if (tvp != NULLVP) {
		if (vnode_getwithref(tvp) == 0) {
			vnode_rele(tvp);
			vnode_put(tvp);
		}
	}
}

// Includes the 0-byte (therefore "SIZE" instead of "LEN").
static const size_t CS_CDHASH_STRING_SIZE = CS_CDHASH_LEN * 2 + 1;

static void
cdhash_to_string(char str[CS_CDHASH_STRING_SIZE], uint8_t const * const cdhash)
{
	static char const nibble[] = "0123456789abcdef";

	/* Apparently still the safest way to get a hex representation
	 * of binary data.
	 * xnu's printf routines have %*D/%20D in theory, but "not really", see:
	 * <rdar://problem/33328859> confusion around %*D/%nD in printf
	 */
	for (int i = 0; i < CS_CDHASH_LEN; ++i) {
		str[i * 2] = nibble[(cdhash[i] & 0xf0) >> 4];
		str[i * 2 + 1] = nibble[cdhash[i] & 0x0f];
	}
	str[CS_CDHASH_STRING_SIZE - 1] = 0;
}

/*
 * __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__
 *
 * Description: Waits for the userspace daemon to respond to the request
 *              we made. Function declared non inline to be visible in
 *		stackshots and spindumps as well as debugging.
 */
__attribute__((noinline)) int
__EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(mach_port_t task_access_port, int32_t new_pid)
{
	return find_code_signature(task_access_port, new_pid);
}

/*
 * Update signature dependent process state, called by
 * process_signature.
 */
static int
proc_process_signature(proc_t p, os_reason_t *signature_failure_reason)
{
	int error = 0;
	char const *error_msg = NULL;

	kern_return_t kr = machine_task_process_signature(proc_get_task_raw(p), proc_platform(p), proc_sdk(p), &error_msg);

	if (kr != KERN_SUCCESS) {
		error = EINVAL;

		if (error_msg != NULL) {
			uint32_t error_msg_len = (uint32_t)strlen(error_msg) + 1;
			mach_vm_address_t data_addr = 0;
			int reason_error = 0;
			int kcdata_error = 0;

			os_reason_t reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_SECURITY_POLICY);
			reason->osr_flags = OS_REASON_FLAG_GENERATE_CRASH_REPORT | OS_REASON_FLAG_CONSISTENT_FAILURE;

			if ((reason_error = os_reason_alloc_buffer_noblock(reason,
			    kcdata_estimate_required_buffer_size(1, error_msg_len))) == 0 &&
			    (kcdata_error = kcdata_get_memory_addr(&reason->osr_kcd_descriptor,
			    EXIT_REASON_USER_DESC, error_msg_len,
			    &data_addr)) == KERN_SUCCESS) {
				kern_return_t mc_error = kcdata_memcpy(&reason->osr_kcd_descriptor, (mach_vm_address_t)data_addr,
				    error_msg, error_msg_len);

				if (mc_error != KERN_SUCCESS) {
					printf("process_signature: failed to copy reason string (kcdata_memcpy error: %d)\n",
					    mc_error);
				}
			} else {
				printf("failed to allocate space for reason string (os_reason_alloc_buffer error: %d, kcdata error: %d, length: %u)\n",
				    reason_error, kcdata_error, error_msg_len);
			}

			assert(*signature_failure_reason == NULL); // shouldn't have gotten so far
			*signature_failure_reason = reason;
		}
	}
	return error;
}

static int
process_signature(proc_t p, struct image_params *imgp)
{
	mach_port_t port = IPC_PORT_NULL;
	kern_return_t kr = KERN_FAILURE;
	int error = EACCES;
	boolean_t unexpected_failure = FALSE;
	struct cs_blob *csb;
	boolean_t require_success = FALSE;
	int spawn = (imgp->ip_flags & IMGPF_SPAWN);
	const int vfexec = 0;
	os_reason_t signature_failure_reason = OS_REASON_NULL;

	/*
	 * Override inherited code signing flags with the
	 * ones for the process that is being successfully
	 * loaded
	 */
	proc_lock(p);
	proc_csflags_update(p, imgp->ip_csflags);
	proc_unlock(p);

	/* Set the switch_protect flag on the map */
	if (proc_getcsflags(p) & (CS_HARD | CS_KILL)) {
		vm_map_switch_protect(get_task_map(proc_task(p)), TRUE);
	}
	/* set the cs_enforced flags in the map */
	if (proc_getcsflags(p) & CS_ENFORCEMENT) {
		vm_map_cs_enforcement_set(get_task_map(proc_task(p)), TRUE);
	} else {
		vm_map_cs_enforcement_set(get_task_map(proc_task(p)), FALSE);
	}

	/*
	 * image activation may be failed due to policy
	 * which is unexpected but security framework does not
	 * approve of exec, kill and return immediately.
	 */
	if (imgp->ip_mac_return != 0) {
		boot_progress_process_stage(proc_getpid(p),p->p_name,"exec MAC policy REFUSED");
		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_SECURITY_POLICY, 0, 0);
		signature_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_SECURITY_POLICY);
		error = imgp->ip_mac_return;
		unexpected_failure = TRUE;
		goto done;
	}

	if (imgp->ip_cs_error != OS_REASON_NULL) {
		signature_failure_reason = imgp->ip_cs_error;
		imgp->ip_cs_error = OS_REASON_NULL;
		error = EACCES;
		goto done;
	}

	/* call the launch constraints hook */
	os_reason_t launch_constraint_reason;
	if ((error = mac_proc_check_launch_constraints(p, imgp, &launch_constraint_reason)) != 0) {
		signature_failure_reason = launch_constraint_reason;
		goto done;
	}

#if XNU_TARGET_OS_OSX
	/* Check for platform passed in spawn attr if iOS binary is being spawned */
	if (proc_platform(p) == PLATFORM_IOS) {
		struct _posix_spawnattr *psa = (struct _posix_spawnattr *) imgp->ip_px_sa;
		if (psa == NULL || psa->psa_platform == 0) {
			boolean_t no_sandbox_entitled = FALSE;
#if DEBUG || DEVELOPMENT
			/*
			 * Allow iOS binaries to spawn on internal systems
			 * if no-sandbox entitlement is present of unentitled_ios_sim_launch
			 * boot-arg set to true
			 */
			if (unentitled_ios_sim_launch) {
				no_sandbox_entitled = TRUE;
			} else {
				no_sandbox_entitled = IOVnodeHasEntitlement(imgp->ip_vp,
				    (int64_t)imgp->ip_arch_offset, "com.apple.private.security.no-sandbox");
			}
#endif /* DEBUG || DEVELOPMENT */
			if (!no_sandbox_entitled) {
				signature_failure_reason = os_reason_create(OS_REASON_EXEC,
				    EXEC_EXIT_REASON_WRONG_PLATFORM);
				error = EACCES;
				goto done;
			}
			printf("Allowing spawn of iOS binary %s since it has "
			    "com.apple.private.security.no-sandbox entitlement or unentitled_ios_sim_launch "
			    "boot-arg set to true\n", p->p_name);
		} else if (psa->psa_platform != PLATFORM_IOS) {
			/* Simulator binary spawned with wrong platform */
			signature_failure_reason = os_reason_create(OS_REASON_EXEC,
			    EXEC_EXIT_REASON_WRONG_PLATFORM);
			error = EACCES;
			goto done;
		} else {
			printf("Allowing spawn of iOS binary %s since correct platform was passed in spawn\n",
			    p->p_name);
		}
	}
#endif /* XNU_TARGET_OS_OSX */

	/* If the code signature came through the image activation path, we skip the
	 * taskgated / externally attached path. */
	if (imgp->ip_csflags & CS_SIGNED) {
		error = 0;
		goto done;
	}

	/* The rest of the code is for signatures that either already have been externally
	 * attached (likely, but not necessarily by a previous run through the taskgated
	 * path), or that will now be attached by taskgated. */

	kr = task_get_task_access_port(proc_task(p), &port);
	if (KERN_SUCCESS != kr || !IPC_PORT_VALID(port)) {
		error = 0;
		if (require_success) {
			KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
			    proc_getpid(p), OS_REASON_CODESIGNING, CODESIGNING_EXIT_REASON_TASK_ACCESS_PORT, 0, 0);
			signature_failure_reason = os_reason_create(OS_REASON_CODESIGNING, CODESIGNING_EXIT_REASON_TASK_ACCESS_PORT);
			error = EACCES;
		}
		goto done;
	}

	/*
	 * taskgated returns KERN_SUCCESS if it has completed its work
	 * and the exec should continue, KERN_FAILURE if the exec should
	 * fail, or it may error out with different error code in an
	 * event of mig failure (e.g. process was signalled during the
	 * rpc call, taskgated died, mig server died etc.).
	 */

	kr = __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(port, proc_getpid(p));
	switch (kr) {
	case KERN_SUCCESS:
		error = 0;
		break;
	case KERN_FAILURE:
		error = EACCES;

		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_CODESIGNING, CODESIGNING_EXIT_REASON_TASKGATED_INVALID_SIG, 0, 0);
		signature_failure_reason = os_reason_create(OS_REASON_CODESIGNING, CODESIGNING_EXIT_REASON_TASKGATED_INVALID_SIG);
		goto done;
	default:
		error = EACCES;

		KERNEL_DEBUG_CONSTANT(BSDDBG_CODE(DBG_BSD_PROC, BSD_PROC_EXITREASON_CREATE) | DBG_FUNC_NONE,
		    proc_getpid(p), OS_REASON_EXEC, EXEC_EXIT_REASON_TASKGATED_OTHER, 0, 0);
		signature_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_TASKGATED_OTHER);
		unexpected_failure = TRUE;
		goto done;
	}

	/* Only do this if exec_resettextvp() did not fail */
	if (p->p_textvp != NULLVP) {
		csb = ubc_cs_blob_get(p->p_textvp, -1, -1, p->p_textoff);

		if (csb != NULL) {
			/* As the enforcement we can do here is very limited, we only allow things that
			 * are the only reason why this code path still exists:
			 * Adhoc signed non-platform binaries without special cs_flags and without any
			 * entitlements (unrestricted ones still pass AMFI). */
			if (
				/* Revalidate the blob if necessary through bumped generation count. */
				(ubc_cs_generation_check(p->p_textvp) == 0 ||
				ubc_cs_blob_revalidate(p->p_textvp, csb, imgp, 0, proc_platform(p)) == 0) &&
				/* Only CS_ADHOC, no CS_KILL, CS_HARD etc. */
				(csb->csb_flags & CS_ALLOWED_MACHO) == CS_ADHOC &&
				/* If it has a CMS blob, it's not adhoc. The CS_ADHOC flag can lie. */
				csblob_find_blob_bytes((const uint8_t *)csb->csb_mem_kaddr, csb->csb_mem_size,
				CSSLOT_SIGNATURESLOT,
				CSMAGIC_BLOBWRAPPER) == NULL &&
				/* It could still be in a trust cache (unlikely with CS_ADHOC), or a magic path. */
				csb->csb_platform_binary == 0 &&
				/* No entitlements, not even unrestricted ones. */
				csb->csb_entitlements_blob == NULL &&
				csb->csb_der_entitlements_blob == NULL) {
				proc_lock(p);
				proc_csflags_set(p, CS_SIGNED | CS_VALID);
				proc_unlock(p);
			} else {
				uint8_t cdhash[CS_CDHASH_LEN];
				char cdhash_string[CS_CDHASH_STRING_SIZE];
				proc_getcdhash(p, cdhash);
				cdhash_to_string(cdhash_string, cdhash);
				printf("ignoring detached code signature on '%s' with cdhash '%s' "
				    "because it is invalid, or not a simple adhoc signature.\n",
				    p->p_name, cdhash_string);
			}
		}
	}

done:
	if (0 == error) {
		/*
		 * Update the new process's signature-dependent process state.
		 * state.
		 */

		boot_progress_process_stage(proc_getpid(p),p->p_name,"exec process signature enter");
		error = proc_process_signature(p, &signature_failure_reason);
		boot_progress_process_stage(proc_getpid(p),p->p_name,error ? "exec process signature REFUSED" : "exec process signature accepted");
	}

	if (0 == error) {
		/*
		 * Update the new main thread's signature-dependent thread
		 * state. This was also called when the thread was created,
		 * but for the main thread the signature was not yet attached
		 * at that time.
		 */
		kr = thread_process_signature(imgp->ip_new_thread, proc_get_task_raw(p));

		if (kr != KERN_SUCCESS) {
			error = EINVAL;
			signature_failure_reason = os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_MACHINE_THREAD);
		}
	}

	if (0 == error) {
		/* The process's code signature related properties are
		 * fully set up, so this is an opportune moment to log
		 * platform binary execution, if desired. */
		if (platform_exec_logging != 0 && csproc_get_platform_binary(p)) {
			uint8_t cdhash[CS_CDHASH_LEN];
			char cdhash_string[CS_CDHASH_STRING_SIZE];
			proc_getcdhash(p, cdhash);
			cdhash_to_string(cdhash_string, cdhash);

			os_log(peLog, "CS Platform Exec Logging: Executing platform signed binary "
			    "'%s' with cdhash %s\n", p->p_name, cdhash_string);
		}
	} else {
		if (!unexpected_failure) {
			proc_csflags_set(p, CS_KILLED);
		}
		/* make very sure execution fails */
		if (vfexec || spawn) {
			assert(signature_failure_reason != OS_REASON_NULL);
			psignal_vfork_with_reason(p, proc_task(p), imgp->ip_new_thread,
			    SIGKILL, signature_failure_reason);
			signature_failure_reason = OS_REASON_NULL;
			error = 0;
		} else {
			assert(signature_failure_reason != OS_REASON_NULL);
			psignal_with_reason(p, SIGKILL, signature_failure_reason);
			signature_failure_reason = OS_REASON_NULL;
		}
	}

	if (port != IPC_PORT_NULL) {
		ipc_port_release_send(port);
	}

	/* If we hit this, we likely would have leaked an exit reason */
	assert(signature_failure_reason == OS_REASON_NULL);
	return error;
}

/*
 * Typically as soon as we start executing this process, the
 * first instruction will trigger a VM fault to bring the text
 * pages (as executable) into the address space, followed soon
 * thereafter by dyld data structures (for dynamic executable).
 * To optimize this, as well as improve support for hardware
 * debuggers that can only access resident pages present
 * in the process' page tables, we prefault some pages if
 * possible. Errors are non-fatal.
 */
#ifndef PREVENT_CALLER_STACK_USE
#define PREVENT_CALLER_STACK_USE __attribute__((noinline))
#endif
static void PREVENT_CALLER_STACK_USE
exec_prefault_data(proc_t p __unused, struct image_params *imgp, load_result_t *load_result)
{
	int ret;
	size_t expected_all_image_infos_size;
	kern_return_t kr;

	/*
	 * Prefault executable or dyld entry point.
	 */
	if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
		DEBUG4K_LOAD("entry_point 0x%llx\n", (uint64_t)load_result->entry_point);
	}
	kr = vm_fault(current_map(),
	    vm_map_trunc_page(load_result->entry_point,
	    vm_map_page_mask(current_map())),
	    VM_PROT_READ | VM_PROT_EXECUTE,
	    FALSE, VM_KERN_MEMORY_NONE,
	    THREAD_UNINT, NULL, 0);
	if (kr != KERN_SUCCESS) {
		DEBUG4K_ERROR("map %p va 0x%llx -> 0x%x\n", current_map(), (uint64_t)vm_map_trunc_page(load_result->entry_point, vm_map_page_mask(current_map())), kr);
	}

	if (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) {
		expected_all_image_infos_size = sizeof(struct user64_dyld_all_image_infos);
	} else {
		expected_all_image_infos_size = sizeof(struct user32_dyld_all_image_infos);
	}

	/* Decode dyld anchor structure from <mach-o/dyld_images.h> */
	if (load_result->dynlinker &&
	    load_result->all_image_info_addr &&
	    load_result->all_image_info_size >= expected_all_image_infos_size) {
		union {
			struct user64_dyld_all_image_infos      infos64;
			struct user32_dyld_all_image_infos      infos32;
		} all_image_infos;

		/*
		 * Pre-fault to avoid copyin() going through the trap handler
		 * and recovery path.
		 */
		if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
			DEBUG4K_LOAD("all_image_info_addr 0x%llx\n", load_result->all_image_info_addr);
		}
		kr = vm_fault(current_map(),
		    vm_map_trunc_page(load_result->all_image_info_addr,
		    vm_map_page_mask(current_map())),
		    VM_PROT_READ | VM_PROT_WRITE,
		    FALSE, VM_KERN_MEMORY_NONE,
		    THREAD_UNINT, NULL, 0);
		if (kr != KERN_SUCCESS) {
//			printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(load_result->all_image_info_addr, vm_map_page_mask(current_map())), kr);
		}
		if ((load_result->all_image_info_addr & PAGE_MASK) + expected_all_image_infos_size > PAGE_SIZE) {
			/* all_image_infos straddles a page */
			kr = vm_fault(current_map(),
			    vm_map_trunc_page(load_result->all_image_info_addr + expected_all_image_infos_size - 1,
			    vm_map_page_mask(current_map())),
			    VM_PROT_READ | VM_PROT_WRITE,
			    FALSE, VM_KERN_MEMORY_NONE,
			    THREAD_UNINT, NULL, 0);
			if (kr != KERN_SUCCESS) {
//				printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(load_result->all_image_info_addr + expected_all_image_infos_size -1, vm_map_page_mask(current_map())), kr);
			}
		}

		if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
			DEBUG4K_LOAD("copyin(0x%llx, 0x%lx)\n", load_result->all_image_info_addr, expected_all_image_infos_size);
		}
		ret = copyin((user_addr_t)load_result->all_image_info_addr,
		    &all_image_infos,
		    expected_all_image_infos_size);
		if (ret == 0 && all_image_infos.infos32.version >= DYLD_ALL_IMAGE_INFOS_ADDRESS_MINIMUM_VERSION) {
			user_addr_t notification_address;
			user_addr_t dyld_image_address;
			user_addr_t dyld_version_address;
			user_addr_t dyld_all_image_infos_address;
			user_addr_t dyld_slide_amount;

			if (imgp->ip_flags & IMGPF_IS_64BIT_ADDR) {
				notification_address = (user_addr_t)all_image_infos.infos64.notification;
				dyld_image_address = (user_addr_t)all_image_infos.infos64.dyldImageLoadAddress;
				dyld_version_address = (user_addr_t)all_image_infos.infos64.dyldVersion;
				dyld_all_image_infos_address = (user_addr_t)all_image_infos.infos64.dyldAllImageInfosAddress;
			} else {
				notification_address = all_image_infos.infos32.notification;
				dyld_image_address = all_image_infos.infos32.dyldImageLoadAddress;
				dyld_version_address = all_image_infos.infos32.dyldVersion;
				dyld_all_image_infos_address = all_image_infos.infos32.dyldAllImageInfosAddress;
			}

			/*
			 * dyld statically sets up the all_image_infos in its Mach-O
			 * binary at static link time, with pointers relative to its default
			 * load address. Since ASLR might slide dyld before its first
			 * instruction is executed, "dyld_slide_amount" tells us how far
			 * dyld was loaded compared to its default expected load address.
			 * All other pointers into dyld's image should be adjusted by this
			 * amount. At some point later, dyld will fix up pointers to take
			 * into account the slide, at which point the all_image_infos_address
			 * field in the structure will match the runtime load address, and
			 * "dyld_slide_amount" will be 0, if we were to consult it again.
			 */

			dyld_slide_amount = (user_addr_t)load_result->all_image_info_addr - dyld_all_image_infos_address;

#if 0
			kprintf("exec_prefault: 0x%016llx 0x%08x 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
			    (uint64_t)load_result->all_image_info_addr,
			    all_image_infos.infos32.version,
			    (uint64_t)notification_address,
			    (uint64_t)dyld_image_address,
			    (uint64_t)dyld_version_address,
			    (uint64_t)dyld_all_image_infos_address);
#endif

			if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
				DEBUG4K_LOAD("notification_address 0x%llx dyld_slide_amount 0x%llx\n", (uint64_t)notification_address, (uint64_t)dyld_slide_amount);
			}
			kr = vm_fault(current_map(),
			    vm_map_trunc_page(notification_address + dyld_slide_amount,
			    vm_map_page_mask(current_map())),
			    VM_PROT_READ | VM_PROT_EXECUTE,
			    FALSE, VM_KERN_MEMORY_NONE,
			    THREAD_UNINT, NULL, 0);
			if (kr != KERN_SUCCESS) {
//				printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(notification_address + dyld_slide_amount, vm_map_page_mask(current_map())), kr);
			}
			if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
				DEBUG4K_LOAD("dyld_image_address 0x%llx dyld_slide_amount 0x%llx\n", (uint64_t)dyld_image_address, (uint64_t)dyld_slide_amount);
			}
			kr = vm_fault(current_map(),
			    vm_map_trunc_page(dyld_image_address + dyld_slide_amount,
			    vm_map_page_mask(current_map())),
			    VM_PROT_READ | VM_PROT_EXECUTE,
			    FALSE, VM_KERN_MEMORY_NONE,
			    THREAD_UNINT, NULL, 0);
			if (kr != KERN_SUCCESS) {
//				printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(dyld_image_address + dyld_slide_amount, vm_map_page_mask(current_map())), kr);
			}
			if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
				DEBUG4K_LOAD("dyld_version_address 0x%llx dyld_slide_amount 0x%llx\n", (uint64_t)dyld_version_address, (uint64_t)dyld_slide_amount);
			}
			kr = vm_fault(current_map(),
			    vm_map_trunc_page(dyld_version_address + dyld_slide_amount,
			    vm_map_page_mask(current_map())),
			    VM_PROT_READ,
			    FALSE, VM_KERN_MEMORY_NONE,
			    THREAD_UNINT, NULL, 0);
			if (kr != KERN_SUCCESS) {
//				printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(dyld_version_address + dyld_slide_amount, vm_map_page_mask(current_map())), kr);
			}
			if (vm_map_page_shift(current_map()) < (int)PAGE_SHIFT) {
				DEBUG4K_LOAD("dyld_all_image_infos_address 0x%llx dyld_slide_amount 0x%llx\n", (uint64_t)dyld_version_address, (uint64_t)dyld_slide_amount);
			}
			kr = vm_fault(current_map(),
			    vm_map_trunc_page(dyld_all_image_infos_address + dyld_slide_amount,
			    vm_map_page_mask(current_map())),
			    VM_PROT_READ | VM_PROT_WRITE,
			    FALSE, VM_KERN_MEMORY_NONE,
			    THREAD_UNINT, NULL, 0);
			if (kr != KERN_SUCCESS) {
//				printf("%s:%d map %p va 0x%llx -> 0x%x\n", __FUNCTION__, __LINE__, current_map(), vm_map_trunc_page(dyld_all_image_infos_address + dyld_slide_amount, vm_map_page_mask(current_map())), kr);
			}
		}
	}
}

static int
sysctl_libmalloc_experiments SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg2, req)
	int changed;
	errno_t error;
	uint64_t value = os_atomic_load_wide(&libmalloc_experiment_factors, relaxed);

	error = sysctl_io_number(req, value, sizeof(value), &value, &changed);
	if (error) {
		return error;
	}

	if (changed) {
		os_atomic_store_wide(&libmalloc_experiment_factors, value, relaxed);
	}

	return 0;
}

EXPERIMENT_FACTOR_PROC(_kern, libmalloc_experiments, CTLTYPE_QUAD | CTLFLAG_RW, 0, 0, &sysctl_libmalloc_experiments, "A", "");

/* meizu: park marker for the offline patch set */
