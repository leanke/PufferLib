#ifndef POKERED_VISITED_H
#define POKERED_VISITED_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)
#define VISITED_BYTES (VISITED_COORDS_SIZE / 8)

static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
    uint32_t cx = x < MAX_X ? x : MAX_X - 1;
    uint32_t cy = y < MAX_Y ? y : MAX_Y - 1;
    return (uint32_t)map * (MAX_X * MAX_Y) + cx * MAX_Y + cy;
}

static inline bool vbit_get(const uint8_t *bits, uint32_t idx) {
    return (bits[idx >> 3] >> (idx & 7)) & 1;
}

static inline bool vbit_test_set(uint8_t *bits, uint32_t idx) {
    uint8_t mask = (uint8_t)(1u << (idx & 7));
    if (bits[idx >> 3] & mask)
        return false;
    bits[idx >> 3] |= mask;
    return true;
}

#endif
