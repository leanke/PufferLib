#pragma once

// Depends on Terraria struct (included by terraria.h before this file)

// ─── Deterministic RNG ───────────────────────────────────────────────────────

static inline uint32_t wg_hash(uint32_t seed, uint32_t x, uint32_t y) {
    uint32_t h = seed ^ (x * 2654435761u) ^ (y * 2246822519u);
    h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
    return h;
}

static inline float wg_randf(uint32_t seed, int x, int y) {
    return (float)(wg_hash(seed, (uint32_t)x, (uint32_t)y) & 0xFFFFu) / 65535.0f;
}

// Interpolated 1D smooth noise (value noise)
static float wg_noise1d(uint32_t seed, float xf, int octave) {
    int xi = (int)floorf(xf);
    float t = xf - (float)xi;
    t = t * t * (3.0f - 2.0f * t); // smoothstep
    float v0 = wg_randf(seed + (uint32_t)octave * 999983u, xi,     0);
    float v1 = wg_randf(seed + (uint32_t)octave * 999983u, xi + 1, 0);
    return v0 + t * (v1 - v0);
}

// Surface height at tile column x (y=0 is top of world)
static int surface_height(uint32_t seed, int x) {
    float h = 0.0f;
    h += wg_noise1d(seed, x * 0.015f, 1) * 10.0f; // large hills
    h += wg_noise1d(seed, x * 0.04f,  2) * 4.0f;  // medium bumps
    h += wg_noise1d(seed, x * 0.1f,   3) * 2.0f;  // small details
    // Surface centered at WORLD_H/4, range [WORLD_H/5, WORLD_H/3]
    int base = WORLD_H / 4;
    int sh = base + (int)h - 8;
    if (sh < 4)            sh = 4;
    if (sh > WORLD_H / 3)  sh = WORLD_H / 3;
    return sh;
}

// ─── Main world generator ─────────────────────────────────────────────────────

static void generate_world(Terraria* env) {
    uint32_t seed = (uint32_t)env->rng;
    int surface[WORLD_W];

    // 1. Heightmap
    for (int x = 0; x < WORLD_W; x++)
        surface[x] = surface_height(seed, x);

    // 2. Fill tiles: air → grass→dirt→stone layer
    for (int ty = 0; ty < WORLD_H; ty++) {
        for (int tx = 0; tx < WORLD_W; tx++) {
            uint8_t t = 0;
            int sh = surface[tx];
            if (ty < sh) {
                TILE_SET_TYPE(t, TILE_AIR);
            } else if (ty == sh) {
                TILE_SET_TYPE(t, TILE_GRASS);
            } else if (ty <= sh + 6) {
                TILE_SET_TYPE(t, TILE_DIRT);
            } else {
                TILE_SET_TYPE(t, TILE_STONE);
            }
            // Background wall underground
            if (ty > sh) TILE_SET_WALL(t, 1);
            env->tiles[ty][tx] = t;
        }
    }

    // 3. Cave carving (cellular automata below surface)
    // Seed: 45% chance of air in underground region
    for (int ty = 0; ty < WORLD_H; ty++) {
        for (int tx = 0; tx < WORLD_W; tx++) {
            int sh = surface[tx];
            if (ty <= sh + 4) continue; // preserve surface
            if (wg_randf(seed ^ 0xDEAD1u, tx, ty) < 0.45f) {
                TILE_SET_TYPE(env->tiles[ty][tx], TILE_AIR);
            }
        }
    }

    // Smooth passes (cellular automata)
    uint8_t temp[WORLD_H][WORLD_W];
    for (int pass = 0; pass < 4; pass++) {
        memcpy(temp, env->tiles, sizeof(temp));
        for (int ty = 1; ty < WORLD_H - 1; ty++) {
            for (int tx = 1; tx < WORLD_W - 1; tx++) {
                int sh = surface[tx];
                if (ty <= sh + 3) continue;
                // Count non-air neighbors (3×3)
                int solid = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        solid += (TILE_TYPE(temp[ty+dy][tx+dx]) != TILE_AIR) ? 1 : 0;
                uint8_t tile = env->tiles[ty][tx];
                if (solid >= 5) {
                    // Fill with appropriate material
                    TileType fill = (ty > sh + 8) ? TILE_STONE : TILE_DIRT;
                    TILE_SET_TYPE(tile, fill);
                } else {
                    TILE_SET_TYPE(tile, TILE_AIR);
                }
                TILE_SET_WALL(tile, (ty > sh) ? 1 : 0);
                env->tiles[ty][tx] = tile;
            }
        }
    }

    // 4. Ore veins (scatter clusters at depth)
    // Coal: depth > sh+12
    for (int v = 0; v < 20; v++) {
        int cx = (int)(wg_randf(seed ^ 0x1111u, v, 0) * (WORLD_W - 4)) + 2;
        int cy = (int)(wg_randf(seed ^ 0x1111u, v, 1) * (WORLD_H * 0.6f) + WORLD_H * 0.35f);
        int r  = 1 + (int)(wg_randf(seed ^ 0x1111u, v, 2) * 2.0f);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy > r*r + 1) continue;
                int tx = cx + dx, ty2 = cy + dy;
                if (tx < 0 || tx >= WORLD_W || ty2 < 0 || ty2 >= WORLD_H) continue;
                if (TILE_TYPE(env->tiles[ty2][tx]) == TILE_STONE) {
                    TILE_SET_TYPE(env->tiles[ty2][tx], TILE_COAL);
                }
            }
        }
    }
    // Iron: depth > sh+20
    for (int v = 0; v < 12; v++) {
        int cx = (int)(wg_randf(seed ^ 0x2222u, v, 0) * (WORLD_W - 4)) + 2;
        int cy = (int)(wg_randf(seed ^ 0x2222u, v, 1) * (WORLD_H * 0.45f) + WORLD_H * 0.45f);
        int r  = 1 + (int)(wg_randf(seed ^ 0x2222u, v, 2) * 1.5f);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy > r*r + 1) continue;
                int tx = cx + dx, ty2 = cy + dy;
                if (tx < 0 || tx >= WORLD_W || ty2 < 0 || ty2 >= WORLD_H) continue;
                if (TILE_TYPE(env->tiles[ty2][tx]) == TILE_STONE) {
                    TILE_SET_TYPE(env->tiles[ty2][tx], TILE_IRON);
                }
            }
        }
    }
    // Gold: depth > sh+35
    for (int v = 0; v < 6; v++) {
        int cx = (int)(wg_randf(seed ^ 0x3333u, v, 0) * (WORLD_W - 4)) + 2;
        int cy = (int)(wg_randf(seed ^ 0x3333u, v, 1) * (WORLD_H * 0.25f) + WORLD_H * 0.65f);
        int r  = 1;
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int tx = cx + dx, ty2 = cy + dy;
                if (tx < 0 || tx >= WORLD_W || ty2 < 0 || ty2 >= WORLD_H) continue;
                if (TILE_TYPE(env->tiles[ty2][tx]) == TILE_STONE) {
                    TILE_SET_TYPE(env->tiles[ty2][tx], TILE_GOLD);
                }
            }
        }
    }

    // 5. Trees on surface
    for (int tx = 2; tx < WORLD_W - 2; tx++) {
        if (wg_randf(seed ^ 0x4444u, tx, 0) > 0.15f) continue;
        // Need solid grass below
        int sh = surface[tx];
        if (TILE_TYPE(env->tiles[sh][tx]) != TILE_GRASS) continue;
        // Check space for tree
        int height = 4 + (int)(wg_randf(seed ^ 0x4444u, tx, 1) * 3.0f);
        // Trunk
        for (int h = 1; h <= height; h++) {
            int ty = sh - h;
            if (ty < 1) break;
            TILE_SET_TYPE(env->tiles[ty][tx], TILE_WOOD);
        }
        // Leaf cap (3x3 centered at top of trunk)
        int leaf_top = sh - height - 1;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int lx = tx + dx, ly = leaf_top + dy;
                if (lx < 0 || lx >= WORLD_W || ly < 0 || ly >= WORLD_H) continue;
                if (TILE_TYPE(env->tiles[ly][lx]) == TILE_AIR) {
                    TILE_SET_TYPE(env->tiles[ly][lx], TILE_LEAVES);
                }
            }
        }
    }

    // 6. Starter house centered on spawn column
    int sx = WORLD_W / 2;
    int sh = surface[sx];

    // Interior: cols [sx-3, sx+4], rows [sh-4, sh-1] — clear to air with background wall
    for (int ty = sh - 4; ty <= sh - 1; ty++) {
        for (int tx = sx - 3; tx <= sx + 4; tx++) {
            uint8_t t = 0;
            TILE_SET_TYPE(t, TILE_AIR);
            TILE_SET_WALL(t, 1);
            env->tiles[ty][tx] = t;
        }
    }
    // Ceiling: row sh-5, cols sx-4 to sx+5
    for (int tx = sx - 4; tx <= sx + 5; tx++)
        TILE_SET_TYPE(env->tiles[sh - 5][tx], TILE_WOOD);
    // Left wall: col sx-4, rows sh-5 to sh
    for (int ty = sh - 5; ty <= sh; ty++)
        TILE_SET_TYPE(env->tiles[ty][sx - 4], TILE_WOOD);
    // Right wall: col sx+5, rows sh-5 to sh
    for (int ty = sh - 5; ty <= sh; ty++)
        TILE_SET_TYPE(env->tiles[ty][sx + 5], TILE_WOOD);
    // Door opening: 2-tile gap at bottom of left wall
    {
        uint8_t t = 0;
        TILE_SET_TYPE(t, TILE_AIR);
        TILE_SET_WALL(t, 1);
        env->tiles[sh - 1][sx - 4] = t;
        env->tiles[sh - 2][sx - 4] = t;
    }
    // Workbench inside on right side
    TILE_SET_TYPE(env->tiles[sh - 1][sx + 3], TILE_WORKBENCH);

    // 7. Player spawn: above center surface
    float spawn_x = (float)sx + 0.5f;
    float spawn_y = (float)(sh - 1) - PLAYER_HH - 0.05f;
    if (spawn_y < PLAYER_HH) spawn_y = PLAYER_HH;

    env->player.px      = spawn_x;
    env->player.py      = spawn_y;
    env->player.php     = 100;
    env->player.pmax_hp = 100;
    env->player.facing  = 1;
    env->player.on_ground = 0;
    env->player.mine_tx = -1;
    env->player.mine_ty = -1;

    // 8. Starting tools
    inv_add(&env->inventory, ITEM_PICK_WOOD, 1);
    inv_add(&env->inventory, ITEM_AXE_WOOD,  1);
    env->player.equip_pick = ITEM_PICK_WOOD;
    env->player.equip_axe  = ITEM_AXE_WOOD;
}
