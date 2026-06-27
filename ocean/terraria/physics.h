#pragma once

// Depends on Terraria struct (included by terraria.h before this file)

// Physics constants defined in terraria.h before this file is included.
// (PLAYER_HW, PLAYER_HH, WALK_SPEED, JUMP_SPEED, GRAVITY, MAX_FALL)

static inline int solid_at(const Terraria* env, float wx, float wy) {
    if (wx < 0.0f || wy < 0.0f) return 1;
    int tx = (int)wx;
    int ty = (int)wy;
    if (tx >= WORLD_W || ty >= WORLD_H) return 1;
    return TILE_PROPS[TILE_TYPE(env->tiles[ty][tx])].is_solid;
}

// Check all 4 corners of the player AABB for solid tiles
static inline int player_overlaps(const Terraria* env, float cx, float cy) {
    return solid_at(env, cx - PLAYER_HW, cy - PLAYER_HH) ||
           solid_at(env, cx + PLAYER_HW, cy - PLAYER_HH) ||
           solid_at(env, cx - PLAYER_HW, cy + PLAYER_HH) ||
           solid_at(env, cx + PLAYER_HW, cy + PLAYER_HH);
}

// Same check for a generic AABB (used for enemies: ew=half-width, eh=half-height)
static inline int aabb_overlaps(const Terraria* env, float cx, float cy, float hw, float hh) {
    return solid_at(env, cx - hw, cy - hh) ||
           solid_at(env, cx + hw, cy - hh) ||
           solid_at(env, cx - hw, cy + hh) ||
           solid_at(env, cx + hw, cy + hh);
}

static void physics_step(Terraria* env) {
    Player* p = &env->player;

    if (!p->on_ground) {
        p->pvy += GRAVITY;
        if (p->pvy > MAX_FALL) p->pvy = MAX_FALL;
    } else {
        if (!solid_at(env, p->px - PLAYER_HW, p->py + PLAYER_HH + 0.05f) &&
            !solid_at(env, p->px + PLAYER_HW, p->py + PLAYER_HH + 0.05f)) {
            p->on_ground = 0;
        }
        p->pvx *= 0.75f;
        if (fabsf(p->pvx) < 0.005f) p->pvx = 0.0f;
    }

    if (p->pvx != 0.0f) {
        float nx = p->px + p->pvx;
        if (player_overlaps(env, nx, p->py)) {
            // Try step-up (only when grounded)
            if (p->on_ground && !player_overlaps(env, nx, p->py - 1.0f)) {
                p->px  = nx;
                p->py -= 1.0f;
                while (!solid_at(env, p->px - PLAYER_HW, p->py + PLAYER_HH + 0.05f) &&
                       !solid_at(env, p->px + PLAYER_HW, p->py + PLAYER_HH + 0.05f) &&
                       p->py < (float)WORLD_H) {
                    p->py += 0.1f;
                }
                p->py -= 0.1f;
                p->on_ground = 1;
            } else {
                p->pvx = 0.0f;
            }
        } else {
            p->px = nx;
        }
    }

    // ── Move Y (stepped to prevent tunneling through thin floors/ceilings) ─
    if (p->pvy != 0.0f) {
        float remaining = p->pvy;
        while (fabsf(remaining) > 1e-6f) {
            float sign = (remaining < 0.0f) ? -1.0f : 1.0f;
            float step = sign * fminf(fabsf(remaining), 0.5f);
            float ny = p->py + step;
            if (player_overlaps(env, p->px, ny)) {
                if (step > 0.0f) {
                    int ty_floor = (int)(p->py + PLAYER_HH + step);
                    p->py = (float)ty_floor - PLAYER_HH - 0.001f;
                    p->on_ground = 1;
                } else {
                    int ty_ceil = (int)(p->py - PLAYER_HH + step);
                    p->py = (float)(ty_ceil + 1) + PLAYER_HH + 0.001f;
                }
                p->pvy = 0.0f;
                break;
            }
            p->py = ny;
            remaining -= step;
            if (step > 0.0f) p->on_ground = 0;
        }
    }

    if (p->px < PLAYER_HW)              { p->px = PLAYER_HW;              p->pvx = 0.0f; }
    if (p->px > WORLD_W - PLAYER_HW)   { p->px = WORLD_W - PLAYER_HW;   p->pvx = 0.0f; }
    if (p->py < PLAYER_HH)             { p->py = PLAYER_HH;              p->pvy = 0.0f; }
    if (p->py > WORLD_H - PLAYER_HH)   { p->py = WORLD_H - PLAYER_HH;   p->pvy = 0.0f; p->on_ground = 1; }
}

#define ENEMY_HW 0.38f
#define ENEMY_HH 0.38f

static void enemy_physics(Terraria* env, int i) {
    EnemySoA* e = &env->enemies;
    uint8_t etype = e->etype[i];
    int flying = ENEMY_PROPS[etype].is_flying;

    if (!flying) {
        e->evy[i] += GRAVITY;
        if (e->evy[i] > MAX_FALL) e->evy[i] = MAX_FALL;
    } else {
        // Bats: float toward target y
        e->evy[i] *= 0.9f;
    }

    float nx = e->ex[i] + e->evx[i];
    if (aabb_overlaps(env, nx, e->ey[i], ENEMY_HW, ENEMY_HH)) {
        if (!flying) {
            // Try step-up
            if (!aabb_overlaps(env, nx, e->ey[i] - 1.0f, ENEMY_HW, ENEMY_HH)) {
                e->ex[i] = nx;
                e->ey[i] -= 1.0f;
            } else {
                // If can't step up, try jumping
                if (solid_at(env, e->ex[i] - ENEMY_HW, e->ey[i] + ENEMY_HH + 0.1f) ||
                    solid_at(env, e->ex[i] + ENEMY_HW, e->ey[i] + ENEMY_HH + 0.1f)) {
                    e->evy[i] = -2.0f; // jump
                }
                e->evx[i] = -e->evx[i]; // reverse direction
            }
        } else {
            e->evx[i] = 0.0f;
        }
    } else {
        e->ex[i] = nx;
    }

    float ny = e->ey[i] + e->evy[i];
    if (!flying && aabb_overlaps(env, e->ex[i], ny, ENEMY_HW, ENEMY_HH)) {
        if (e->evy[i] > 0.0f) {
            int ty_floor = (int)(e->ey[i] + ENEMY_HH + e->evy[i]);
            e->ey[i] = (float)ty_floor - ENEMY_HH - 0.001f;
        } else {
            int ty_ceil = (int)(e->ey[i] - ENEMY_HH + e->evy[i]);
            e->ey[i] = (float)(ty_ceil + 1) + ENEMY_HH + 0.001f;
        }
        e->evy[i] = 0.0f;
    } else {
        e->ey[i] = ny;
    }

    if (e->ex[i] < ENEMY_HW)            e->ex[i] = ENEMY_HW;
    if (e->ex[i] > WORLD_W - ENEMY_HW)  e->ex[i] = WORLD_W - ENEMY_HW;
    if (e->ey[i] < ENEMY_HH)            e->ey[i] = ENEMY_HH;
    if (e->ey[i] > WORLD_H - ENEMY_HH)  e->ey[i] = WORLD_H - ENEMY_HH;
}
