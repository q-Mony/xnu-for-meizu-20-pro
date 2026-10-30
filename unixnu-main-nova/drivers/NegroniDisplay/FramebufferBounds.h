#ifndef FRAMEBUFFER_BOUNDS_H
#define FRAMEBUFFER_BOUNDS_H
#include <stdint.h>
struct FBLayout { uint32_t width, height, row, length, mapLength; };
static inline bool fb_layout(uint64_t base, uint64_t width, uint64_t height,
    uint64_t row, uint64_t depth, uint64_t rotate, uint64_t page, FBLayout *out) {
    if (!out || !base || !page || (page & (page-1)) || (base & (page-1)) ||
        !width || !height || width > 16384 || height > 16384 ||
        depth != 32 || rotate || row > UINT32_MAX || row < width * 4 ||
        row > UINT32_MAX / height) return false;
    uint64_t length = row * height;
    if (!length || base > UINT64_MAX-length || page > UINT32_MAX ||
        length > UINT32_MAX - (page - 1)) return false;
    uint64_t mapped = (length + page - 1) & ~(page - 1);
    if (!mapped || base > UINT64_MAX - mapped) return false;
    out->width=(uint32_t)width; out->height=(uint32_t)height;
    out->row=(uint32_t)row; out->length=(uint32_t)length;
    out->mapLength=(uint32_t)mapped;
    return true;
}
static inline bool fb_rect(uint32_t width, uint32_t height, uint32_t row, uint64_t length,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!w || !h || x >= width || y >= height || w > width-x || h > height-y ||
        (uint64_t)width*4 > row) return false;
    uint64_t end = (uint64_t)(y+h-1)*row + (uint64_t)(x+w)*4;
    return end <= length;
}
#endif
