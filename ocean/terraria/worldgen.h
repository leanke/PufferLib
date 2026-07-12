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

// Rightmost DESERT_WIDTH columns are the Desert biome — placed at one edge of
// the map, away from the center-spawned starter house.
#define DESERT_WIDTH 40
static inline int is_desert_column(int tx) { return tx >= WORLD_W - DESERT_WIDTH; }

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

// Places `count` circular ore-vein blobs of `ore` into stone tiles. Vein
// center depth is drawn from [depth_min_frac, depth_max_frac] of WORLD_H,
// radius from [1, 1+r_range] (r_range=0 gives a fixed 3×3 blob).
static void place_ore_blobs(Terraria* env, uint32_t seed, TileType ore, int count,
                             float depth_min_frac, float depth_max_frac, float r_range) {
    for (int v = 0; v < count; v++) {
        int cx = (int)(wg_randf(seed, v, 0) * (WORLD_W - 4)) + 2;
        int cy = (int)(wg_randf(seed, v, 1) * (WORLD_H * (depth_max_frac - depth_min_frac))
                        + WORLD_H * depth_min_frac);
        int r  = 1 + (int)(wg_randf(seed, v, 2) * r_range);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy > r*r + 1) continue;
                int tx = cx + dx, ty2 = cy + dy;
                if (tx < 0 || tx >= WORLD_W || ty2 < 0 || ty2 >= WORLD_H) continue;
                if (TILE_TYPE(env->tiles[ty2][tx]) == TILE_STONE) {
                    TILE_SET_TYPE(env->tiles[ty2][tx], ore);
                }
            }
        }
    }
}

// ─── Main world generator ─────────────────────────────────────────────────────

static void generate_world(Terraria* env) {
    uint32_t seed = (uint32_t)env->rng;
    int surface[WORLD_W];

    // 1. Heightmap
    for (int x = 0; x < WORLD_W; x++)
        surface[x] = surface_height(seed, x);

    // 2. Fill tiles: air → grass→dirt→stone layer (Desert biome columns get
    // sand instead of grass/dirt near the surface; TILE_SAND/ITEM_SAND were
    // already wired for mining/inventory, just unused as a biome until now)
    for (int ty = 0; ty < WORLD_H; ty++) {
        for (int tx = 0; tx < WORLD_W; tx++) {
            uint8_t t = 0;
            int sh = surface[tx];
            int desert = is_desert_column(tx);
            if (ty < sh) {
                TILE_SET_TYPE(t, TILE_AIR);
            } else if (ty <= sh + 6) {
                TileType surf_fill = desert ? TILE_SAND : (ty == sh ? TILE_GRASS : TILE_DIRT);
                TILE_SET_TYPE(t, surf_fill);
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

    // 4. Ore veins (scatter clusters at depth): center depth drawn from
    // [depth_min_frac, depth_max_frac] of WORLD_H, radius from [1, 1+r_range].
    place_ore_blobs(env, seed ^ 0x1111u, TILE_COAL,   20, 0.35f, 0.95f, 2.0f);
    place_ore_blobs(env, seed ^ 0x2222u, TILE_IRON,   12, 0.45f, 0.90f, 1.5f);
    place_ore_blobs(env, seed ^ 0x5555u, TILE_SILVER,  8, 0.55f, 0.85f, 1.2f);
    place_ore_blobs(env, seed ^ 0x3333u, TILE_GOLD,    6, 0.65f, 0.90f, 0.0f);

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

    // Merchant's fixed spot: interior floor, left side (away from the
    // workbench). Position is set now but `active` stays 0 until the player
    // crosses the coin threshold (checked each tick in c_step).
    env->merchant.active = 0;
    env->merchant.nx = (float)(sx - 2) + 0.5f;
    env->merchant.ny = (float)(sh - 1) - PLAYER_HH - 0.05f;

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

    // 9. Corruption seed patch — small, near the left edge (opposite the
    // Desert biome and well away from the center-spawned starter house).
    env->spread_count  = 0;
    env->spread_cursor = 0;
    {
        int ccx = 30;
        int csh = surface[ccx];
        for (int dx = -2; dx <= 2; dx++) {
            int tx = ccx + dx;
            if (tx < 0 || tx >= WORLD_W) continue;
            if (TILE_TYPE(env->tiles[csh][tx]) != TILE_GRASS) continue;
            TILE_SET_TYPE(env->tiles[csh][tx], TILE_CORRUPT_GRASS);
            if (env->spread_count < MAX_SPREADERS) {
                env->spread_x[env->spread_count] = (uint16_t)tx;
                env->spread_y[env->spread_count] = (uint16_t)csh;
                env->spread_count++;
            }
        }
    }
}

// Advances corruption spread by a few tiles per call. Walks a round-robin
// cursor through the active-spreader list (not a full-grid scan) and, for
// each visited spreader, attempts to convert one directly-adjacent
// grass/dirt tile. Newly-converted tiles join the list; a dug trench (mined
// to TILE_AIR) blocks spread for free since only grass/dirt are convertible.
#define SPREAD_PER_TICK 2
static void spread_step(Terraria* env) {
    if (env->spread_count == 0) return;
    static const int SDX[4] = { 0, 0, -1, 1 };
    static const int SDY[4] = { -1, 1, 0, 0 };

    for (int n = 0; n < SPREAD_PER_TICK; n++) {
        uint16_t idx = (uint16_t)(env->spread_cursor % env->spread_count);
        env->spread_cursor++;
        int sx = env->spread_x[idx];
        int sy = env->spread_y[idx];

        uint32_t* rng = &env->rng;
        *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
        int start_dir = (int)(*rng & 3u);

        for (int k = 0; k < 4; k++) {
            int d  = (start_dir + k) & 3;
            int tx = sx + SDX[d];
            int ty = sy + SDY[d];
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            TileType tt = TILE_TYPE(env->tiles[ty][tx]);
            if (tt != TILE_GRASS && tt != TILE_DIRT) continue;

            TILE_SET_TYPE(env->tiles[ty][tx], TILE_CORRUPT_GRASS);
            if (env->spread_count < MAX_SPREADERS) {
                env->spread_x[env->spread_count] = (uint16_t)tx;
                env->spread_y[env->spread_count] = (uint16_t)ty;
                env->spread_count++;
            } else {
                // List is full: overwrite the slot we're about to revisit
                // next rather than growing unbounded.
                uint16_t ovf = (uint16_t)(env->spread_cursor % MAX_SPREADERS);
                env->spread_x[ovf] = (uint16_t)tx;
                env->spread_y[ovf] = (uint16_t)ty;
            }
            break;
        }
    }
}
