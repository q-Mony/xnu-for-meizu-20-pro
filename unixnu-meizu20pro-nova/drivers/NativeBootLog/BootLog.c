// SPDX-License-Identifier: MIT
// Kernel-side framebuffer boot log for the Meizu 20 Pro bring-up.
//
// There is no serial console on this phone, so every kernel step has to be
// visible on the panel. This module owns a text console on the frame buffer the
// boot loader handed over in boot_args:
//
//   native_blog_start(args)  called right after the MMU is on; it learns the
//                            frame buffer geometry. This phone's frame buffer
//                            lies outside the owned window, so nothing is drawn
//                            until a mapping exists.
//   native_blog_kv_ready()   called once the kernel map exists; it maps the
//                            frame buffer, replays the checkpoints recorded
//                            until then, and takes over kprintf
//   native_blog_stage(name, value)
//                            a named checkpoint, drawn as one line (recorded
//                            while the frame buffer is unreachable)
//   native_blog_putc(c)      kprintf sink (PE_kputc), so the kernel's own log
//                            appears on the screen too
//   native_blog_irq()        interrupt counter; periodic status is capped at 1 Hz
//
// Nothing here touches registers, translation or the display mode: it only
// writes pixels into the frame buffer described by boot_args and cleans the
// D-cache for the rows it drew.
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned long ulong;

/* Kernel globals (no headers: this file must stay independent of the kernel
 * schedule so the archive builds against the same flags as any kernel object). */
extern u8 iso_font[];
extern u64 gPhysBase, gVirtBase, real_phys_size;
extern void (*PE_kputc)(char);
extern u8 disable_kprintf_output, disable_serial_output;
/* drivers/ArmCpu: GIC/timer bring-up evidence published for observation. */
extern volatile unsigned int armcpu_gic_ready;
extern volatile unsigned long long armcpu_timer_interrupts;
/* Owner-chain progress, from the kernel's own counters (bsd_ast -> bsdinit_task ->
 * load_init_program -> Data mount), so the last state stays on the status row. */
extern volatile int meizu_bsd_ast_count;
extern volatile int meizu_bsdinit_task_count;
extern volatile int meizu_load_init_count;
extern volatile int native_owner_data_status;
extern volatile int native_owner_data_stage;
extern volatile int meizu_exec_count;
extern volatile int meizu_exec_fail;
extern volatile int meizu_exec_errno;
extern volatile int meizu_mac_exec_count;
extern volatile int meizu_sys_count;
extern volatile int meizu_init_sys_count;
extern volatile int meizu_init_last_syscall;
extern volatile int meizu_init_quiet_seconds;
extern volatile int meizu_init_progress_state;
/* bsd/vm/vm_unix.c: where init's shared_region_check_np stopped. */
extern volatile int meizu_init_np_stage;
extern volatile unsigned long long meizu_init_np_sr_flags;
extern volatile unsigned long long meizu_init_np_map_in_progress;
extern volatile int meizu_init_np_kr;
extern volatile unsigned long long meizu_init_faults;
/* bsd/kern/kern_exec.c autolog + osfmk/kern/thread.c: init's kernel PC with the
 * slide already removed, so the value names a symbol in the build tree, plus the
 * scheduler state and wait event that say why it is not running. */
extern volatile unsigned long long meizu_init_kpc_unslid;
extern volatile unsigned long long meizu_init_kpc_hist[4];
extern volatile unsigned int meizu_init_tstate;
extern volatile unsigned long long meizu_init_wait_event;
/* osfmk/arm64/machine_routines.c: maps device memory into kernel_map. */
extern u64 ml_io_map_unmappable(u64 phys, u64 size, u32 flags);
extern void kprintf(const char *format, ...);
extern void printf(const char *format, ...);
extern void meizu_log_putc(int ch);
extern void meizu_log_timestamp_record(char kind);
uint64_t native_blog_boot_video_format(char *, uint64_t);
/* osfmk/kern/thread.c: is the interrupted thread the first user process? */
extern int meizu_this_is_init(void);

/* osfmk/console/video_console.c: the kernel's own console description. Only the
 * prefix that matters here is declared; the layout is the ABI of video_console.h. */
struct vc_info {
	u32 v_height;
	u32 v_width;
	u32 v_depth;
	u32 v_rowbytes;
	u64 v_baseaddr;
};
extern struct vc_info vinfo;

#define SCALE      2
#define CELL_W     8
#define CELL_H     16
#define LINE_H     (CELL_H * SCALE)
#define COLS_MAX   90
#define ROWS_MAX   64

static u8 *fb;                       /* current writable frame buffer address */
/* Published for the early fault reporter: it can only draw where a mapping is
 * known, so it reads all three instead of the boot args. */
volatile u64 native_blog_frame;      /* mapped frame buffer, 0 while unmapped */
volatile u64 native_blog_geometry;   /* (stride << 32) | (width << 16) | height */
volatile u64 native_blog_phase;
volatile u64 meizu_init_el0_pc;
static volatile u64 sample_elr;
static volatile u64 sample_spsr;      /* 0 not started, 1 identity, 2 kernel, 3 no mapping */
static volatile u64 sample_kern_elr[4];
static volatile u64 sample_kern_spsr[4];
static u64 irq_el0_count;
static u64 irq_el1_count;
volatile u64 meizu_init_irq_count;
volatile u64 meizu_init_el0_sample_count;
volatile u64 meizu_init_el1_pc;
volatile u64 meizu_init_el1_sample_count;
static u64 fb_pa;                    /* frame buffer physical address */
/* Original bootloader handoff, before validation or vinfo replacement. */
static u64 boot_video[6];            /* base, display, rowbytes, width, height, depth */
static unsigned boot_video_saved, boot_video_logged;
static u64 win_phys, win_size;       /* the owned window from boot args */
static u32 stride, width, height;
static u32 cols, rows, top, status_row;
static u32 cursor_row, cursor_col;
static u32 page;
static unsigned usable;              /* the geometry is valid; fb may still be unmapped */
static unsigned clean_rows = 1;      /* rows need D-cache cleaning (mapped Normal) */
static volatile unsigned active, busy;
static u32 putc_since_status __attribute__((unused));

static volatile u64 irq_count, fiq_count, stage_count;
/* Separate nonblocking gate protects the periodic budget and its statistics.
 * It does not claim to fix the older console-wide volatile busy protocol. */
static unsigned periodic_gate;
static unsigned periodic_started;
static u64 periodic_last_end, periodic_hz;
static u64 periodic_draws, periodic_ticks, periodic_max_ticks;
static u64 periodic_throttled, periodic_clock_errors;
static u64 blog_scrolls;
static u64 identity_ttbr0;           /* TTBR0 while the bootstrap map is live */
static unsigned early_drawn;         /* the console drew through that map */

/* Checkpoints taken before the frame buffer is reachable wait here; the labels
 * are kernel read-only strings, so a pointer is enough. */
#define RECORD_MAX 24
static const char *rec_name[RECORD_MAX];
static u64 rec_value[RECORD_MAX];
static u32 rec_count;

static void native_blog_take_kprintf(void);
static void blog_status(void);

/* No cpu_data or scheduler dependency in the early interrupt hook. */
static u64
blog_counter(void)
{
	u64 value;
	__asm__ volatile ("isb\n\tmrs %0, cntvct_el0" : "=r"(value) :: "memory");
	return value;
}

static u64
blog_counter_hz(void)
{
	u64 value;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(value));
	return value;
}

/* One weak CAS, no software spin or waiting in IRQ/FIQ context. A spurious
 * failure merely skips a diagnostic update. */
static unsigned
blog_periodic_try_enter(void)
{
	unsigned expected = 0;
	return __atomic_compare_exchange_n(&periodic_gate, &expected, 1, 1,
	    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void
blog_periodic_leave(void)
{
	__atomic_store_n(&periodic_gate, 0, __ATOMIC_RELEASE);
}

/* The early entry cannot rely on logging being initialized. Emit from the
 * kernel-map checkpoint directly into the persistent console ring instead. */
static void
blog_log_boot_video(void)
{
	static const char *const names[6] = {
		" base=0x", " display=0x", " rowbytes=0x",
		" width=0x", " height=0x", " depth=0x"
	};
	const char *s;
	u32 i;
	int shift;

	if (!__atomic_load_n(&boot_video_saved, __ATOMIC_ACQUIRE) || boot_video_logged) {
		return;
	}
	boot_video_logged = 1;
	meizu_log_timestamp_record('R');
	s = "[boot-video raw]";
	while (*s) {
		meizu_log_putc(*s++);
	}
	for (i = 0; i < 6; i++) {
		s = names[i];
		while (*s) {
			meizu_log_putc(*s++);
		}
		for (shift = 60; shift >= 0; shift -= 4) {
			meizu_log_putc("0123456789abcdef"[(boot_video[i] >> shift) & 15]);
		}
	}
	meizu_log_putc('\n');
}

/* Copy the immutable bootloader handoff into each persistent snapshot. */
uint64_t
native_blog_boot_video_format(char *out, uint64_t cap)
{
	static const char *const names[6] = {
		" base=0x", " display=0x", " rowbytes=0x",
		" width=0x", " height=0x", " depth=0x"
	};
	const char *s;
	uint64_t n = 0;
	u32 i;
	int shift;

	/* The complete line is 172 bytes; never emit a partial record. */
	if (!__atomic_load_n(&boot_video_saved, __ATOMIC_ACQUIRE) || cap < 172) {
		return 0;
	}
	s = "[boot-video raw]";
	while (*s) {
		out[n++] = *s++;
	}
	for (i = 0; i < 6; i++) {
		s = names[i];
		while (*s) {
			out[n++] = *s++;
		}
		for (shift = 60; shift >= 0; shift -= 4) {
			out[n++] = "0123456789abcdef"[(boot_video[i] >> shift) & 15];
		}
	}
	out[n++] = '\n';
	return n;
}

static inline void
blog_clean(u64 first, u64 last)
{
	u64 a = first & ~63UL;
	u64 end = (last + 63) & ~63UL;

	if (!clean_rows) {
		return;
	}
	for (; a < end; a += 64) {
		__asm__ volatile ("dc civac, %0" : : "r" (a) : "memory");
	}
	__asm__ volatile ("dsb sy" : : : "memory");
}

static void
blog_clean_line(u32 row)
{
	u64 y0 = top + (u64)row * LINE_H;
	u64 first = (u64)fb + y0 * stride;
	blog_clean(first, first + LINE_H * (u64)stride);
}

static u64
blog_ttbr0(void)
{
	u64 v;

	__asm__ volatile ("mrs %0, ttbr0_el1" : "=r" (v));
	return v;
}

/* The patcher adds the frame buffer to the bootstrap identity map at the MMU
 * site, so the console can draw until arm_vm_init switches TTBR0 away from it.
 * The frame buffer itself is below the window, so this map is the only way to
 * put the first post-MMU lines on the panel. */
static unsigned
blog_identity_ok(void)
{
	/* Once promoted the frame buffer is a kernel mapping, so the identity
	 * map's TTBR0 no longer matters. */
	return native_blog_phase == 2 || !early_drawn || blog_ttbr0() == identity_ttbr0;
}

static void
blog_demote(void)
{
	active = 0;
	fb = 0;
	native_blog_frame = 0;
	native_blog_phase = 3;
	clean_rows = 0;
}

/* One 8x16 glyph from the kernel's own ISO font, scaled. Zero bits paint black,
 * so a cell is always fully repainted. */
static void
blog_glyph(u32 cx, u32 cy, char c)
{
	const u8 *glyph = iso_font + ((u8)c) * CELL_H;
	u32 x = cx * CELL_W * SCALE;
	u64 y = top + (u64)cy * LINE_H;
	u32 fx, fy, r;

	for (r = 0; r < CELL_H; r++) {
		u8 bits = glyph[r];

		for (fx = 0; fx < CELL_W; fx++) {
			/* iso_font rows run least-significant bit first. */
			u32 color = (bits & (1u << fx)) ? 0xFFFFFFFFu : 0u;

			for (fy = 0; fy < SCALE; fy++) {
				u32 *p = (u32 *)(fb + (y + r * SCALE + fy) * stride + (u64)(x + fx * SCALE) * 4);
				u32 k;

				for (k = 0; k < SCALE; k++) {
					p[k] = color;
				}
			}
		}
	}
}

static void
blog_clear_row(u32 row)
{
	u64 y = top + (u64)row * LINE_H;
	u32 r;

	for (r = 0; r < LINE_H; r++) {
		u32 *p = (u32 *)(fb + (y + r) * stride);
		u32 k;

		for (k = 0; k < cols * CELL_W * SCALE; k++) {
			p[k] = 0;
		}
	}
}

static void blog_putc(char c);
static void blog_stage_line(const char *name, u64 value);

static void
blog_clear_page(void)
{
	u32 r;

	for (r = 0; r < rows; r++) {
		blog_clear_row(r);
	}
}

static void
blog_newline(void)
{
	cursor_col = 0;
	blog_clean_line(cursor_row);
	if (++cursor_row >= rows) {
		u32 r;
		u64 row_bytes = (u64)LINE_H * stride;
		u64 *dst = (u64 *)(fb + (u64)top * stride);
		u64 words = row_bytes / 8;
		/* Count real scrolling, not persistent-log lines. */
		__atomic_fetch_add(&blog_scrolls, 1, __ATOMIC_RELAXED);

		/* Scroll the page up one text row instead of clearing it: a flashing
		 * panel cannot be photographed. */
		for (r = 0; r + 1 < rows; r++) {
			u64 *from = (u64 *)((u8 *)dst + (r + 1) * row_bytes);
			u64 *to = (u64 *)((u8 *)dst + r * row_bytes);
			u64 i;
			for (i = 0; i < words; i++) {
				to[i] = from[i];
			}
		}
		{
			u64 *last = (u64 *)((u8 *)dst + (rows - 1) * row_bytes);
			u64 i;
			for (i = 0; i < words; i++) {
				last[i] = 0;
			}
		}
		blog_clean_line(rows - 1);
		cursor_row = rows - 1;
		page++;
	}
}

static void
blog_putc(char c)
{
	if (c == '\n') {
		blog_newline();
		return;
	}
	if (c == '\r') {
		cursor_col = 0;
		return;
	}
	if (c == '\t') {
		u32 k;

		for (k = 0; k < 4; k++) {
			blog_putc(' ');
		}
		return;
	}
	if ((u8)c < 0x20 || (u8)c > 0x7e) {
		c = '.';
	}
	if (cursor_col >= cols) {
		blog_newline();
	}
	blog_glyph(cursor_col, cursor_row, c);
	cursor_col++;
	if (cursor_col >= cols) {
		blog_clean_line(cursor_row);
	}
}

static void
blog_str(const char *s)
{
	while (*s) {
		blog_putc(*s++);
	}
}

static void
blog_hex(u64 v)
{
	int i;

	for (i = 15; i >= 0; i--) {
		blog_putc("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
	}
}

__attribute__((unused)) static void
blog_hex32(u64 v)
{
	int i;

	for (i = 7; i >= 0; i--) {
		blog_putc("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
	}
}

static void
blog_dec(u64 v)
{
	char digits[21];
	u32 n = 0;

	if (v == 0) {
		blog_putc('0');
		return;
	}
	while (v && n < sizeof(digits)) {
		digits[n++] = (char)('0' + v % 10);
		v /= 10;
	}
	while (n) {
		blog_putc(digits[--n]);
	}
}

/* ---- public interface ---- */

/* Derive the text grid from the current geometry. Returns 0 if the frame buffer
 * is too small to hold a useful console. */
static u32
blog_layout(void)
{
	u32 total;

	cols = width / (CELL_W * SCALE);
	if (cols > COLS_MAX) {
		cols = COLS_MAX;
	}
	top = height * 5 / 64;
	total = (height - top - 260) / LINE_H;   /* leave the rounded corner clear */
	if (total > ROWS_MAX) {
		total = ROWS_MAX;
	}
	if (cols < 20 || total < 4) {
		return 0;
	}
	status_row = total - 1;
	rows = total - 1;
	return 1;
}

/* Called right after the MMU is switched on. The patcher adds the frame buffer
 * to the bootstrap identity map at the MMU site and passes mapped != 0 when
 * that succeeded; otherwise the console only learns the geometry and records
 * checkpoints until native_blog_kv_ready() maps the frame buffer. */
void
native_blog_start(u64 boot_args, u64 mapped)
{
	u64 w, h, rb, base;

	if (!boot_args || usable) {
		return;
	}
	if (!__atomic_load_n(&boot_video_saved, __ATOMIC_ACQUIRE)) {
		u32 i;
		for (i = 0; i < 6; i++) {
			boot_video[i] = *(volatile u64 *)(boot_args + 0x28 + (u64)i * 8);
		}
		__atomic_store_n(&boot_video_saved, 1, __ATOMIC_RELEASE);
	}
	base = *(volatile u64 *)(boot_args + 0x28);
	rb   = *(volatile u64 *)(boot_args + 0x38);
	w    = *(volatile u64 *)(boot_args + 0x40);
	h    = *(volatile u64 *)(boot_args + 0x48);
	win_phys = *(volatile u64 *)(boot_args + 0x10);
	win_size = *(volatile u64 *)(boot_args + 0x18);
	if (!base || rb < 4 || rb > 65536 || w < 320 || h < 320 || w > 8192 || h > 8192) {
		return;
	}
	fb_pa  = base;
	stride = (u32)rb;
	width  = (u32)w;
	height = (u32)h;
	if (!blog_layout()) {
		stride = width = height = 0;
		return;
	}
	usable = 1;
	native_blog_geometry = ((u64)stride << 32) | ((u64)(width & 0xFFFF) << 16) | (height & 0xFFFF);
	if (mapped) {
		/* The bootstrap entry is Device memory: no cache maintenance, and the
		 * identity map goes away inside arm_vm_init. */
		clean_rows = 0;
		identity_ttbr0 = blog_ttbr0();
		early_drawn = 1;
	} else if (!win_size || base < win_phys || base - win_phys >= win_size) {
		/* Outside the identity map: the reporter must not use the physical
		 * address either, so phase says "known but unreachable". */
		native_blog_phase = 3;
		return;
	} else {
		clean_rows = 1;
	}
	fb = (u8 *)base;
	cursor_row = cursor_col = 0;
	page = 0;
	native_blog_frame = base;
	native_blog_phase = 1;
	active = 1;

	blog_clear_row(0);
	blog_clear_row(status_row);
	blog_clean_line(0);
	blog_clean_line(status_row);
	blog_stage_line("MMU ON", base);
}

/* Draw one recorded checkpoint. */
static void
blog_replay(void)
{
	u32 i;

	for (i = 0; i < rec_count; i++) {
		blog_stage_line(rec_name[i], rec_value[i]);
	}
	rec_count = 0;
}

/* Called once kernel_map exists (right after the kernel's video console was
 * acquired): map the frame buffer into the kernel address space, replay what
 * happened before that, and take over kprintf. */
void
native_blog_kv_ready(void)
{
	u64 size;
	u64 va;
	u32 c, t, r;

	blog_log_boot_video();
	if (native_blog_phase == 2) {
		return;
	}
	if (!usable) {
		return;
	}
	busy = 1;
	va = 0;
	/* The kernel's video console maps the frame buffer for itself only when it
	 * was handed a kernel address. The boot console's v_baseaddr is the
	 * bootstrap identity address, which disappears once XNU installs a user
	 * pmap, so it must not be used here. */
	if (vinfo.v_depth == 32 && vinfo.v_baseaddr >= 0xFFFFFF8000000000ULL &&
	    vinfo.v_rowbytes >= 4 && vinfo.v_width >= 320 && vinfo.v_height >= 320) {
		va = vinfo.v_baseaddr;
		stride = vinfo.v_rowbytes;
		width = vinfo.v_width;
		height = vinfo.v_height;
		clean_rows = 1;
	} else {
		size = (u64)stride * height;
		va = ml_io_map_unmappable(fb_pa, size, 0x7 /* VM_WIMG_IO */);
		clean_rows = 0;   /* device memory: writes reach the panel directly */
	}
	if (!va) {
		busy = 0;
		return;
	}
	c = width / (CELL_W * SCALE);
	if (c > COLS_MAX) {
		c = COLS_MAX;
	}
	t = height * 5 / 64;
	r = (height - t - 260) / LINE_H;
	if (r > ROWS_MAX) {
		r = ROWS_MAX;
	}
	if (c < 20 || r < 6) {
		busy = 0;
		return;
	}
	cols = c;
	top = t;
	rows = r - 5;
	status_row = r - 5;
	fb = (u8 *)va;
	if (!early_drawn) {
		/* Nothing was drawn yet: start on a clean page. */
		cursor_row = cursor_col = 0;
		page = 0;
		blog_clear_page();
	} else if (cursor_row >= rows) {
		cursor_row = 0;
		cursor_col = 0;
	}
	native_blog_frame = va;
	native_blog_geometry = ((u64)stride << 32) | ((u64)(width & 0xFFFF) << 16) | (height & 0xFFFF);
	native_blog_phase = 2;
	active = 1;
	blog_clean_line(0);
	blog_clean_line(status_row);
	blog_replay();
	blog_stage_line("KV MAP", va);
	blog_status();
	busy = 0;
	native_blog_take_kprintf();
}

/* The kprintf sink. kprintf holds its lock and interrupts are off here, so this
 * must never block; a nested call (panic inside the drawing) just drops. */
void
native_blog_putc(char c)
{
	if (!active || busy) {
		return;
	}
	if (!blog_identity_ok()) {
		blog_demote();
		return;
	}
	busy = 1;
	blog_putc(c);
	busy = 0;
}

static void
blog_stage_line(const char *name, u64 value)
{
	u64 n;

	n = ++stage_count;
	cursor_col = 0;
	if (cursor_row) {
		cursor_row++;
		if (cursor_row >= rows) {
			blog_clear_page();
			cursor_row = 0;
		}
	}
	blog_putc('[');
	blog_dec(n);
	blog_putc(']');
	blog_putc(' ');
	blog_str(name);
	blog_putc('=');
	blog_hex(value);
	blog_putc('\n');
}

/* A checkpoint taken while the frame buffer has no mapping is kept here and
 * replayed by the first draw. */
static void
blog_record(const char *name, u64 value)
{
	if (rec_count >= RECORD_MAX) {
		return;
	}
	rec_name[rec_count] = name;
	rec_value[rec_count] = value;
	rec_count++;
}

void
native_blog_stage(const char *name, u64 value)
{
	if (active && !blog_identity_ok()) {
		blog_demote();
	}
	if (!active) {
		blog_record(name, value);
		return;
	}
	if (busy) {
		return;
	}
	busy = 1;
	blog_stage_line(name, value);
	busy = 0;
}

static void
blog_status(void)
{
	u32 row = cursor_row, col = cursor_col;
	u32 saved = cols;
	u32 line;
	extern volatile unsigned long long meizu_exec_entry;
	extern volatile unsigned long long meizu_exec_base;

	cursor_col = 0;
	for (line = 0; line < 5; line++) {
		cursor_row = status_row + line;
		cursor_col = 0;
		while (cursor_col < saved) {
			blog_glyph(cursor_col++, cursor_row, ' ');
		}
		blog_clean_line(cursor_row);
	}
	cols = 1000;
	for (line = 0; line < 5; line++) {
		cursor_row = status_row + line;
		cursor_col = 0;
		if (line == 0) {
			blog_str("IRQ=");
			blog_dec(irq_count);
			blog_str(" E0=");
			blog_dec(irq_el0_count);
			blog_str(" E1=");
			blog_dec(irq_el1_count);
			blog_str(" IC=");
			blog_dec(meizu_init_irq_count);
			blog_str(" BA=");
			blog_dec((u64)(u32)meizu_bsd_ast_count);
			blog_str(" BI=");
			blog_dec((u64)(u32)meizu_bsdinit_task_count);
			blog_str(" LI=");
			blog_dec((u64)(u32)meizu_load_init_count);
		} else if (line == 1) {
			blog_str("DS=");
			blog_dec((u64)(u32)native_owner_data_status);
			blog_str("/");
			blog_dec((u64)(u32)native_owner_data_stage);
			blog_str(" EX=");
			blog_dec((u64)(u32)meizu_exec_count);
			blog_str("/");
			blog_dec((u64)(u32)meizu_exec_fail);
			blog_str(" SY=");
			blog_dec((u64)(u32)meizu_sys_count);
			blog_str(" PF=");
			blog_dec(meizu_init_faults);
			blog_str(" IY=");
			blog_dec((u64)(u32)meizu_init_sys_count);
			blog_str(" SC=");
			blog_dec((u64)(u32)meizu_init_last_syscall);
			blog_str(" ST=");
			if (meizu_init_progress_state == 1) {
				blog_str("ACT");
			} else if (meizu_init_progress_state == 2) {
				blog_str("PC");
			} else if (meizu_init_progress_state == 3) {
				blog_str("SAME");
			} else if (meizu_init_progress_state == 4) {
				blog_str("KPC");
			} else if (meizu_init_progress_state == 5) {
				blog_str("KSAME");
			} else if (meizu_init_progress_state == 6) {
				blog_str("KIRQ");
			} else if (meizu_init_progress_state == 7) {
				blog_str("NOIRQ");
			} else {
				blog_str("WAIT");
			}
			blog_str(" Q=");
			blog_dec((u64)(u32)meizu_init_quiet_seconds);
		} else if (line == 2) {
			/* Where init's shared-region syscall stopped, latched in kernel memory.
			 * NS = stage, NF = sr_first_mapping, NM = sr_mapping_in_progress,
			 * NK = the kern_return it returned. */
			blog_str("NS=");
			blog_dec((u64)(u32)meizu_init_np_stage);
			blog_str(" NF=");
			blog_hex32(meizu_init_np_sr_flags >> 32);
			blog_hex32(meizu_init_np_sr_flags);
			blog_str(" NK=");
			blog_dec((u64)(u32)meizu_init_np_kr);
		} else if (line == 3) {
			blog_str("NM=");
			blog_hex32(meizu_init_np_map_in_progress >> 32);
			blog_hex32(meizu_init_np_map_in_progress);
			blog_str(" H1=");
			blog_hex32(meizu_init_kpc_hist[0] >> 32);
			blog_hex32(meizu_init_kpc_hist[0]);
			blog_str(" H2=");
			blog_hex32(meizu_init_kpc_hist[1] >> 32);
			blog_hex32(meizu_init_kpc_hist[1]);
		} else {
			blog_str("PC=");
			blog_hex32(meizu_init_el0_pc >> 32);
			blog_hex32(meizu_init_el0_pc);
			blog_str(" KP=");
			blog_hex32(meizu_init_el1_pc >> 32);
			blog_hex32(meizu_init_el1_pc);
			blog_str(" KU=");
			blog_hex32(meizu_init_kpc_unslid >> 32);
			blog_hex32(meizu_init_kpc_unslid);
			blog_str(" TS=");
			blog_dec((u64)(u32)meizu_init_tstate);
		}
	}
	cols = saved;
	cursor_row = row;
	cursor_col = col;
}

/* Both interrupt sources share one elapsed-time budget, after the original
 * 512/64-event filters. Keep one full second after the preceding draw ends.
 * Initial KV status and direct checkpoints/kprintf are not rate limited. */
static void
blog_periodic_status(void)
{
	u64 now, frequency, elapsed, end;
	if (!blog_periodic_try_enter()) {
		return;
	}
	if (!active || busy) {
		goto done;
	}
	if (!blog_identity_ok()) {
		blog_demote();
		goto done;
	}
	now = blog_counter();
	frequency = blog_counter_hz();
	if (!frequency || frequency > 0xffffffffULL) {
		periodic_clock_errors++;
		goto done;
	}
	if (periodic_started) {
		elapsed = now - periodic_last_end;
		/* Unsigned subtraction admits a real 64-bit wrap. A backward
		 * discontinuity or frequency change rebases without a burst. */
		if (elapsed > 0x7fffffffffffffffULL || frequency != periodic_hz) {
			periodic_clock_errors++;
			periodic_last_end = now;
			periodic_hz = frequency;
			goto done;
		}
		if (elapsed < frequency) {
			periodic_throttled++;
			goto done;
		}
	}
	busy = 1;
	now = blog_counter();
	blog_status();
	end = blog_counter();
	busy = 0;
	periodic_draws++;
	elapsed = end - now;
	if (elapsed <= 0x7fffffffffffffffULL) {
		periodic_ticks += elapsed;
		if (elapsed > periodic_max_ticks) {
			periodic_max_ticks = elapsed;
		}
	} else {
		periodic_clock_errors++;
	}
	periodic_last_end = end;
	periodic_hz = frequency;
	periodic_started = 1;
done:
	blog_periodic_leave();
}

/* Called only by the existing autolog thread. Snapshot under the same try-gate,
 * then print after release. At most 31 reports, ten seconds apart, and one
 * final report at the first cadence-eligible heartbeat after the 300-second budget.
 * Ticks are CNTVCT deltas, not a Boot-menu clock or an exclusive CPU budget. */
void
native_blog_status_timing_dump(void)
{
	static unsigned started, reports, finished;
	static u64 started_at, reported_at, report_hz;
	u64 now, frequency, age, draws, ticks, maximum, throttled, errors, scrolls;
	unsigned final;
	if (finished || reports >= 31) {
		return;
	}
	now = blog_counter();
	frequency = blog_counter_hz();
	if (!frequency || frequency > 0xffffffffULL) {
		return;
	}
	if (!started) {
		started = 1;
		started_at = reported_at = now;
		report_hz = frequency;
	}
	/* Never reset the boot-wide reporting budget on a broken clock. */
	if (frequency != report_hz || now - started_at > 0x7fffffffffffffffULL ||
	    now - reported_at > 0x7fffffffffffffffULL) {
		finished = 1;
		return;
	}
	age = now - started_at;
	final = age >= frequency * 300;
	if (reports && now - reported_at < frequency * 10) {
		return;
	}
	if (!blog_periodic_try_enter()) {
		return;
	}
	draws = periodic_draws;
	ticks = periodic_ticks;
	maximum = periodic_max_ticks;
	throttled = periodic_throttled;
	errors = periodic_clock_errors;
	scrolls = __atomic_load_n(&blog_scrolls, __ATOMIC_RELAXED);
	blog_periodic_leave();
	reported_at = now;
	reports++;
	if (final) {
		finished = 1;
	}
	printf("nova: blog-status clock=cntvct ticks=%llu hz=%llu age_ticks=%llu "
	    "dump=%u final=%u draws=%llu draw_ticks=%llu max_ticks=%llu "
	    "throttled=%llu clock_errors=%llu scrolls=%llu interval_seconds=1\n",
	    now, frequency, age, reports, final, draws, ticks, maximum,
	    throttled, errors, scrolls);
}

/* The timer is an IRQ on this platform and a FIQ on Apple ones; count both so
 * the status line shows which one drives the kernel's clock. */
void
native_blog_irq(void)
{
	u64 n = ++irq_count;
	u64 elr, spsr;

	if (!active) {
		return;
	}
	__asm__ volatile ("isb\n\tmrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile ("isb\n\tmrs %0, spsr_el1" : "=r"(spsr));
	if ((spsr & 0xf) == 0) {
		/* EL0: retain the first user process separately for init diagnostics. */
		sample_elr = elr;
		sample_spsr = spsr;
		if (meizu_this_is_init()) {
			meizu_init_el0_pc = elr;
			meizu_init_el0_sample_count++;
		}
		irq_el0_count++;
	} else {
		/* EL1: the kernel itself. Keep one sample per CPU: a thread that
		 * spins in the kernel shows up as the same PC on its own CPU. */
		u64 mpidr, cpu;
		__asm__ volatile ("isb\n\tmrs %0, mpidr_el1" : "=r"(mpidr));
		cpu = mpidr & 0xff;
		if (cpu < 4) {
			sample_kern_elr[cpu] = elr;
			sample_kern_spsr[cpu] = spsr;
		}
		irq_el1_count++;
		if (meizu_this_is_init()) {
			meizu_init_el1_pc = elr;
			meizu_init_el1_sample_count++;
		}
	}
	if (meizu_this_is_init()) {
		meizu_init_irq_count++;
	}
	if (busy || (n & 511)) {
		return;
	}
	blog_periodic_status();
}

void
native_blog_fiq(void)
{
	u64 n = ++fiq_count;

	if (!active || busy || (n & 63)) {
		return;
	}
	blog_periodic_status();
}

u64
native_blog_irq_count(void)
{
	return irq_count;
}

/* The kernel installed this table in start.s before the MMU was enabled and
 * never changes it again; the early fault reporter points VBAR at its own copy
 * until the kernel's exception machinery is up. */
extern u8 ExceptionVectorsBase[];

void
native_blog_restore_vectors(void)
{
	__asm__ volatile ("msr vbar_el1, %0\n\tisb" : : "r" (ExceptionVectorsBase) : "memory");
}

/* Debug aid for the bring-up: arm_vm_init switches TTBR0 away from the
 * bootstrap tables near its end, which would leave the console without a
 * mapping for the rest of the boot. The first checkpoint after that switch
 * puts the bootstrap root back (the frame buffer mapping lives there); user
 * space installs its own TTBR0 when the first thread runs. */
void
native_blog_keep_identity(void)
{
	if (!early_drawn || !identity_ttbr0) {
		return;
	}
	__asm__ volatile (
		"msr ttbr0_el1, %0\n\t"
		"dsb sy\n\t"
		"tlbi vmalle1\n\t"
		"dsb sy\n\t"
		"isb\n\t"
		: : "r" (identity_ttbr0) : "memory");
	if (!active) {
		fb = (u8 *)fb_pa;
		native_blog_frame = fb_pa;
		native_blog_phase = 1;
		clean_rows = 0;
		active = 1;
	}
}

/* Point kprintf at this console and stop the DEVELOPMENT build from dropping
 * kprintf output. Called once the startup functions have set PE_kputc. */
static void
native_blog_take_kprintf(void)
{
	if (!active) {
		return;
	}
	PE_kputc = native_blog_putc;
	disable_serial_output = 0;
	disable_kprintf_output = 0;
}
