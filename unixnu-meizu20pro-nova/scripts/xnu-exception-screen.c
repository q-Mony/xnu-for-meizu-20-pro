/* Early EL1 fault reporter for the kernel bring-up.
 *
 * Installed by patch-xnu-entry-probe.py alongside the post-MMU checkpoint: the
 * stage-2 cave points VBAR_EL1 at a 2 KiB-aligned table whose synchronous
 * entries branch here, before it enables the MMU. Any fault from then on draws
 * ESR_EL1, ELR_EL1, FAR_EL1, SCTLR_EL1 and TCR_EL1 on the frame buffer, so the
 * stage that stops is readable from a photograph instead of a silent hang.
 *
 * Where to draw comes from the boot log console: FRAME holds the frame buffer
 * only once it has a mapping this code can use, and GEOM carries the stride and
 * size, so nothing here has to read the (possibly unmapped) boot args. Before
 * the MMU is on that address does not exist; then PHASE is 0 and the physical
 * address from SCRATCH is used instead. With no console at all (FRAME 0, PHASE
 * set) drawing would fault again, so the reporter stays silent.
 *
 * All state is on the exception stack (STACK_TOP), because this code lives in
 * the kernel's read-only text.
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

#define SCALE 6

static const u8 FONT[22][7] = {
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},   /* 0 */
  {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},   /* 1 */
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},   /* 2 */
  {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},   /* 3 */
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},   /* 4 */
  {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},   /* 5 */
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},   /* 6 */
  {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},   /* 7 */
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},   /* 8 */
  {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},   /* 9 */
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   /* A */
  {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},   /* B */
  {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},   /* C */
  {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},   /* D */
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},   /* E */
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},   /* F */
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},   /* L */
  {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},   /* R */
  {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},   /* S */
  {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},   /* T */
  {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},   /* X */
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* space */
};

struct ctx {
  u8  *base;
  u32  rb, w, h, x;
};

static const u8 *glyph (char c)
{
  if (c >= '0' && c <= '9') {
    return FONT[c - '0'];
  }
  if (c >= 'A' && c <= 'F') {
    return FONT[10 + (c - 'A')];
  }
  switch (c) {
    case 'L': return FONT[16];
    case 'R': return FONT[17];
    case 'S': return FONT[18];
    case 'T': return FONT[19];
    case 'X': return FONT[20];
    case ' ': return FONT[21];
  }
  return 0;
}

static void px (struct ctx *g, u32 x, u32 y)
{
  if (x < g->w && y < g->h) {
    *(volatile u32 *)(g->base + (u64)y * g->rb + (u64)x * 4) = 0xFFFFFFFFu;
  }
}

static void ch (struct ctx *g, char c, u32 x, u32 y)
{
  const u8 *f = glyph (c);
  u32 r, q, dy, dx;

  if (!f) {
    return;
  }
  for (r = 0; r < 7; r++) {
    for (q = 0; q < 5; q++) {
      if (f[r] & (0x10 >> q)) {
        for (dy = 0; dy < SCALE; dy++) {
          for (dx = 0; dx < SCALE; dx++) {
            px (g, x + q * SCALE + dx, y + r * SCALE + dy);
          }
        }
      }
    }
  }
}

static void line (struct ctx *g, const char *label, u64 v, u32 y)
{
  u32 x = g->w / 24;
  int i;

  while (*label) {
    ch (g, *label++, x, y);
    x += 6 * SCALE;
  }
  x += 2 * SCALE;
  for (i = 11; i >= 0; i--) {
    ch (g, "0123456789ABCDEF"[(v >> (i * 4)) & 15], x, y);
    x += 6 * SCALE;
  }
}

/* The frame buffer mapping at this point is Normal memory with the D-cache on,
 * so the writes above are only visible to the display after a clean. */
static void clean (struct ctx *g, u32 y0, u32 y1)
{
  u32 y;
  u64 a;

  for (y = y0; y < y1; y++) {
    u64 first = (u64)(g->base + (u64)y * g->rb);
    u64 end   = first + (u64)g->w * 4;
    for (a = first & ~63UL; a < end; a += 64) {
      __asm__ volatile ("dc civac, %0" : : "r" (a) : "memory");
    }
  }
  __asm__ volatile ("dsb sy" : : : "memory");
}

__attribute__((noreturn)) static void park (void)
{
  for (;;) {
    __asm__ volatile ("wfe");
  }
}

__attribute__((noreturn, used))
void exc_main (u64 esr, u64 elr, u64 far, u64 sctlr, u64 tcr, u64 r0, u64 r1, u64 lr)
{
  volatile u64 *scratch = (volatile u64 *)SCRATCH;
  u64           args    = *scratch;
  struct ctx    g;
  u32           y;

  __asm__ volatile ("msr daifset, #15");
  g.base = (u8 *)0;
#if FRAME
  g.base = (u8 *)*(volatile u64 *)FRAME;
  if (g.base != 0) {
    u64 geom = *(volatile u64 *)GEOM;
    g.rb = (u32)(geom >> 32);
    g.w  = (u32)((geom >> 16) & 0xFFFF);
    g.h  = (u32)(geom & 0xFFFF);
  } else if (*(volatile u64 *)PHASE != 0) {
    /* The console exists but has no address this code can use. */
    park ();
  }
#endif
  if (g.base == 0) {
    if (args == 0) {
      park ();
    }
    g.base = (u8 *)*(volatile u64 *)(args + 0x28);
    g.rb   = (u32)*(volatile u64 *)(args + 0x38);
    g.w    = (u32)*(volatile u64 *)(args + 0x40);
    g.h    = (u32)*(volatile u64 *)(args + 0x48);
  }
  if ((g.rb < 4) || (g.w < 64) || (g.h < 64)) {
    park ();
  }

  /* Below the console's text area: the checkpoints above it stay readable. */
  y = g.h * 5 / 8;
  line (&g, "ESR", esr, y);   y += 9 * SCALE;
  line (&g, "ELR", elr, y);   y += 9 * SCALE;
  line (&g, "FAR", far, y);   y += 9 * SCALE;
  line (&g, "SCTLR", sctlr, y); y += 9 * SCALE;
  line (&g, "TCR", tcr, y);   y += 9 * SCALE;
  line (&g, "X0", r0, y);     y += 9 * SCALE;
  line (&g, "X2", r1, y);     y += 9 * SCALE;
  line (&g, "LR", lr, y);     y += 9 * SCALE;
  clean (&g, g.h * 5 / 8, y);
  park ();
}

/* Vector entry. The synchronous vectors of the patched table branch here with
 * the MMU on; take the exception stack so a fault raised while SP_EL1 was not in
 * use still has a writable, mapped stack. */
__attribute__((naked, noreturn, used))
void exc_entry (void)
{
  __asm__ volatile (
    "msr daifset, #15\n\t"
    "msr spsel, #1\n\t"
    "ldr x9, =%0\n\t"
    "mov sp, x9\n\t"
    "mov x5, x0\n\t"
    "mov x6, x2\n\t"
    "mov x7, x30\n\t"
    "mrs x0, esr_el1\n\t"
    "mrs x1, elr_el1\n\t"
    "mrs x2, far_el1\n\t"
    "mrs x3, sctlr_el1\n\t"
    "mrs x4, tcr_el1\n\t"
    "b exc_main\n\t"
    : : "i" (STACK_TOP));
}
