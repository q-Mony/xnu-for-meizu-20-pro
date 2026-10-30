/*
 * Copyright (c) 2005-2006 Apple Computer, Inc. All rights reserved.
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
/*
 * @OSF_COPYRIGHT@
 */
/*
 * @APPLE_FREE_COPYRIGHT@
 */

#include <kern/spl.h>
#include <mach/std_types.h>
#include <types.h>
#include <kern/thread.h>
#include <console/serial_protos.h>
#include <libkern/section_keywords.h>

extern void cons_cinput(char ch);               /* The BSD routine that gets characters */

SECURITY_READ_ONLY_LATE(unsigned int) serialmode;                               /* Serial mode keyboard and console control */

/*
 *  This routine will start a thread that polls the serial port, listening for
 *  characters that have been typed.
 */

void
serial_keyboard_init(void)
{
	kern_return_t   result;
	thread_t                thread;

	if (!(serialmode & SERIALMODE_INPUT)) { /* Leave if we do not want a serial console */
		return;
	}

	kprintf("Serial keyboard started\n");
	result = kernel_thread_start_priority((thread_continue_t)serial_keyboard_start, NULL, MAXPRI_KERNEL, &thread);
	if (result != KERN_SUCCESS) {
		panic("serial_keyboard_init");
	}

	thread_deallocate(thread);
}

void
serial_keyboard_start(void)
{
	/* Go see if there are any characters pending now */
	serial_keyboard_poll();
}

__dead2
void
serial_keyboard_poll(void)
{
	int chr;
	uint64_t next;

	while (1) {
		chr = _serial_getc(false); /* Get a character if there is one */
		if (chr < 0) { /* The serial buffer is empty */
			break;
		}
		cons_cinput((char)chr); /* Buffer up the character */
	}

	clock_interval_to_deadline(16, 1000000, &next); /* Get time of pop */

	assert_wait_deadline((event_t)serial_keyboard_poll, THREAD_UNINT, next); /* Show we are "waiting" */
	thread_block((thread_continue_t)serial_keyboard_poll); /* Wait for it */
	panic("serial_keyboard_poll: Shouldn't never ever get here...");
}

boolean_t
console_is_serial(void)
{
	return cons_ops_index == SERIAL_CONS_OPS;
}

int
switch_to_video_console(void)
{
	int old_cons_ops = cons_ops_index;
	cons_ops_index = VC_CONS_OPS;
	return old_cons_ops;
}

int
switch_to_serial_console(void)
{
	extern bool serial_console_enabled;
	int old_cons_ops = cons_ops_index;

	if (serial_console_enabled) {
		cons_ops_index = SERIAL_CONS_OPS;
	}

	return old_cons_ops;
}

/* The switch_to_{video,serial,kgdb}_console functions return a cookie that
 *  can be used to restore the console to whatever it was before, in the
 *  same way that splwhatever() and splx() work.  */
void
switch_to_old_console(int old_console)
{
	static boolean_t squawked;
	uint32_t ops = old_console;

	if ((ops >= nconsops) && !squawked) {
		squawked = TRUE;
		printf("switch_to_old_console: unknown ops %d\n", ops);
	} else {
		cons_ops_index = ops;
	}
}

/* Private bookkeeping in the existing flags word; no printbuf ABI change. */
#define CONS_PB_NOVA_LINE_START 0x4

void
console_printbuf_state_init(struct console_printbuf_state * data, int write_on_newline, int can_block)
{
	if (data == NULL) {
		return;
	}
	bzero(data, sizeof(struct console_printbuf_state));
	data->flags = CONS_PB_NOVA_LINE_START;
	if (write_on_newline) {
		data->flags |= CONS_PB_WRITE_NEWLINE;
	}
	if (can_block) {
		data->flags |= CONS_PB_CANBLOCK;
	}
}

/* meizu: console ring mirrored from the printbuf so a periodic UFS writer can persist it.
 * The nabu autolog design: never touch storage from the panic path, snapshot instead. */
#include <console/meizu_log.h>
static char meizu_log_ring[MEIZU_LOG_RING_SIZE];
static volatile uint64_t meizu_log_written;

void
meizu_log_putc(int ch)
{
	uint64_t at = __atomic_fetch_add(&meizu_log_written, 1, __ATOMIC_RELAXED);
	meizu_log_ring[at & (MEIZU_LOG_RING_SIZE - 1)] = (char)ch;
}

/* Timestamp only this Nova diagnostic ARM64 development build. The test
 * seam replaces register reads, never the production formatter/ring writer. */
#if (defined(__arm64__) && DEVELOPMENT) || defined(NOVA_LOG_TIMESTAMP_TEST)
#define NOVA_LOG_TIMESTAMP_ENABLED 1
#else
#define NOVA_LOG_TIMESTAMP_ENABLED 0
#endif

#if NOVA_LOG_TIMESTAMP_ENABLED
#ifdef NOVA_LOG_TIMESTAMP_TEST
extern void nova_log_test_read_counter(uint64_t *, uint64_t *);
#endif
static void
meizu_log_hex(char *out, uint64_t value, unsigned digits)
{
	static const char hex[] = "0123456789abcdef";
	for (unsigned i = 0; i < digits; i++) {
		out[digits - 1 - i] = hex[value & 15];
		value >>= 4;
	}
}
#endif

/* One reservation keeps the prefix (and a printbuf's current first byte)
 * contiguous with respect to other reservations. It does not make a whole
 * message atomic or publish a committed snapshot: the original char ring
 * still permits interleaving and a concurrent snapshot can see in-flight
 * stores. No printf, allocation, locks, CPU-data lookup, or storage I/O here. */
static void
meizu_log_timestamp_emit(char kind, int append_char, int ch)
{
#if NOVA_LOG_TIMESTAMP_ENABLED
	uint64_t ticks, hz;
#ifdef NOVA_LOG_TIMESTAMP_TEST
	nova_log_test_read_counter(&ticks, &hz);
#else
	__asm__ volatile("isb\n\tmrs %0, cntvct_el0\n\tmrs %1, cntfrq_el0"
	    : "=r"(ticks), "=r"(hz) :: "memory");
#endif
	char record[] = "[nova-ts:v1 c=cntvct t=0000000000000000 f=00000000 k=P] ";
	unsigned length = sizeof(record) - 1;
	meizu_log_hex(record + sizeof("[nova-ts:v1 c=cntvct t=") - 1, ticks, 16);
	meizu_log_hex(record + sizeof("[nova-ts:v1 c=cntvct t=0000000000000000 f=") - 1, hz, 8);
	record[length - 3] = kind;
	/* Reuse the string's trailing NUL slot for the original first byte. */
	if (append_char) record[length++] = (char)ch;
	uint64_t at = __atomic_fetch_add(&meizu_log_written, length, __ATOMIC_RELAXED);
	for (unsigned i = 0; i < length; i++) {
		meizu_log_ring[(at + i) & (MEIZU_LOG_RING_SIZE - 1)] = record[i];
	}
#else
	(void)kind;
	if (append_char) meizu_log_putc(ch);
#endif
}

/* Bare message sites call this once immediately before their first byte.
 * They keep their existing character writes and need not hold any lock. */
void
meizu_log_timestamp_record(char kind)
{
	if (kind == 'R') meizu_log_timestamp_emit(kind, 0, 0);
}

/* These phase helpers are called only at ordinary BSD boot/mount sites.
 * Never call them from the timestamp, putc, IRQ, or exception paths. A begin
 * timestamp precedes its printf: that printing cost is part of the phase's
 * elapsed interval. This is wall elapsed time, not exclusive function CPU. */
#if NOVA_LOG_TIMESTAMP_ENABLED
static uint64_t nova_boot_phase_sequence;
#endif
uint64_t nova_boot_phase_begin(const char *phase);
void nova_boot_phase_end(const char *phase, uint64_t id, int result);

uint64_t
nova_boot_phase_begin(const char *phase)
{
#if NOVA_LOG_TIMESTAMP_ENABLED
	if (phase == NULL) return 0;
	uint64_t id = __atomic_add_fetch(&nova_boot_phase_sequence, 1, __ATOMIC_RELAXED);
	uint64_t ticks = mach_absolute_time(), ns;
	absolutetime_to_nanoseconds(ticks, &ns);
	printf("nova: bootphase v=1 phase=%s id=%llu edge=begin clock=mach_absolute ticks=%llu ns=%llu result=0\n",
	    phase, (unsigned long long)id, (unsigned long long)ticks, (unsigned long long)ns);
	return id;
#else
	(void)phase;
	return 0;
#endif
}

void
nova_boot_phase_end(const char *phase, uint64_t id, int result)
{
#if NOVA_LOG_TIMESTAMP_ENABLED
	if (phase == NULL || id == 0) return;
	uint64_t ticks = mach_absolute_time(), ns;
	absolutetime_to_nanoseconds(ticks, &ns);
	printf("nova: bootphase v=1 phase=%s id=%llu edge=end clock=mach_absolute ticks=%llu ns=%llu result=%d\n",
	    phase, (unsigned long long)id, (unsigned long long)ticks, (unsigned long long)ns, result);
#else
	(void)phase;
	(void)id;
	(void)result;
#endif
}

uint64_t
meizu_log_snapshot(char *out, uint64_t cap, uint64_t *snapshot_total)
{
	uint64_t total = __atomic_load_n(&meizu_log_written, __ATOMIC_RELAXED);
	*snapshot_total = total;
	uint64_t count = total < MEIZU_LOG_RING_SIZE ? total : MEIZU_LOG_RING_SIZE;
	uint64_t i;

	if (cap < count) {
		count = cap;
	}
	for (i = 0; i < count; i++) {
		out[i] = meizu_log_ring[(total - count + i) & (MEIZU_LOG_RING_SIZE - 1)];
	}
	return count;
}

uint64_t
meizu_log_total(void)
{
	return __atomic_load_n(&meizu_log_written, __ATOMIC_RELAXED);
}

void
console_printbuf_putc(int ch, void * arg)
{
	struct console_printbuf_state * info = (struct console_printbuf_state *)arg;
	if (info->total == 0 || (info->flags & CONS_PB_NOVA_LINE_START)) {
		meizu_log_timestamp_emit(info->total == 0 ? 'P' : 'L', 1, ch);
	} else {
		meizu_log_putc(ch);
	}
	/* Track input, not the truncated/flushed screen buffer's last byte. */
	if (ch == '\n') info->flags |= CONS_PB_NOVA_LINE_START;
	else info->flags &= ~CONS_PB_NOVA_LINE_START;
	info->total += 1;
	if (info->pos < (SERIAL_CONS_BUF_SIZE - 1)) {
		info->str[info->pos] = (char)ch;
		info->pos += 1;
	} else {
		/*
		 * when len(line) > SERIAL_CONS_BUF_SIZE, we truncate the message
		 * if boot-arg 'drain_uart_sync=1' is set, then
		 * drain all the buffer right now and append new ch
		 */
		if (serialmode & SERIALMODE_SYNCDRAIN) {
			info->str[info->pos] = '\0';
			console_write(info->str, info->pos);
			info->pos            = 0;
			info->str[info->pos] = (char)ch;
			info->pos += 1;
		}
	}

	info->str[info->pos] = '\0';
	/* if newline, then try output to console */
	if (ch == '\n' && info->flags & CONS_PB_WRITE_NEWLINE) {
		console_write(info->str, info->pos);
		info->pos            = 0;
		info->str[info->pos] = '\0';
	}
}

void
console_printbuf_clear(struct console_printbuf_state * info)
{
	if (info->pos != 0) {
		console_write(info->str, info->pos);
	}
	info->pos = 0;
	info->str[info->pos] = '\0';
	info->total = 0;
	info->flags |= CONS_PB_NOVA_LINE_START;
}
