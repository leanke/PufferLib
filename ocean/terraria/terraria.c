// Standalone playable demo + benchmark for the Terraria RL environment.
// Build: ./build.sh terraria --fast
// Run:   ./terraria [seed] [--headless] [--bench N]
//
// Controls:
//   A/D or Arrows     Move left / right
//   W / Space         Jump
//   S / Down          Descend (crouch into openings)
//   Q                 Aim up (mine above head without jumping)
//   Z or Left-click   Mine / Attack
//   X or Right-click  Place block in selected slot
//   1-0               Select hotbar slot 0-9
//   Mouse wheel       Cycle hotbar slot
//   Hold C            Show crafting menu
//   C + 1-9           Craft recipe 1-9
//   C + 0             Craft recipe 10
//   C + Shift + 1-9   Craft recipe 11-19
//   C + Shift + 0     Craft recipe 20
//   H                 Toggle controls overlay
//   ESC               Quit

#include "terraria.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DEMO_TILE_SZ  12
#define DEMO_WIN_W    800
#define DEMO_WIN_H    500
#define DEMO_HUD_H    60
#define DEMO_PLAY_H   (DEMO_WIN_H - DEMO_HUD_H)        // 440
#define DEMO_VIEW_TW  (DEMO_WIN_W / DEMO_TILE_SZ)       // 66
#define DEMO_VIEW_TH  (DEMO_PLAY_H / DEMO_TILE_SZ)      // 36
#define DEMO_FPS      30

static const char* ITEM_NAMES[ITEM_COUNT] = {
    "Nothing",                                                  // 0
    "Dirt", "Stone", "Wood", "Coal",                           // 1-4
    "Iron Ore", "Gold Ore", "Iron Bar", "Gold Bar", "Sand",   // 5-9
    "Torch", "Platform",                                        // 10-11
    "Chest Block", "Workbench", "Furnace", "Door", "Bed",     // 12-16
    "Wood Pick", "Stone Pick", "Iron Pick", "Gold Pick",       // 17-20
    "Wood Axe", "Stone Axe", "Iron Axe",                       // 21-23
    "Wood Sword", "Stone Sword", "Iron Sword", "Gold Sword",   // 24-27
    "Iron Helm", "Gold Helm",                                  // 28-29
    "Iron Chest", "Gold Chest",                                // 30-31
    "Iron Legs", "Gold Legs",                                  // 32-33
};

static const char* ITEM_SHORT[ITEM_COUNT] = {
    "-",
    "Dirt","Stone","Wood","Coal",
    "IronOre","GoldOre","IronBar","GoldBar","Sand",
    "Torch","Platfrm",
    "Chest","Wrkbnch","Furnace","Door","Bed",
    "WoPick","StPick","IrPick","AuPick",
    "WoAxe","StAxe","IrAxe",
    "WoSword","StSword","IrSword","AuSword",
    "IrHelm","AuHelm",
    "IrChest","AuChest",
    "IrLegs","AuLegs",
};

static const char* RECIPE_NAMES[NUM_RECIPES] = {
    "Workbench",
    "Wood Pick",  "Wood Axe",   "Wood Sword",
    "Torch x8",   "Platform x4",
    "Stone Pick", "Stone Axe",  "Stone Sword",
    "Furnace",
    "Iron Bar",   "Gold Bar",
    "Iron Pick",  "Iron Axe",   "Iron Sword",  "Iron Helm",
    "Gold Pick",  "Gold Sword",
    "Door",       "Bed",
};

static const Color ITEM_COLORS[ITEM_COUNT] = {
    {60,60,60,255},      // None
    {139,90,43,255},     // Dirt
    {128,128,128,255},   // Stone
    {139,69,19,255},     // Wood
    {50,50,50,255},      // Coal
    {160,120,80,255},    // Iron Ore
    {220,180,30,255},    // Gold Ore
    {180,170,160,255},   // Iron Bar
    {240,200,40,255},    // Gold Bar
    {210,180,140,255},   // Sand
    {255,230,80,255},    // Torch
    {170,110,50,255},    // Platform
    {200,160,80,255},    // Chest Block
    {180,130,60,255},    // Workbench
    {200,80,20,255},     // Furnace
    {120,80,40,255},     // Door
    {200,100,100,255},   // Bed
    {120,90,40,255},     // Wood Pick
    {110,110,110,255},   // Stone Pick
    {180,170,160,255},   // Iron Pick
    {240,200,40,255},    // Gold Pick
    {120,90,40,255},     // Wood Axe
    {110,110,110,255},   // Stone Axe
    {180,170,160,255},   // Iron Axe
    {120,90,40,255},     // Wood Sword
    {110,110,110,255},   // Stone Sword
    {180,170,160,255},   // Iron Sword
    {240,200,40,255},    // Gold Sword
    {180,170,160,255},   // Iron Helm
    {240,200,40,255},    // Gold Helm
    {180,170,160,255},   // Iron Chest
    {240,200,40,255},    // Gold Chest
    {180,170,160,255},   // Iron Legs
    {240,200,40,255},    // Gold Legs
};

static int can_craft(const Terraria* env, int r) {
    if (r < 0 || r >= NUM_RECIPES) return 0;
    const Recipe* rc = &RECIPES[r];
    if (rc->station == STATION_WORKBENCH && !has_station(env, TILE_WORKBENCH)) return 0;
    if (rc->station == STATION_FURNACE   && !has_station(env, TILE_FURNACE))   return 0;
    for (int i = 0; i < rc->n_in; i++) {
        if (rc->in_item[i] == ITEM_NONE) continue;
        if (inv_count(&env->inventory, rc->in_item[i]) < rc->in_count[i]) return 0;
    }
    return 1;
}

static void draw_demo(const Terraria* env, int sel, int show_help, int show_craft,
                      int death_flash) {
    // Camera: top-left corner in tile space
    float cam_x = env->player.px - DEMO_VIEW_TW * 0.5f;
    float cam_y = env->player.py - DEMO_VIEW_TH * 0.5f;
    if (cam_x < 0.0f) cam_x = 0.0f;
    if (cam_y < 0.0f) cam_y = 0.0f;
    if (cam_x > WORLD_W - DEMO_VIEW_TW) cam_x = (float)(WORLD_W - DEMO_VIEW_TW);
    if (cam_y > WORLD_H - DEMO_VIEW_TH) cam_y = (float)(WORLD_H - DEMO_VIEW_TH);

    BeginDrawing();
    Color sky = env->is_night ? (Color){8,8,30,255} : (Color){82,148,218,255};
    ClearBackground(sky);

    int tx0 = (int)cam_x;
    int ty0 = (int)cam_y;
    for (int ty = ty0; ty <= ty0 + DEMO_VIEW_TH + 1 && ty < WORLD_H; ty++) {
        for (int tx = tx0; tx <= tx0 + DEMO_VIEW_TW + 1 && tx < WORLD_W; tx++) {
            uint8_t tile = env->tiles[ty][tx];
            TileType ttype = TILE_TYPE(tile);
            int sx = (int)((tx - cam_x) * DEMO_TILE_SZ);
            int sy = (int)((ty - cam_y) * DEMO_TILE_SZ);
            if (ttype == TILE_AIR) {
                if (TILE_HAS_WALL(tile))
                    DrawRectangle(sx, sy, DEMO_TILE_SZ, DEMO_TILE_SZ, (Color){38,22,12,255});
                continue;
            }
            Color tc = TILE_COLORS[ttype];
            DrawRectangle(sx, sy, DEMO_TILE_SZ, DEMO_TILE_SZ, tc);
            DrawRectangle(sx, sy, DEMO_TILE_SZ, 1, (Color){0,0,0,50});
            DrawRectangle(sx, sy, 1, DEMO_TILE_SZ, (Color){0,0,0,50});
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!env->enemies.eactive[i]) continue;
        float ex = env->enemies.ex[i];
        float ey = env->enemies.ey[i];
        int sx = (int)((ex - ENEMY_HW - cam_x) * DEMO_TILE_SZ);
        int sy = (int)((ey - ENEMY_HH - cam_y) * DEMO_TILE_SZ);
        int sw = (int)(ENEMY_HW * 2.0f * DEMO_TILE_SZ);
        int sh = (int)(ENEMY_HH * 2.0f * DEMO_TILE_SZ);
        if (sx + sw < 0 || sx > DEMO_WIN_W || sy + sh < 0 || sy > DEMO_PLAY_H) continue;
        DrawRectangle(sx, sy, sw, sh, ENEMY_COLORS[env->enemies.etype[i]]);
        int16_t hp = env->enemies.ehp[i];
        int16_t mhp = ENEMY_PROPS[env->enemies.etype[i]].hp;
        if (mhp > 0) {
            int hw = sw * hp / mhp;
            DrawRectangle(sx, sy - 4, sw, 3, (Color){80,0,0,200});
            DrawRectangle(sx, sy - 4, hw, 3, (Color){220,40,40,255});
        }
    }

    {
        const Player* p = &env->player;
        int sx = (int)((p->px - PLAYER_HW - cam_x) * DEMO_TILE_SZ);
        int sy = (int)((p->py - PLAYER_HH - cam_y) * DEMO_TILE_SZ);
        int sw = (int)(PLAYER_HW * 2.0f * DEMO_TILE_SZ);
        int sh = (int)(PLAYER_HH * 2.0f * DEMO_TILE_SZ);
        Color body = (p->iframes > 0 && (p->iframes / 3) % 2 == 0)
                     ? (Color){255,255,255,200}
                     : (Color){220,70,70,255};
        DrawRectangle(sx, sy, sw, sh, body);
        DrawRectangle(sx + 1, sy + 1, sw - 2, sh / 3, (Color){240,120,120,255});
        int eye_x = sx + (p->facing > 0 ? sw - 3 : 1);
        DrawRectangle(eye_x, sy + 2, 2, 2, WHITE);

        if (p->mine_tx >= 0) {
            int mx = (int)((p->mine_tx - cam_x) * DEMO_TILE_SZ);
            int my = (int)((p->mine_ty - cam_y) * DEMO_TILE_SZ);
            DrawRectangleLines(mx, my, DEMO_TILE_SZ, DEMO_TILE_SZ, WHITE);
            uint8_t mtile = env->tiles[p->mine_ty][p->mine_tx];
            int hard = TILE_PROPS[TILE_TYPE(mtile)].hardness;
            if (hard > 0) {
                int prog_w = DEMO_TILE_SZ * p->mine_progress / hard;
                DrawRectangle(mx, my + DEMO_TILE_SZ - 2, prog_w, 2, (Color){255,200,0,255});
            }
        }
    }

    DrawRectangle(0, DEMO_PLAY_H, DEMO_WIN_W, DEMO_HUD_H, (Color){14,14,22,235});
    DrawLine(0, DEMO_PLAY_H, DEMO_WIN_W, DEMO_PLAY_H, (Color){55,55,75,255});

    // Left: HP bar + status
    {
        int bx = 6, by = DEMO_PLAY_H + 5;
        int bw = 140, bh = 11;
        DrawRectangle(bx, by, bw, bh, (Color){50,0,0,255});
        if (env->player.pmax_hp > 0) {
            int hw = bw * env->player.php / env->player.pmax_hp;
            if (hw > 0) DrawRectangle(bx, by, hw, bh, (Color){220,40,40,255});
        }
        DrawText(TextFormat("HP  %d / %d", env->player.php, env->player.pmax_hp),
                 bx + 3, by + 1, 8, WHITE);
        Color dc = env->is_night ? (Color){110,110,240,255} : (Color){255,215,60,255};
        DrawText(env->is_night ? "NIGHT" : "DAY",   6, DEMO_PLAY_H + 20, 9, dc);
        DrawText(TextFormat("T:%d", env->tick),     46, DEMO_PLAY_H + 20, 8, (Color){160,160,160,255});
        DrawText(TextFormat("E:%d", enemies_count(&env->enemies)),
                             90, DEMO_PLAY_H + 20, 8, (Color){160,160,160,255});
        DrawText("H:help", 6, DEMO_PLAY_H + 32, 8, (Color){120,120,120,255});
    }

    // Center: hotbar slots 0-9
    {
        int slot_sz = 40;
        int n_hot   = 10;
        int hbx     = (DEMO_WIN_W - n_hot * slot_sz) / 2;
        int hby     = DEMO_PLAY_H + 5;
        int icon_sz = slot_sz - 12;

        for (int s = 0; s < n_hot; s++) {
            int rx = hbx + s * slot_sz;
            int ry = hby;
            int is_sel = (s == sel);
            DrawRectangle(rx, ry, slot_sz - 2, slot_sz - 2,
                          is_sel ? (Color){65,65,105,255} : (Color){30,30,50,220});
            DrawRectangleLines(rx, ry, slot_sz - 2, slot_sz - 2,
                               is_sel ? WHITE : (Color){65,65,88,255});

            const InvSlot* sl = &env->inventory.slots[s];
            if (sl->item_id != ITEM_NONE) {
                Color ic = ITEM_COLORS[sl->item_id < ITEM_COUNT ? sl->item_id : 0];
                DrawRectangle(rx + 4, ry + 4, icon_sz, icon_sz - 2, ic);
                DrawText(TextFormat("%d", sl->count), rx + 3, ry + slot_sz - 13, 7, WHITE);
            }
            DrawText(TextFormat("%d", (s + 1) % 10), rx + 2, ry + 1, 7, (Color){150,150,150,180});
        }

        // Item name below hotbar
        const InvSlot* cur = &env->inventory.slots[sel < 32 ? sel : 0];
        if (cur->item_id != ITEM_NONE && cur->item_id < ITEM_COUNT) {
            int name_w = MeasureText(ITEM_NAMES[cur->item_id], 9);
            DrawText(ITEM_NAMES[cur->item_id],
                     hbx + (n_hot * slot_sz - name_w) / 2,
                     hby + slot_sz + 1, 9, (Color){220,220,220,255});
        }
    }

    // Right: equipment summary
    {
        const Player* p = &env->player;
        int ex = DEMO_WIN_W - 175;
        int ey = DEMO_PLAY_H + 4;
        DrawText(TextFormat("Pick:%-9s Axe:%s",
            p->equip_pick   ? ITEM_SHORT[p->equip_pick]   : "-",
            p->equip_axe    ? ITEM_SHORT[p->equip_axe]    : "-"),
            ex, ey,      8, (Color){190,190,190,255});
        DrawText(TextFormat("Wpn: %-9s Helm:%s",
            p->equip_weapon ? ITEM_SHORT[p->equip_weapon] : "-",
            p->equip_helmet ? ITEM_SHORT[p->equip_helmet] : "-"),
            ex, ey + 13, 8, (Color){190,190,190,255});
        DrawText(TextFormat("Chest:%-8s Legs:%s",
            p->equip_chest  ? ITEM_SHORT[p->equip_chest]  : "-",
            p->equip_legs   ? ITEM_SHORT[p->equip_legs]   : "-"),
            ex, ey + 26, 8, (Color){190,190,190,255});
    }

    if (show_craft) {
        int col_w = 250, row_h = 13;
        int rows  = NUM_RECIPES / 2;  // 10 per column
        int pan_w = col_w * 2 + 10;
        int pan_h = rows * row_h + 24;
        int pan_x = (DEMO_WIN_W - pan_w) / 2;
        int pan_y = (DEMO_PLAY_H - pan_h) / 2;
        DrawRectangle(pan_x, pan_y, pan_w, pan_h, (Color){10,10,20,225});
        DrawRectangleLines(pan_x, pan_y, pan_w, pan_h, (Color){80,80,120,255});
        DrawText("CRAFTING  (hold C + key)", pan_x + 6, pan_y + 4, 9, YELLOW);
        for (int r = 0; r < NUM_RECIPES; r++) {
            int col = r / rows;
            int row = r % rows;
            int rx  = pan_x + 5 + col * col_w;
            int ry  = pan_y + 20 + row * row_h;
            const char* key;
            if      (r < 9)  key = TextFormat("C+%d",   r + 1);
            else if (r == 9) key = "C+0";
            else if (r < 19) key = TextFormat("C+S+%d", r - 9);
            else             key = "C+S+0";
            int craftable = can_craft(env, r);
            Color rc = craftable ? (Color){110,220,110,255} : (Color){130,130,130,200};
            DrawText(TextFormat("%-7s %s", key, RECIPE_NAMES[r]), rx, ry, 9, rc);
        }
    }

    if (show_help) {
        int hx = 10, hy = 10;
        DrawRectangle(hx - 4, hy - 4, 218, 152, (Color){0,0,0,185});
        DrawText("CONTROLS  (H to toggle)", hx, hy, 10, YELLOW); hy += 14;
        DrawText("WASD / Arrows   Move", hx, hy, 9, WHITE); hy += 11;
        DrawText("W / Space       Jump", hx, hy, 9, WHITE); hy += 11;
        DrawText("KP 1-9          Focus tile (numpad)", hx, hy, 9, WHITE); hy += 11;
        DrawText("Q               Focus up (KP8)", hx, hy, 9, WHITE); hy += 11;
        DrawText("S / Down        Focus down (KP2)", hx, hy, 9, WHITE); hy += 11;
        DrawText("Z / Left-click  Mine / Attack", hx, hy, 9, WHITE); hy += 11;
        DrawText("X / Right-click Place block", hx, hy, 9, WHITE); hy += 11;
        DrawText("1-0             Select slot", hx, hy, 9, WHITE); hy += 11;
        DrawText("Scroll wheel    Cycle slot", hx, hy, 9, WHITE); hy += 11;
        DrawText("Hold C          Crafting menu", hx, hy, 9, WHITE); hy += 11;
        DrawText("C+1..9, C+0     Craft 1-10", hx, hy, 9, WHITE); hy += 11;
        DrawText("C+S+1..9, C+S+0 Craft 11-20", hx, hy, 9, WHITE); hy += 11;
        DrawText("ESC             Quit", hx, hy, 9, WHITE);
    }

    if (death_flash > 0) {
        int alpha = death_flash * 5;
        if (alpha > 180) alpha = 180;
        DrawRectangle(0, 0, DEMO_WIN_W, DEMO_PLAY_H, (Color){200,0,0,alpha});
        if (death_flash > 25) {
            DrawText("YOU DIED", DEMO_WIN_W/2 - 55, DEMO_PLAY_H/2 - 12, 24, WHITE);
        }
    }

    EndDrawing();
}

static void demo_mode(unsigned int seed) {
    InitWindow(DEMO_WIN_W, DEMO_WIN_H, "Terraria RL Demo");
    SetTargetFPS(DEMO_FPS);

    Terraria env;
    memset(&env, 0, sizeof(Terraria));
    env.num_agents   = 1;
    env.rng          = seed;
    env.observations = (float*)calloc(512, sizeof(float));
    env.actions      = (float*)calloc(5,   sizeof(float));
    env.rewards      = (float*)calloc(1,   sizeof(float));
    env.terminals    = (float*)calloc(1,   sizeof(float));
    c_reset(&env);

    printf("Terraria RL Demo  seed=%u  world=%dx%d  size=%.1f KB\n",
           seed, WORLD_W, WORLD_H, (double)sizeof(Terraria) / 1024.0);
    printf("Press H to show/hide controls\n");

    int sel          = 0;
    int show_help    = 1;
    int show_craft   = 0;
    int death_flash  = 0;
    long steps       = 0;
    long episodes    = 1;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) break;
        if (IsKeyPressed(KEY_H)) show_help = !show_help;

        for (int k = 0; k < 9; k++)
            if (IsKeyPressed(KEY_ONE + k)) sel = k;
        if (IsKeyPressed(KEY_ZERO)) sel = 9;
        float wheel = GetMouseWheelMove();
        if (wheel >  0.4f) sel = (sel + 31) % 32;
        if (wheel < -0.4f) sel = (sel +  1) % 32;

        int ml = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
        int mr = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
        int mj = IsKeyDown(KEY_W) || IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP);
        int move = 0;
        if      (mj && ml) move = 4;
        else if (mj && mr) move = 5;
        else if (mj)       move = 3;
        else if (ml)       move = 1;
        else if (mr)       move = 2;
        env.actions[0] = (float)move;

        int tool = 0;
        if (IsMouseButtonDown(0) || IsKeyDown(KEY_Z))       tool = 1;
        else if (IsMouseButtonDown(1) || IsKeyDown(KEY_X))  tool = 2;
        env.actions[1] = (float)tool;
        env.actions[2] = (float)sel;

        // ── Focus (branch 4) — numpad 1-9 selects surrounding tile ───────────
        int focus = 0;
        for (int k = 1; k <= 9; k++)
            if (IsKeyDown(KEY_KP_0 + k)) { focus = k; break; }
        if (focus == 0 && IsKeyDown(KEY_Q))                              focus = 8; // up
        if (focus == 0 && (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)))    focus = 2; // down
        env.actions[4] = (float)focus;

        show_craft     = IsKeyDown(KEY_C);
        int craft      = 0;
        if (IsKeyDown(KEY_C)) {
            int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            for (int k = 0; k < 9; k++) {
                if (IsKeyPressed(KEY_ONE + k)) {
                    craft = shift ? (k + 11) : (k + 1);
                    if (craft > NUM_RECIPES) craft = 0;
                }
            }
            if (IsKeyPressed(KEY_ZERO)) {
                craft = shift ? 20 : 10;
                if (craft > NUM_RECIPES) craft = 0;
            }
        }
        env.actions[3] = (float)craft;

        c_step(&env);
        steps++;

        if (env.terminals[0] > 0.5f) {
            printf("Episode %ld  steps=%ld  return=%.1f  mined=%.0f  crafted=%.0f  kills=%.0f\n",
                   episodes, steps, env.log.episode_return,
                   env.log.tiles_mined, env.log.items_crafted, env.log.enemies_killed);
            episodes++;
            death_flash = 50;
        }
        if (death_flash > 0) death_flash--;

        draw_demo(&env, sel, show_help, show_craft, death_flash);
    }

    printf("Played %ld steps, %ld episodes\n", steps, episodes - 1);
    CloseWindow();
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
}

static uint32_t xorshift32(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x ? x : 0xDEADBEEFu;
    return x;
}

static void bench_mode(int n_envs, unsigned int seed) {
    printf("Benchmarking %d envs x 10000 steps...\n", n_envs);
    Terraria* envs  = (Terraria*)calloc(n_envs, sizeof(Terraria));
    float* obs_buf  = (float*)calloc(n_envs * 512, sizeof(float));
    float* act_buf  = (float*)calloc(n_envs * 5,   sizeof(float));
    float* rew_buf  = (float*)calloc(n_envs,        sizeof(float));
    float* term_buf = (float*)calloc(n_envs,         sizeof(float));

    for (int e = 0; e < n_envs; e++) {
        envs[e].num_agents   = 1;
        envs[e].rng          = seed + (unsigned int)e;
        envs[e].observations = obs_buf  + e * 512;
        envs[e].actions      = act_buf  + e * 5;
        envs[e].rewards      = rew_buf  + e;
        envs[e].terminals    = term_buf + e;
        c_reset(&envs[e]);
    }

    uint32_t rng = seed ^ 0x12345678u;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long total = 0;
    for (int step = 0; step < 10000; step++) {
        for (int e = 0; e < n_envs; e++) {
            envs[e].actions[0] = (float)(xorshift32(&rng) % 6);
            envs[e].actions[1] = (float)(xorshift32(&rng) % 3);
            envs[e].actions[2] = (float)(xorshift32(&rng) % 32);
            envs[e].actions[3] = (float)(xorshift32(&rng) % 21);
            envs[e].actions[4] = (float)(xorshift32(&rng) % 10);
            c_step(&envs[e]);
        }
        total += n_envs;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("%.0f steps/sec  (%.3fs elapsed)\n", (double)total / elapsed, elapsed);

    free(envs); free(obs_buf); free(act_buf); free(rew_buf); free(term_buf);
}

static void headless_mode(unsigned int seed) {
    Terraria env;
    memset(&env, 0, sizeof(Terraria));
    env.num_agents   = 1;
    env.rng          = seed;
    env.observations = (float*)calloc(512, sizeof(float));
    env.actions      = (float*)calloc(5,   sizeof(float));
    env.rewards      = (float*)calloc(1,   sizeof(float));
    env.terminals    = (float*)calloc(1,   sizeof(float));
    c_reset(&env);

    printf("Headless random policy  seed=%u  env_size=%.1f KB\n",
           seed, (double)sizeof(Terraria) / 1024.0);

    uint32_t rng = seed ^ 0xABCD1234u;
    long steps = 0, eps = 0;
    while (steps < 200000) {
        env.actions[0] = (float)(xorshift32(&rng) % 6);
        env.actions[1] = (float)(xorshift32(&rng) % 3);
        env.actions[2] = (float)(xorshift32(&rng) % 32);
        env.actions[3] = (float)(xorshift32(&rng) % 21);
        env.actions[4] = (float)(xorshift32(&rng) % 10);
        c_step(&env);
        steps++;
        if (env.terminals[0] > 0.5f) {
            eps++;
            printf("Episode %ld  step %ld  return=%.1f\n", eps, steps, env.log.episode_return);
        }
    }
    printf("Done: %ld steps, %ld episodes\n", steps, eps);
    free(env.observations); free(env.actions); free(env.rewards); free(env.terminals);
}

int main(int argc, char** argv) {
    unsigned int seed = (unsigned int)time(NULL);
    int headless  = 0;
    int bench_n   = 0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--bench") == 0 && i + 1 < argc)
            bench_n = atoi(argv[++i]);
        else
            seed = (unsigned int)atoi(argv[i]);
    }

    if (bench_n > 0)  { bench_mode(bench_n, seed); return 0; }
    if (headless)     { headless_mode(seed);        return 0; }
    demo_mode(seed);
    return 0;
}
