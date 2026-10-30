#ifndef GOP_HANDOFF_BOUNDS_H
#define GOP_HANDOFF_BOUNDS_H
#include "FramebufferBounds.h"

/* Decoded values, not a wire ABI. Reservation must be independently established. */
struct GopHandoff {
    uint64_t base, size, width, height, row;
    uint32_t format;
};
struct GopConsole {
    uint64_t base, width, height, row, depth, rotate, offset;
};
struct GopReservation { uint64_t base, size; };
enum { GOP_FORMAT_BGR_RESERVED_8 = 1 };

/* A successful result proves arithmetic containment only, not ownership. */
static inline bool gop_handoff_bounds(const GopHandoff *g, const GopConsole *c,
    const GopReservation *reserved, uint64_t page, FBLayout *out) {
    if (!g || !c || !reserved || !out || g->format != GOP_FORMAT_BGR_RESERVED_8 ||
        !g->size || g->base > UINT64_MAX - g->size ||
        !reserved->size || reserved->base > UINT64_MAX - reserved->size ||
        c->offset || c->base != g->base || c->width != g->width ||
        c->height != g->height || c->row != g->row) return false;
    FBLayout candidate;
    if (!fb_layout(c->base, c->width, c->height, c->row, c->depth,
            c->rotate, page, &candidate)) return false;
    if (candidate.mapLength > g->size || g->base < reserved->base ||
        g->base > reserved->base + reserved->size ||
        candidate.mapLength > reserved->base + reserved->size - g->base) return false;
    *out = candidate;
    return true;
}
#endif
