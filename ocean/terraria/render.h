#pragma once

// Depends on Terraria struct (included by terraria.h before this file)
// Requires raylib.

#include "raylib.h"

#define RENDER_TILE_SZ 4
#define RENDER_W (WORLD_W * RENDER_TILE_SZ)
#define RENDER_H (WORLD_H * RENDER_TILE_SZ)

static const Color TILE_COLORS[TILE_COUNT] = {
    {  0,   0,   0, 255}, // AIR
    {139,  90,  43, 255}, // DIRT
    { 34, 139,  34, 255}, // GRASS
    {128, 128, 128, 255}, // STONE
    {139,  69,  19, 255}, // WOOD
    { 50, 200,  50, 200}, // LEAVES
    {210, 180, 140, 255}, // SAND
    { 60,  60,  60, 255}, // COAL
    {180, 140, 100, 255}, // IRON
    {255, 215,   0, 255}, // GOLD
    {160, 100,  40, 200}, // PLATFORM
    {200, 160,  80, 255}, // CHEST
    {180, 130,  60, 255}, // WORKBENCH
    {200,  80,  20, 255}, // FURNACE
    {255, 255, 100, 255}, // TORCH
    {120,  80,  40, 255}, // DOOR_CLOSED
    {160, 120,  60, 200}, // DOOR_OPEN
    {200, 100, 100, 255}, // BED
    {200, 200, 210, 255}, // SILVER
    {110,  40, 150, 255}, // CORRUPT_GRASS
};

static const Color ENEMY_COLORS[ENEMY_COUNT] = {
    {  0, 200,   0, 255}, // SLIME
    { 80,  50,  30, 255}, // ZOMBIE
    { 50,  50, 180, 255}, // BAT
    {200, 200, 200, 255}, // SKELETON
    {180,  20, 140, 255}, // BOSS
};

static void c_render(Terraria* env) {
    if (!IsWindowReady()) {
        InitWindow(RENDER_W, RENDER_H, "Terraria RL");
        SetTargetFPS(30);
    }
    if (WindowShouldClose() || IsKeyDown(KEY_ESCAPE)) exit(0);

    BeginDrawing();
    ClearBackground(BLACK);

    // Draw tiles
    for (int ty = 0; ty < WORLD_H; ty++) {
        for (int tx = 0; tx < WORLD_W; tx++) {
            uint8_t tile = env->tiles[ty][tx];
            TileType ttype = TILE_TYPE(tile);
            if (ttype == TILE_AIR) continue;
            int rx = tx * RENDER_TILE_SZ;
            int ry = ty * RENDER_TILE_SZ;
            Color c = TILE_COLORS[ttype];
            if (TILE_HAS_WALL(tile) && ttype == TILE_AIR) {
                DrawRectangle(rx, ry, RENDER_TILE_SZ, RENDER_TILE_SZ, (Color){40,20,10,255});
            } else {
                DrawRectangle(rx, ry, RENDER_TILE_SZ, RENDER_TILE_SZ, c);
            }
        }
    }

    // Draw player
    {
        const Player* p = &env->player;
        int rx = (int)((p->px - PLAYER_HW) * RENDER_TILE_SZ);
        int ry = (int)((p->py - PLAYER_HH) * RENDER_TILE_SZ);
        int rw = (int)(PLAYER_HW * 2 * RENDER_TILE_SZ);
        int rh = (int)(PLAYER_HH * 2 * RENDER_TILE_SZ);
        DrawRectangle(rx, ry, rw, rh, (Color){255, 100, 100, 255});

        if (p->mine_tx >= 0) {
            DrawRectangleLines(p->mine_tx * RENDER_TILE_SZ, p->mine_ty * RENDER_TILE_SZ,
                               RENDER_TILE_SZ, RENDER_TILE_SZ, WHITE);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!env->enemies.eactive[i]) continue;
        float ex = env->enemies.ex[i];
        float ey = env->enemies.ey[i];
        int rx = (int)((ex - ENEMY_HW) * RENDER_TILE_SZ);
        int ry = (int)((ey - ENEMY_HH) * RENDER_TILE_SZ);
        int rw = (int)(ENEMY_HW * 2 * RENDER_TILE_SZ);
        int rh = (int)(ENEMY_HH * 2 * RENDER_TILE_SZ);
        Color c = ENEMY_COLORS[env->enemies.etype[i]];
        DrawRectangle(rx, ry, rw, rh, c);
    }

    DrawText(TextFormat("HP:%d  Day:%s  Tick:%d",
        env->player.php,
        env->is_night ? "NIGHT" : "DAY",
        env->tick), 4, 4, 10, WHITE);

    EndDrawing();
}
